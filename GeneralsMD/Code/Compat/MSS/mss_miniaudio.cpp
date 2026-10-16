/*
**	Command & Conquer Generals Zero Hour(tm) - modified version.
**	Licensed under the GNU General Public License v3 or later (see LICENSE.md).
*/
// mss_miniaudio.cpp - the Miles Sound System calls used by MilesAudioManager, implemented on
// miniaudio.  See Compat/MSS/MSS.h for the overview.
//
// Threading model
//   * Every AIL_ function takes g_lock for its own duration only.
//   * miniaudio mixes on its own real-time thread.  When a sound reaches its end, that thread only
//     sets a flag and wakes the service thread.
//   * The service thread calls the game's end-of-sample callbacks without holding g_lock (the game
//     starts the next sound from inside the callback, as it did with Miles' timer thread).
//   * Sample and stream objects are pooled and never freed before AIL_shutdown, so a handle that
//     the game still holds always points to valid memory.

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_ENABLE_ONLY_SPECIFIC_BACKENDS
#define MA_ENABLE_WASAPI
#define MA_ENABLE_DSOUND
#define MA_ENABLE_WINMM
#define MA_ENABLE_NULL
#include "miniaudio.h"

#include "MSS/MSS.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace {

const S32 DEFAULT_PLAYBACK_RATE = 22050;
const int NUM_USER_DATA = 8;

enum ObjectKind { KIND_SAMPLE_2D, KIND_SAMPLE_3D, KIND_LISTENER };

// ------------------------------------------------------------------------------------------------
// Globals
// ------------------------------------------------------------------------------------------------
std::recursive_mutex g_lock;
ma_engine g_engine;
bool g_engineReady = false;
ZhMssDriver *g_driver = NULL;

AIL_file_open_callback g_fileOpen = NULL;
AIL_file_close_callback g_fileClose = NULL;
AIL_file_seek_callback g_fileSeek = NULL;
AIL_file_read_callback g_fileRead = NULL;

std::thread g_serviceThread;
HANDLE g_wake = NULL;					// auto-reset event: something reached its end
std::atomic<bool> g_quit(false);

char g_versionString[] = "miniaudio " MA_VERSION_STRING " (Miles-compatible layer)";

// ------------------------------------------------------------------------------------------------
// .wav parsing
// ------------------------------------------------------------------------------------------------
inline U32 ReadU32(const unsigned char *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((U32)p[3] << 24); }
inline U32 ReadU16(const unsigned char *p) { return p[0] | (p[1] << 8); }

// Fills info from a RIFF/WAVE image.  Returns false if it is not one.
bool ParseWav(const void *image, AILSOUNDINFO *info)
{
	memset(info, 0, sizeof(*info));
	const unsigned char *p = (const unsigned char *)image;
	if (!p || memcmp(p, "RIFF", 4) != 0 || memcmp(p + 8, "WAVE", 4) != 0) {
		return false;
	}
	const U32 riffEnd = ReadU32(p + 4) + 8;
	U32 offset = 12;
	U32 factSamples = 0;
	bool haveFmt = false;
	while (offset + 8 <= riffEnd) {
		const unsigned char *chunk = p + offset;
		const U32 size = ReadU32(chunk + 4);
		if (memcmp(chunk, "fmt ", 4) == 0 && size >= 16) {
			info->format = (S32)ReadU16(chunk + 8);
			info->channels = (S32)ReadU16(chunk + 10);
			info->rate = ReadU32(chunk + 12);
			info->block_size = ReadU16(chunk + 20);
			info->bits = (S32)ReadU16(chunk + 22);
			haveFmt = true;
		} else if (memcmp(chunk, "fact", 4) == 0 && size >= 4) {
			factSamples = ReadU32(chunk + 8);
		} else if (memcmp(chunk, "data", 4) == 0) {
			info->data_ptr = chunk + 8;
			info->data_len = size;
			break;
		}
		offset += 8 + size + (size & 1);
	}
	if (!haveFmt || !info->data_ptr || info->channels <= 0) {
		return false;
	}
	info->initial_ptr = image;
	if (info->format == WAVE_FORMAT_PCM && info->bits > 0) {
		info->samples = info->data_len / (info->channels * (info->bits / 8));
	} else {
		info->samples = factSamples;
	}
	return true;
}

// Size of the file image: everything up to the end of the data chunk.
size_t WavImageSize(const AILSOUNDINFO &info)
{
	return (const unsigned char *)info.data_ptr - (const unsigned char *)info.initial_ptr + info.data_len;
}

// ------------------------------------------------------------------------------------------------
// File access through the game's callbacks (or stdio before they are installed)
// ------------------------------------------------------------------------------------------------
struct GameFile
{
	U32 handle;
	FILE *fp;
};

bool OpenGameFile(const char *name, GameFile *f)
{
	f->handle = 0;
	f->fp = NULL;
	if (g_fileOpen) {
		return g_fileOpen(name, &f->handle) != 0 && f->handle != 0;
	}
	f->fp = fopen(name, "rb");
	return f->fp != NULL;
}

void CloseGameFile(GameFile *f)
{
	if (f->fp) {
		fclose(f->fp);
	} else if (f->handle && g_fileClose) {
		g_fileClose(f->handle);
	}
	f->fp = NULL;
	f->handle = 0;
}

ma_result DecoderRead(ma_decoder *decoder, void *buffer, size_t bytes, size_t *bytesRead)
{
	GameFile *f = (GameFile *)decoder->pUserData;
	size_t n = f->fp ? fread(buffer, 1, bytes, f->fp) : g_fileRead(f->handle, buffer, (U32)bytes);
	*bytesRead = n;
	return (n == 0 && bytes > 0) ? MA_AT_END : MA_SUCCESS;
}

ma_result DecoderSeek(ma_decoder *decoder, ma_int64 offset, ma_seek_origin origin)
{
	GameFile *f = (GameFile *)decoder->pUserData;
	U32 type = origin == ma_seek_origin_start ? AIL_FILE_SEEK_BEGIN
		: origin == ma_seek_origin_end ? AIL_FILE_SEEK_END : AIL_FILE_SEEK_CURRENT;
	if (f->fp) {
		int whence = type == AIL_FILE_SEEK_BEGIN ? SEEK_SET : type == AIL_FILE_SEEK_END ? SEEK_END : SEEK_CUR;
		return fseek(f->fp, (long)offset, whence) == 0 ? MA_SUCCESS : MA_ERROR;
	}
	return g_fileSeek(f->handle, (S32)offset, type) >= 0 ? MA_SUCCESS : MA_ERROR;
}

// ------------------------------------------------------------------------------------------------
// Data source that repeats a decoder a given number of times (stream loop counts)
// ------------------------------------------------------------------------------------------------
struct LoopingSource
{
	ma_data_source_base base;
	ma_decoder *decoder;
	std::atomic<S32> remaining;		// plays left including the current one; 0 = forever
};

ma_result LoopRead(ma_data_source *ds, void *out, ma_uint64 frameCount, ma_uint64 *framesRead)
{
	LoopingSource *src = (LoopingSource *)ds;
	ma_format format;
	ma_uint32 channels;
	ma_decoder_get_data_format(src->decoder, &format, &channels, NULL, NULL, 0);
	const ma_uint32 frameBytes = ma_get_bytes_per_frame(format, channels);

	ma_uint64 total = 0;
	int restarts = 0;
	while (total < frameCount) {
		ma_uint64 got = 0;
		ma_decoder_read_pcm_frames(src->decoder, (unsigned char *)out + total * frameBytes, frameCount - total, &got);
		total += got;
		if (total >= frameCount) {
			break;
		}
		// End of the file.
		S32 left = src->remaining.load();
		if (left == 1 || restarts > 1) {
			break;	// last play (or an empty file): stop
		}
		if (left > 1) {
			src->remaining.store(left - 1);
		}
		ma_decoder_seek_to_pcm_frame(src->decoder, 0);
		if (got == 0) {
			++restarts;
		}
	}
	*framesRead = total;
	return total == 0 ? MA_AT_END : MA_SUCCESS;
}

ma_result LoopSeek(ma_data_source *ds, ma_uint64 frame)
{
	return ma_decoder_seek_to_pcm_frame(((LoopingSource *)ds)->decoder, frame);
}

ma_result LoopFormat(ma_data_source *ds, ma_format *format, ma_uint32 *channels, ma_uint32 *rate, ma_channel *map, size_t mapCap)
{
	return ma_decoder_get_data_format(((LoopingSource *)ds)->decoder, format, channels, rate, map, mapCap);
}

ma_result LoopCursor(ma_data_source *ds, ma_uint64 *cursor)
{
	return ma_decoder_get_cursor_in_pcm_frames(((LoopingSource *)ds)->decoder, cursor);
}

ma_result LoopLength(ma_data_source *ds, ma_uint64 *length)
{
	return ma_decoder_get_length_in_pcm_frames(((LoopingSource *)ds)->decoder, length);
}

const ma_data_source_vtable g_loopingVTable = { LoopRead, LoopSeek, LoopFormat, LoopCursor, LoopLength, NULL, 0 };

// ------------------------------------------------------------------------------------------------
// Pan: Miles 0..1 (0.5 = center), miniaudio -1..1
// ------------------------------------------------------------------------------------------------
inline float ToMaPan(F32 pan) { return pan < 0.0f ? -1.0f : pan > 1.0f ? 1.0f : pan * 2.0f - 1.0f; }

} // namespace

// ------------------------------------------------------------------------------------------------
// Objects behind the handles
// ------------------------------------------------------------------------------------------------
struct ZhMssDriver { int unused; };

struct ZhMssSample
{
	ObjectKind kind;
	bool inUse;
	// playback
	ma_decoder decoder;
	bool decoderReady;
	ma_sound sound;
	bool soundReady;
	bool started;			// AIL_start_* was called for the current file
	const void *image;
	U32 fileRate;
	// parameters (kept while no sound exists, applied when one is created)
	F32 volume;
	F32 pan;
	S32 playbackRate;
	F32 position[3];
	F32 minDistance;
	F32 maxDistance;
	S32 userData[NUM_USER_DATA];
	// end of playback
	AILSAMPLECB eos;
	std::atomic<bool> ended;
};

struct ZhMssStream
{
	bool inUse;
	GameFile file;
	ma_decoder decoder;
	bool decoderReady;
	LoopingSource source;
	ma_sound sound;
	bool soundReady;
	bool started;
	U32 rate;
	F32 volume;
	F32 pan;
	AILSTREAMCB callback;
	std::atomic<bool> ended;
};

namespace {

std::vector<ZhMssSample *> g_samples;	// every sample/listener ever allocated (pooled)
std::vector<ZhMssStream *> g_streams;	// every stream ever allocated (pooled)
ZhMssSample *g_listener = NULL;

void OnSampleEnd(void *user, ma_sound *)
{
	// Real-time audio thread: just flag it.
	((ZhMssSample *)user)->ended.store(true);
	SetEvent(g_wake);
}

void OnStreamEnd(void *user, ma_sound *)
{
	((ZhMssStream *)user)->ended.store(true);
	SetEvent(g_wake);
}

// --- samples -------------------------------------------------------------------------------------
void ApplySampleParams(ZhMssSample *s)
{
	if (!s->soundReady) {
		return;
	}
	ma_sound_set_volume(&s->sound, s->volume);
	if (s->fileRate > 0 && s->playbackRate > 0) {
		ma_sound_set_pitch(&s->sound, (float)s->playbackRate / (float)s->fileRate);
	}
	if (s->kind == KIND_SAMPLE_2D) {
		ma_sound_set_pan(&s->sound, ToMaPan(s->pan));
	} else {
		ma_sound_set_position(&s->sound, s->position[0], s->position[1], s->position[2]);
		ma_sound_set_min_distance(&s->sound, s->minDistance);
		ma_sound_set_max_distance(&s->sound, s->maxDistance);
	}
}

void DestroySampleSound(ZhMssSample *s)
{
	if (s->soundReady) {
		ma_sound_uninit(&s->sound);
		s->soundReady = false;
	}
	if (s->decoderReady) {
		ma_decoder_uninit(&s->decoder);
		s->decoderReady = false;
	}
	s->started = false;
	s->ended.store(false);
}

void ResetSample(ZhMssSample *s)
{
	DestroySampleSound(s);
	s->image = NULL;
	s->fileRate = 0;
	s->volume = 1.0f;
	s->pan = 0.5f;
	s->playbackRate = DEFAULT_PLAYBACK_RATE;
}

// Creates the sound for a .wav image; keeps volume/pan, takes the playback rate from the file.
S32 LoadSampleImage(ZhMssSample *s, const void *image)
{
	DestroySampleSound(s);
	s->image = NULL;
	AILSOUNDINFO info;
	if (!g_engineReady || !ParseWav(image, &info)) {
		return 0;
	}
	ma_decoder_config dc = ma_decoder_config_init(ma_format_f32, 0, 0);
	if (ma_decoder_init_memory(image, WavImageSize(info), &dc, &s->decoder) != MA_SUCCESS) {
		return 0;
	}
	s->decoderReady = true;

	ma_sound_config sc = ma_sound_config_init_2(&g_engine);
	sc.pDataSource = &s->decoder;
	sc.flags = 0;
	if (s->kind == KIND_SAMPLE_2D) {
		sc.flags |= MA_SOUND_FLAG_NO_SPATIALIZATION;
	}
	sc.endCallback = OnSampleEnd;
	sc.pEndCallbackUserData = s;
	if (ma_sound_init_ex(&g_engine, &sc, &s->sound) != MA_SUCCESS) {
		ma_decoder_uninit(&s->decoder);
		s->decoderReady = false;
		return 0;
	}
	s->soundReady = true;
	if (s->kind == KIND_SAMPLE_3D) {
		ma_sound_set_attenuation_model(&s->sound, ma_attenuation_model_inverse);
		ma_sound_set_rolloff(&s->sound, 1.0f);
		ma_sound_set_doppler_factor(&s->sound, 0.0f);
		ma_sound_set_positioning(&s->sound, ma_positioning_absolute);
	}
	s->image = image;
	s->fileRate = info.rate;
	s->playbackRate = (S32)info.rate;
	ApplySampleParams(s);
	return 1;
}

ZhMssSample *AllocateSample(ObjectKind kind)
{
	ZhMssSample *s = NULL;
	for (size_t i = 0; i < g_samples.size(); ++i) {
		if (!g_samples[i]->inUse) {
			s = g_samples[i];
			break;
		}
	}
	if (!s) {
		s = new ZhMssSample;
		s->decoderReady = false;
		s->soundReady = false;
		g_samples.push_back(s);
	}
	s->kind = kind;
	s->inUse = true;
	s->eos = NULL;
	s->position[0] = s->position[1] = s->position[2] = 0.0f;
	s->minDistance = 1.0f;
	s->maxDistance = 1000000.0f;
	memset(s->userData, 0, sizeof(s->userData));
	ResetSample(s);
	return s;
}

void ReleaseSample(ZhMssSample *s)
{
	if (!s) {
		return;
	}
	ResetSample(s);
	s->eos = NULL;
	s->inUse = false;
}

// --- streams -------------------------------------------------------------------------------------
void DestroyStream(ZhMssStream *st)
{
	if (st->soundReady) {
		ma_sound_uninit(&st->sound);
		st->soundReady = false;
	}
	if (st->decoderReady) {
		ma_data_source_uninit(&st->source);
		ma_decoder_uninit(&st->decoder);
		st->decoderReady = false;
	}
	CloseGameFile(&st->file);
	st->callback = NULL;
	st->started = false;
	st->ended.store(false);
	st->inUse = false;
}

ZhMssStream *OpenStream(const char *filename)
{
	if (!g_engineReady || !filename) {
		return NULL;
	}
	ZhMssStream *st = NULL;
	for (size_t i = 0; i < g_streams.size(); ++i) {
		if (!g_streams[i]->inUse) {
			st = g_streams[i];
			break;
		}
	}
	if (!st) {
		st = new ZhMssStream;
		st->decoderReady = false;
		st->soundReady = false;
		st->file.fp = NULL;
		st->file.handle = 0;
		g_streams.push_back(st);
	}
	st->inUse = true;
	st->callback = NULL;
	st->started = false;
	st->ended.store(false);
	st->volume = 1.0f;
	st->pan = 0.5f;

	if (!OpenGameFile(filename, &st->file)) {
		st->inUse = false;
		return NULL;
	}
	ma_decoder_config dc = ma_decoder_config_init(ma_format_f32, 0, 0);
	if (ma_decoder_init(DecoderRead, DecoderSeek, &st->file, &dc, &st->decoder) != MA_SUCCESS) {
		CloseGameFile(&st->file);
		st->inUse = false;
		return NULL;
	}
	st->decoderReady = true;
	ma_decoder_get_data_format(&st->decoder, NULL, NULL, &st->rate, NULL, 0);

	ma_data_source_config dsc = ma_data_source_config_init();
	dsc.vtable = &g_loopingVTable;
	ma_data_source_init(&dsc, &st->source.base);
	st->source.decoder = &st->decoder;
	st->source.remaining.store(1);

	ma_sound_config sc = ma_sound_config_init_2(&g_engine);
	sc.pDataSource = &st->source;
	sc.flags = MA_SOUND_FLAG_NO_SPATIALIZATION;
	sc.endCallback = OnStreamEnd;
	sc.pEndCallbackUserData = st;
	if (ma_sound_init_ex(&g_engine, &sc, &st->sound) != MA_SUCCESS) {
		DestroyStream(st);
		return NULL;
	}
	st->soundReady = true;
	return st;
}

// --- end-of-playback dispatch ----------------------------------------------------------------------
void ServiceThread()
{
	std::vector<std::pair<ZhMssSample *, AILSAMPLECB> > samples;
	std::vector<std::pair<ZhMssStream *, AILSTREAMCB> > streams;
	while (!g_quit.load()) {
		WaitForSingleObject(g_wake, 50);
		samples.clear();
		streams.clear();
		{
			std::lock_guard<std::recursive_mutex> guard(g_lock);
			for (size_t i = 0; i < g_samples.size(); ++i) {
				ZhMssSample *s = g_samples[i];
				if (s->ended.exchange(false) && s->inUse && s->started) {
					s->started = false;
					if (s->eos) {
						samples.push_back(std::make_pair(s, s->eos));
					}
				}
			}
			for (size_t i = 0; i < g_streams.size(); ++i) {
				ZhMssStream *st = g_streams[i];
				if (st->ended.exchange(false) && st->inUse && st->started) {
					st->started = false;
					if (st->callback) {
						streams.push_back(std::make_pair(st, st->callback));
					}
				}
			}
		}
		// Call the game without holding the lock: it starts the next sound from here.
		for (size_t i = 0; i < samples.size(); ++i) {
			samples[i].second(samples[i].first);
		}
		for (size_t i = 0; i < streams.size(); ++i) {
			streams[i].second(streams[i].first);
		}
	}
}

#define LOCK std::lock_guard<std::recursive_mutex> guard(g_lock)

} // namespace

extern "C" {

// ================================================================================================
// System
// ================================================================================================
char *AILEXPORT AIL_set_redist_directory(char const *) { return (char *)""; }
S32 AILEXPORT AIL_startup(void) { return 1; }

void AILEXPORT AIL_shutdown(void)
{
	if (g_serviceThread.joinable()) {
		g_quit.store(true);
		SetEvent(g_wake);
		g_serviceThread.join();
	}
	LOCK;
	for (size_t i = 0; i < g_streams.size(); ++i) {
		if (g_streams[i]->inUse) {
			DestroyStream(g_streams[i]);
		}
		delete g_streams[i];
	}
	g_streams.clear();
	for (size_t i = 0; i < g_samples.size(); ++i) {
		DestroySampleSound(g_samples[i]);
		delete g_samples[i];
	}
	g_samples.clear();
	g_listener = NULL;
	if (g_engineReady) {
		ma_engine_uninit(&g_engine);
		g_engineReady = false;
	}
	delete g_driver;
	g_driver = NULL;
	if (g_wake) {
		CloseHandle(g_wake);
		g_wake = NULL;
	}
}

void AILEXPORT AIL_MSS_version(char *buffer, U32 size)
{
	if (buffer && size) {
		strncpy(buffer, g_versionString, size - 1);
		buffer[size - 1] = 0;
	}
}

U32 AILEXPORT AIL_get_timer_highest_delay(void) { return 0; }

void AILEXPORT AIL_set_file_callbacks(AIL_file_open_callback opencb, AIL_file_close_callback closecb,
	AIL_file_seek_callback seekcb, AIL_file_read_callback readcb)
{
	LOCK;
	if (opencb && closecb && seekcb && readcb) {
		g_fileOpen = opencb;
		g_fileClose = closecb;
		g_fileSeek = seekcb;
		g_fileRead = readcb;
	} else {
		g_fileOpen = NULL;
		g_fileClose = NULL;
		g_fileSeek = NULL;
		g_fileRead = NULL;
	}
}

void AILEXPORT AIL_mem_free_lock(void *ptr) { free(ptr); }

S32 AILEXPORT AIL_get_DirectSound_info(HSAMPLE, AILLPDIRECTSOUND *lplpDS, AILLPDIRECTSOUNDBUFFER *lplpDSB)
{
	// No DirectSound object: the game then assumes stereo speakers.
	if (lplpDS) *lplpDS = NULL;
	if (lplpDSB) *lplpDSB = NULL;
	return 0;
}

// ================================================================================================
// Quick API
// ================================================================================================
S32 AILEXPORT AIL_quick_startup(S32 use_digital, S32, U32 output_rate, S32, S32 output_channels)
{
	LOCK;
	if (g_engineReady) {
		return 1;
	}
	if (!use_digital) {
		return 0;
	}
	ma_engine_config config = ma_engine_config_init();
	config.listenerCount = 1;
	config.channels = output_channels > 0 ? (ma_uint32)output_channels : 2;
	config.sampleRate = output_rate;
	if (ma_engine_init(&config, &g_engine) != MA_SUCCESS) {
		return 0;	// no audio device: the game turns sound off
	}
	g_engineReady = true;
	// The game's world is right-handed with Z up.
	ma_engine_listener_set_world_up(&g_engine, 0, 0.0f, 0.0f, 1.0f);
	g_driver = new ZhMssDriver;
	g_quit.store(false);
	g_wake = CreateEvent(NULL, FALSE, FALSE, NULL);
	g_serviceThread = std::thread(ServiceThread);
	return 1;
}

void AILEXPORT AIL_quick_handles(HDIGDRIVER *pdig, HMDIDRIVER *pmdi, HDLSDEVICE *pdls)
{
	if (pdig) *pdig = g_driver;
	if (pmdi) *pmdi = NULL;
	if (pdls) *pdls = NULL;
}

HAUDIO AILEXPORT AIL_quick_load_and_play(char const *filename, U32 loop_count, S32)
{
	LOCK;
	ZhMssStream *st = OpenStream(filename);
	if (st) {
		st->source.remaining.store((S32)loop_count);
		st->started = true;
		ma_sound_start(&st->sound);
	}
	return st;
}

void AILEXPORT AIL_quick_set_volume(HAUDIO audio, F32 volume, F32 pan)
{
	LOCK;
	if (audio && audio->inUse) {
		audio->volume = volume;
		audio->pan = pan;
		ma_sound_set_volume(&audio->sound, volume);
		ma_sound_set_pan(&audio->sound, ToMaPan(pan));
	}
}

void AILEXPORT AIL_quick_unload(HAUDIO audio)
{
	LOCK;
	if (audio && audio->inUse) {
		DestroyStream(audio);
	}
}

// ================================================================================================
// .wav helpers
// ================================================================================================
S32 AILEXPORT AIL_WAV_info(void const *data, AILSOUNDINFO *info)
{
	return ParseWav(data, info) ? 1 : 0;
}

S32 AILEXPORT AIL_decompress_ADPCM(AILSOUNDINFO const *info, void **outdata, U32 *outsize)
{
	*outdata = NULL;
	*outsize = 0;
	ma_decoder_config dc = ma_decoder_config_init(ma_format_s16, 0, 0);
	ma_decoder decoder;
	if (ma_decoder_init_memory(info->initial_ptr, WavImageSize(*info), &dc, &decoder) != MA_SUCCESS) {
		return 0;
	}
	ma_uint32 channels = 0, rate = 0;
	ma_decoder_get_data_format(&decoder, NULL, &channels, &rate, NULL, 0);

	// Decode everything into a growing PCM buffer after a 44 byte header.
	const size_t header = 44;
	const size_t frameBytes = channels * 2;
	size_t capacity = header + (info->samples ? info->samples : rate) * frameBytes;
	unsigned char *buf = (unsigned char *)malloc(capacity);
	size_t used = header;
	for (;;) {
		if (capacity - used < 4096 * frameBytes) {
			capacity *= 2;
			buf = (unsigned char *)realloc(buf, capacity);
		}
		ma_uint64 got = 0;
		ma_decoder_read_pcm_frames(&decoder, buf + used, (capacity - used) / frameBytes, &got);
		if (got == 0) {
			break;
		}
		used += (size_t)got * frameBytes;
	}
	ma_decoder_uninit(&decoder);

	const U32 dataLen = (U32)(used - header);
	unsigned char *h = buf;
	memcpy(h, "RIFF", 4);
	U32 v = 36 + dataLen;				memcpy(h + 4, &v, 4);
	memcpy(h + 8, "WAVEfmt ", 8);
	v = 16;								memcpy(h + 16, &v, 4);
	unsigned short s = WAVE_FORMAT_PCM;	memcpy(h + 20, &s, 2);
	s = (unsigned short)channels;		memcpy(h + 22, &s, 2);
	v = rate;							memcpy(h + 24, &v, 4);
	v = rate * (U32)frameBytes;			memcpy(h + 28, &v, 4);
	s = (unsigned short)frameBytes;		memcpy(h + 32, &s, 2);
	s = 16;								memcpy(h + 34, &s, 2);
	memcpy(h + 36, "data", 4);
	memcpy(h + 40, &dataLen, 4);

	*outdata = buf;
	*outsize = (U32)used;
	return 1;
}

// ================================================================================================
// 2D samples
// ================================================================================================
HSAMPLE AILEXPORT AIL_allocate_sample_handle(HDIGDRIVER)
{
	LOCK;
	return g_engineReady ? AllocateSample(KIND_SAMPLE_2D) : NULL;
}

void AILEXPORT AIL_release_sample_handle(HSAMPLE sample) { LOCK; ReleaseSample(sample); }

void AILEXPORT AIL_init_sample(HSAMPLE sample)
{
	LOCK;
	if (sample) {
		ResetSample(sample);
	}
}

S32 AILEXPORT AIL_set_sample_file(HSAMPLE sample, void const *file_image, S32)
{
	LOCK;
	return sample ? LoadSampleImage(sample, file_image) : 0;
}

void AILEXPORT AIL_start_sample(HSAMPLE sample)
{
	LOCK;
	if (sample && sample->soundReady) {
		ma_sound_seek_to_pcm_frame(&sample->sound, 0);
		sample->ended.store(false);
		sample->started = true;
		ma_sound_start(&sample->sound);
	}
}

void AILEXPORT AIL_stop_sample(HSAMPLE sample)
{
	LOCK;
	if (sample && sample->soundReady) {
		ma_sound_stop(&sample->sound);
	}
}

void AILEXPORT AIL_resume_sample(HSAMPLE sample)
{
	LOCK;
	if (sample && sample->soundReady && sample->started && !ma_sound_at_end(&sample->sound)) {
		ma_sound_start(&sample->sound);
	}
}

void AILEXPORT AIL_set_sample_volume_pan(HSAMPLE sample, F32 volume, F32 pan)
{
	LOCK;
	if (sample) {
		sample->volume = volume;
		sample->pan = pan;
		ApplySampleParams(sample);
	}
}

void AILEXPORT AIL_sample_volume_pan(HSAMPLE sample, F32 *volume, F32 *pan)
{
	LOCK;
	if (volume) *volume = sample ? sample->volume : 0.0f;
	if (pan) *pan = sample ? sample->pan : 0.5f;
}

void AILEXPORT AIL_set_sample_playback_rate(HSAMPLE sample, S32 rate)
{
	LOCK;
	if (sample) {
		sample->playbackRate = rate;
		ApplySampleParams(sample);
	}
}

S32 AILEXPORT AIL_sample_playback_rate(HSAMPLE sample)
{
	LOCK;
	return sample ? sample->playbackRate : 0;
}

void AILEXPORT AIL_set_sample_user_data(HSAMPLE sample, U32 index, S32 value)
{
	LOCK;
	if (sample && index < NUM_USER_DATA) {
		sample->userData[index] = value;
	}
}

S32 AILEXPORT AIL_sample_user_data(HSAMPLE sample, U32 index)
{
	LOCK;
	return (sample && index < NUM_USER_DATA) ? sample->userData[index] : 0;
}

AILSAMPLECB AILEXPORT AIL_register_EOS_callback(HSAMPLE sample, AILSAMPLECB callback)
{
	LOCK;
	if (!sample) {
		return NULL;
	}
	AILSAMPLECB old = sample->eos;
	sample->eos = callback;
	return old;
}

// No effect filters are provided (the game only uses a delay filter when one exists).
HPROVIDER AILEXPORT AIL_set_sample_processor(HSAMPLE, SAMPLESTAGE, HPROVIDER) { return 0; }
S32 AILEXPORT AIL_set_filter_sample_preference(HSAMPLE, char const *, void const *) { return 0; }
S32 AILEXPORT AIL_enumerate_filters(HPROENUM *, HPROVIDER *, char **) { return 0; }

// ================================================================================================
// 3D samples
// ================================================================================================
// The game looks providers up by name; both names map to the same positional mixer.
static const char *const g_providerNames[] = { "Miles Fast 2D Positional Audio", "Dolby Surround" };

S32 AILEXPORT AIL_enumerate_3D_providers(HPROENUM *next, HPROVIDER *dest, char **name)
{
	if (!next || *next >= sizeof(g_providerNames) / sizeof(g_providerNames[0])) {
		return 0;
	}
	if (dest) *dest = *next + 1;
	if (name) *name = (char *)g_providerNames[*next];
	++*next;
	return 1;
}

M3DRESULT AILEXPORT AIL_open_3D_provider(HPROVIDER provider) { return (g_engineReady && provider) ? M3D_NOERR : 1; }
void AILEXPORT AIL_close_3D_provider(HPROVIDER) {}
void AILEXPORT AIL_set_3D_speaker_type(HPROVIDER, S32) {}

H3DPOBJECT AILEXPORT AIL_open_3D_listener(HPROVIDER)
{
	LOCK;
	if (!g_engineReady) {
		return NULL;
	}
	if (!g_listener) {
		g_listener = AllocateSample(KIND_LISTENER);
	}
	return g_listener;
}

void AILEXPORT AIL_close_3D_listener(H3DPOBJECT listener)
{
	LOCK;
	if (listener && listener == g_listener) {
		ReleaseSample(g_listener);
		g_listener = NULL;
	}
}

H3DSAMPLE AILEXPORT AIL_allocate_3D_sample_handle(HPROVIDER)
{
	LOCK;
	return g_engineReady ? AllocateSample(KIND_SAMPLE_3D) : NULL;
}

void AILEXPORT AIL_release_3D_sample_handle(H3DSAMPLE sample) { LOCK; ReleaseSample(sample); }

S32 AILEXPORT AIL_set_3D_sample_file(H3DSAMPLE sample, void const *file_image)
{
	LOCK;
	if (!sample) {
		return 0;
	}
	sample->volume = 1.0f;
	return LoadSampleImage(sample, file_image);
}

void AILEXPORT AIL_start_3D_sample(H3DSAMPLE sample) { AIL_start_sample(sample); }
void AILEXPORT AIL_stop_3D_sample(H3DSAMPLE sample) { AIL_stop_sample(sample); }
void AILEXPORT AIL_resume_3D_sample(H3DSAMPLE sample) { AIL_resume_sample(sample); }

void AILEXPORT AIL_set_3D_sample_volume(H3DSAMPLE sample, F32 volume)
{
	LOCK;
	if (sample) {
		sample->volume = volume;
		ApplySampleParams(sample);
	}
}

void AILEXPORT AIL_set_3D_sample_playback_rate(H3DSAMPLE sample, S32 rate) { AIL_set_sample_playback_rate(sample, rate); }
S32 AILEXPORT AIL_3D_sample_playback_rate(H3DSAMPLE sample) { return AIL_sample_playback_rate(sample); }

// Occlusion (a low-pass filter for off-screen sounds) is not implemented.
void AILEXPORT AIL_set_3D_sample_occlusion(H3DSAMPLE, F32) {}

void AILEXPORT AIL_set_3D_sample_distances(H3DSAMPLE sample, F32 dist1, F32 dist2)
{
	LOCK;
	if (sample) {
		F32 lo = dist1 < dist2 ? dist1 : dist2;
		F32 hi = dist1 < dist2 ? dist2 : dist1;
		sample->minDistance = lo > 0.0f ? lo : 1.0f;
		sample->maxDistance = hi > sample->minDistance ? hi : sample->minDistance;
		ApplySampleParams(sample);
	}
}

void AILEXPORT AIL_set_3D_position(H3DPOBJECT obj, F32 x, F32 y, F32 z)
{
	LOCK;
	if (!obj) {
		return;
	}
	obj->position[0] = x;
	obj->position[1] = y;
	obj->position[2] = z;
	if (obj->kind == KIND_LISTENER) {
		if (g_engineReady) {
			ma_engine_listener_set_position(&g_engine, 0, x, y, z);
		}
	} else {
		ApplySampleParams(obj);
	}
}

void AILEXPORT AIL_set_3D_orientation(H3DPOBJECT obj, F32 x_face, F32 y_face, F32 z_face, F32, F32, F32)
{
	// Miles uses a left-handed system, so the game passes "up" as (0,0,-1).  miniaudio is
	// right-handed and is told at startup that +Z is up; only the facing direction is needed.
	LOCK;
	if (obj && obj->kind == KIND_LISTENER && g_engineReady) {
		ma_engine_listener_set_direction(&g_engine, 0, x_face, y_face, z_face);
	}
}

void AILEXPORT AIL_set_3D_user_data(H3DPOBJECT obj, U32 index, S32 value) { AIL_set_sample_user_data(obj, index, value); }
S32 AILEXPORT AIL_3D_user_data(H3DPOBJECT obj, U32 index) { return AIL_sample_user_data(obj, index); }

AIL3DSAMPLECB AILEXPORT AIL_register_3D_EOS_callback(H3DSAMPLE sample, AIL3DSAMPLECB callback)
{
	return AIL_register_EOS_callback(sample, callback);
}

// ================================================================================================
// Streams
// ================================================================================================
HSTREAM AILEXPORT AIL_open_stream(HDIGDRIVER, char const *filename, S32)
{
	LOCK;
	return OpenStream(filename);
}

void AILEXPORT AIL_close_stream(HSTREAM stream)
{
	LOCK;
	if (stream && stream->inUse) {
		DestroyStream(stream);
	}
}

void AILEXPORT AIL_start_stream(HSTREAM stream)
{
	LOCK;
	if (stream && stream->soundReady) {
		ma_sound_seek_to_pcm_frame(&stream->sound, 0);
		stream->ended.store(false);
		stream->started = true;
		ma_sound_start(&stream->sound);
	}
}

void AILEXPORT AIL_pause_stream(HSTREAM stream, S32 onoff)
{
	LOCK;
	if (!stream || !stream->soundReady) {
		return;
	}
	if (onoff) {
		ma_sound_stop(&stream->sound);
	} else if (stream->started && !ma_sound_at_end(&stream->sound)) {
		ma_sound_start(&stream->sound);
	}
}

void AILEXPORT AIL_set_stream_volume_pan(HSTREAM stream, F32 volume, F32 pan)
{
	LOCK;
	if (stream && stream->soundReady) {
		stream->volume = volume;
		stream->pan = pan;
		ma_sound_set_volume(&stream->sound, volume);
		ma_sound_set_pan(&stream->sound, ToMaPan(pan));
	}
}

void AILEXPORT AIL_stream_volume_pan(HSTREAM stream, F32 *volume, F32 *pan)
{
	LOCK;
	if (volume) *volume = stream ? stream->volume : 0.0f;
	if (pan) *pan = stream ? stream->pan : 0.5f;
}

void AILEXPORT AIL_set_stream_loop_count(HSTREAM stream, S32 count)
{
	LOCK;
	if (stream && stream->inUse) {
		stream->source.remaining.store(count < 0 ? 0 : count);
	}
}

S32 AILEXPORT AIL_stream_loop_count(HSTREAM stream)
{
	LOCK;
	return (stream && stream->inUse) ? stream->source.remaining.load() : 0;
}

AILSTREAMCB AILEXPORT AIL_register_stream_callback(HSTREAM stream, AILSTREAMCB callback)
{
	LOCK;
	if (!stream) {
		return NULL;
	}
	AILSTREAMCB old = stream->callback;
	stream->callback = callback;
	return old;
}

void AILEXPORT AIL_stream_ms_position(HSTREAM stream, long *total_milliseconds, long *current_milliseconds)
{
	LOCK;
	long total = 0, current = 0;
	if (stream && stream->decoderReady && stream->rate) {
		ma_uint64 frames = 0;
		if (ma_decoder_get_length_in_pcm_frames(&stream->decoder, &frames) == MA_SUCCESS) {
			total = (long)(frames * 1000 / stream->rate);
		}
		ma_uint64 cursor = 0;
		if (ma_decoder_get_cursor_in_pcm_frames(&stream->decoder, &cursor) == MA_SUCCESS) {
			current = (long)(cursor * 1000 / stream->rate);
		}
	}
	if (total_milliseconds) *total_milliseconds = total;
	if (current_milliseconds) *current_milliseconds = current;
}

} // extern "C"
