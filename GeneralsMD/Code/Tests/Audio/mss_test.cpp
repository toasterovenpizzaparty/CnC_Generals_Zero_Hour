/*
**	Command & Conquer Generals Zero Hour(tm) - modified version.
**	Licensed under the GNU General Public License v3 or later (see LICENSE.md).
*/
// Tests for the miniaudio implementation of the Miles API (Compat/MSS).  Drives it the way
// MilesAudioManager does.  Works without sound hardware (miniaudio falls back to a null device that
// still plays in real time), so it runs in CI and under Wine.
//
//   i686-w64-mingw32-g++-posix -O2 -static -I../../Compat -I../../Libraries/Source/miniaudio \
//       mss_test.cpp ../../Compat/MSS/mss_miniaudio.cpp -lole32 -lwinmm -o mss_test.exe

#include "MSS/MSS.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <atomic>
#include <string>
#include <vector>

static int g_failures = 0;
#define CHECK(cond, what) do { if (cond) printf("  ok   %s\n", what); else { printf("  FAIL %s\n", what); ++g_failures; } } while (0)

// ------------------------------------------------------------------------------------------------
// Test data
// ------------------------------------------------------------------------------------------------
static void Put16(std::vector<unsigned char> &v, unsigned x) { v.push_back(x & 255); v.push_back((x >> 8) & 255); }
static void Put32(std::vector<unsigned char> &v, unsigned x) { Put16(v, x & 0xffff); Put16(v, x >> 16); }
static void PutTag(std::vector<unsigned char> &v, const char *t) { v.insert(v.end(), t, t + 4); }

static std::vector<short> Tone(int frames, int rate, double hz)
{
	std::vector<short> s(frames);
	for (int i = 0; i < frames; ++i) {
		s[i] = (short)(12000.0 * sin(2.0 * 3.14159265358979 * hz * i / rate));
	}
	return s;
}

static std::vector<unsigned char> PcmWav(const std::vector<short> &pcm, int rate)
{
	std::vector<unsigned char> v;
	PutTag(v, "RIFF"); Put32(v, 36 + (unsigned)pcm.size() * 2); PutTag(v, "WAVE");
	PutTag(v, "fmt "); Put32(v, 16); Put16(v, WAVE_FORMAT_PCM); Put16(v, 1); Put32(v, rate);
	Put32(v, rate * 2); Put16(v, 2); Put16(v, 16);
	PutTag(v, "data"); Put32(v, (unsigned)pcm.size() * 2);
	for (size_t i = 0; i < pcm.size(); ++i) Put16(v, (unsigned short)pcm[i]);
	return v;
}

// Mono IMA ADPCM encoder (Microsoft block layout), as used by the game's sound files.
static const int kStep[89] = { 7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,
	118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,
	1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,
	12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767 };
static const int kIndex[16] = { -1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8 };

static std::vector<unsigned char> AdpcmWav(const std::vector<short> &pcm, int rate)
{
	const int blockAlign = 256, perBlock = (blockAlign - 4) * 2 + 1;
	std::vector<unsigned char> data;
	int index = 0;
	for (size_t start = 0; start < pcm.size(); start += perBlock) {
		int pred = pcm[start];
		Put16(data, (unsigned short)pred); data.push_back((unsigned char)index); data.push_back(0);
		unsigned char byte = 0;
		for (int n = 1; n < perBlock; ++n) {
			int sample = start + n < pcm.size() ? pcm[start + n] : 0;
			int step = kStep[index], diff = sample - pred, code = 0;
			if (diff < 0) { code = 8; diff = -diff; }
			int delta = step >> 3;
			if (diff >= step) { code |= 4; diff -= step; delta += step; }
			step >>= 1; if (diff >= step) { code |= 2; diff -= step; delta += step; }
			step >>= 1; if (diff >= step) { code |= 1; delta += step; }
			pred += (code & 8) ? -delta : delta;
			pred = pred < -32768 ? -32768 : pred > 32767 ? 32767 : pred;
			index += kIndex[code]; index = index < 0 ? 0 : index > 88 ? 88 : index;
			if (n & 1) byte = (unsigned char)code; else data.push_back(byte | (unsigned char)(code << 4));
		}
		if (!(perBlock & 1)) data.push_back(byte);
	}
	std::vector<unsigned char> v;
	PutTag(v, "RIFF"); Put32(v, 4 + 28 + 12 + 8 + (unsigned)data.size()); PutTag(v, "WAVE");
	PutTag(v, "fmt "); Put32(v, 20); Put16(v, WAVE_FORMAT_IMA_ADPCM); Put16(v, 1); Put32(v, rate);
	Put32(v, rate * blockAlign / perBlock); Put16(v, blockAlign); Put16(v, 4); Put16(v, 2); Put16(v, perBlock);
	PutTag(v, "fact"); Put32(v, 4); Put32(v, (unsigned)pcm.size());
	PutTag(v, "data"); Put32(v, (unsigned)data.size());
	v.insert(v.end(), data.begin(), data.end());
	return v;
}

// ------------------------------------------------------------------------------------------------
// Game-style file callbacks over an in-memory "file system"
// ------------------------------------------------------------------------------------------------
struct MemFile { const std::vector<unsigned char> *data; long pos; };
static std::vector<unsigned char> g_streamFile;
static std::atomic<int> g_openFiles(0);

static U32 AILCALLBACK FileOpen(char const *name, U32 *handle)
{
	if (strcmp(name, "Data\\Audio\\Tracks\\music.wav") != 0) { *handle = 0; return 0; }
	MemFile *f = new MemFile; f->data = &g_streamFile; f->pos = 0;
	*handle = (U32)(size_t)f; ++g_openFiles;
	return 1;
}
static void AILCALLBACK FileClose(U32 handle) { delete (MemFile *)(size_t)handle; --g_openFiles; }
static S32 AILCALLBACK FileSeek(U32 handle, S32 offset, U32 type)
{
	MemFile *f = (MemFile *)(size_t)handle;
	long size = (long)f->data->size();
	long pos = type == AIL_FILE_SEEK_BEGIN ? offset : type == AIL_FILE_SEEK_END ? size + offset : f->pos + offset;
	if (pos < 0 || pos > size) return -1;
	return f->pos = pos;
}
static U32 AILCALLBACK FileRead(U32 handle, void *buf, U32 bytes)
{
	MemFile *f = (MemFile *)(size_t)handle;
	long n = (long)f->data->size() - f->pos;
	if ((long)bytes < n) n = bytes;
	memcpy(buf, &(*f->data)[f->pos], n);
	f->pos += n;
	return (U32)n;
}

// ------------------------------------------------------------------------------------------------
// End-of-playback callbacks
// ------------------------------------------------------------------------------------------------
static std::atomic<int> g_sampleDone(0), g_sample3DDone(0), g_streamDone(0);
static std::atomic<DWORD> g_callbackThread(0);
static HSAMPLE g_restartSample = NULL;
static std::vector<unsigned char> *g_restartImage = NULL;
static std::atomic<int> g_restarts(0);

static void AILCALLBACK SampleDone(HSAMPLE s)
{
	g_callbackThread = GetCurrentThreadId();
	++g_sampleDone;
	// MilesAudioManager restarts looping sounds from inside this callback.
	if (s == g_restartSample && g_restarts < 2) {
		++g_restarts;
		AIL_init_sample(s);
		AIL_register_EOS_callback(s, SampleDone);
		AIL_set_sample_file(s, &(*g_restartImage)[0], 0);
		AIL_start_sample(s);
	}
}
static void AILCALLBACK Sample3DDone(H3DSAMPLE) { ++g_sample3DDone; }
static void AILCALLBACK StreamDone(HSTREAM) { ++g_streamDone; }

static bool WaitFor(std::atomic<int> &counter, int value, int timeoutMs)
{
	for (int t = 0; t < timeoutMs && counter.load() < value; t += 10) Sleep(10);
	return counter.load() >= value;
}

int main()
{
	const int rate = 22050;
	printf("Miles API on miniaudio\n");

	// --- startup, the same sequence as MilesAudioManager::openDevice ---
	AIL_set_redist_directory("MSS\\");
	AIL_startup();
	CHECK(AIL_quick_startup(1, 0, 44100, 16, 2) == 1, "AIL_quick_startup");
	HDIGDRIVER dig = NULL;
	AIL_quick_handles(&dig, NULL, NULL);
	CHECK(dig != NULL, "digital driver handle");
	AIL_set_file_callbacks(FileOpen, FileClose, FileSeek, FileRead);

	HPROENUM next = HPROENUM_FIRST;
	HPROVIDER provider = 0, fast2D = 0;
	char *name = NULL;
	int providers = 0;
	while (AIL_enumerate_3D_providers(&next, &provider, &name)) {
		++providers;
		if (strcmp(name, "Miles Fast 2D Positional Audio") == 0) fast2D = provider;
	}
	CHECK(providers >= 1 && fast2D != 0, "3D provider 'Miles Fast 2D Positional Audio'");
	CHECK(AIL_open_3D_provider(fast2D) == M3D_NOERR, "AIL_open_3D_provider");
	H3DPOBJECT listener = AIL_open_3D_listener(fast2D);
	CHECK(listener != NULL, "listener");
	AIL_set_3D_orientation(listener, 0, 1, 0, 0, 0, -1);
	AIL_set_3D_position(listener, 0, 0, 0);

	// --- .wav parsing and ADPCM decompression (AudioFileCache::openFile) ---
	std::vector<short> tone = Tone(rate / 5, rate, 440.0);		// 0.2 s
	std::vector<unsigned char> pcmWav = PcmWav(tone, rate);
	std::vector<unsigned char> adpcmWav = AdpcmWav(tone, rate);
	AILSOUNDINFO info;
	CHECK(AIL_WAV_info(&pcmWav[0], &info) && info.format == WAVE_FORMAT_PCM && info.channels == 1 &&
		info.rate == (U32)rate && info.samples == tone.size(), "AIL_WAV_info on PCM");
	CHECK(AIL_WAV_info(&adpcmWav[0], &info) && info.format == WAVE_FORMAT_IMA_ADPCM && info.samples == tone.size(),
		"AIL_WAV_info on IMA ADPCM");
	void *decoded = NULL;
	U32 decodedSize = 0;
	CHECK(AIL_decompress_ADPCM(&info, &decoded, &decodedSize) == 1, "AIL_decompress_ADPCM");
	AILSOUNDINFO dinfo;
	bool parsed = decoded && AIL_WAV_info(decoded, &dinfo) && dinfo.format == WAVE_FORMAT_PCM && dinfo.bits == 16;
	CHECK(parsed && dinfo.samples >= tone.size() && dinfo.samples < tone.size() + 512, "decompressed image is 16-bit PCM of the right length");
	if (parsed) {
		const short *out = (const short *)dinfo.data_ptr;
		double err = 0, ref = 0;
		for (size_t i = 0; i < tone.size(); ++i) { err += fabs((double)out[i] - tone[i]); ref += fabs((double)tone[i]); }
		printf("       ADPCM round trip: mean error %.2f%% of signal\n", 100.0 * err / ref);
		CHECK(err / ref < 0.05, "ADPCM decodes back to the original tone");
	}

	// --- 2D sample, as MilesAudioManager::playSample ---
	HSAMPLE s2d = AIL_allocate_sample_handle(dig);
	CHECK(s2d != NULL, "AIL_allocate_sample_handle");
	AIL_init_sample(s2d);
	AIL_set_sample_user_data(s2d, 0, 7);
	CHECK(AIL_sample_user_data(s2d, 0) == 7, "sample user data");
	AIL_register_EOS_callback(s2d, SampleDone);
	AIL_set_sample_volume_pan(s2d, 0.8f, 0.5f);
	CHECK(AIL_set_sample_file(s2d, decoded, 0) == 1, "AIL_set_sample_file (decompressed ADPCM)");
	CHECK(AIL_sample_playback_rate(s2d) == rate, "playback rate comes from the file");
	DWORD t0 = GetTickCount();
	AIL_start_sample(s2d);
	CHECK(WaitFor(g_sampleDone, 1, 3000), "end-of-sample callback");
	DWORD took = GetTickCount() - t0;
	printf("       0.2 s sample ended after %lu ms\n", (unsigned long)took);
	CHECK(took >= 120 && took < 1500, "sample plays in real time");
	CHECK(g_callbackThread != GetCurrentThreadId(), "callback runs on the service thread");

	// pause/resume: a paused sample must not end
	g_sampleDone = 0;
	AIL_init_sample(s2d);
	AIL_register_EOS_callback(s2d, SampleDone);
	AIL_set_sample_file(s2d, &pcmWav[0], 0);
	AIL_start_sample(s2d);
	AIL_stop_sample(s2d);
	Sleep(400);
	CHECK(g_sampleDone == 0, "stopped sample does not finish");
	AIL_resume_sample(s2d);
	CHECK(WaitFor(g_sampleDone, 1, 3000), "resumed sample finishes");

	// restart from inside the callback (looping sounds)
	g_sampleDone = 0;
	g_restartSample = s2d;
	g_restartImage = &pcmWav;
	AIL_init_sample(s2d);
	AIL_register_EOS_callback(s2d, SampleDone);
	AIL_set_sample_file(s2d, &pcmWav[0], 0);
	AIL_start_sample(s2d);
	CHECK(WaitFor(g_sampleDone, 3, 5000) && g_restarts == 2, "sample restarted from its own callback (3 plays)");
	g_restartSample = NULL;

	// --- 3D sample, as MilesAudioManager::playSample3D ---
	H3DSAMPLE s3d = AIL_allocate_3D_sample_handle(fast2D);
	CHECK(s3d != NULL, "AIL_allocate_3D_sample_handle");
	CHECK(AIL_set_3D_sample_file(s3d, &pcmWav[0]) == 1, "AIL_set_3D_sample_file");
	AIL_register_3D_EOS_callback(s3d, Sample3DDone);
	AIL_set_3D_sample_distances(s3d, 50.0f, 400.0f);
	AIL_set_3D_position(s3d, 100.0f, 30.0f, 0.0f);
	AIL_set_3D_sample_volume(s3d, 1.0f);
	AIL_set_3D_sample_playback_rate(s3d, AIL_3D_sample_playback_rate(s3d) * 2);	// pitch up an octave
	t0 = GetTickCount();
	AIL_start_3D_sample(s3d);
	CHECK(WaitFor(g_sample3DDone, 1, 3000), "3D end-of-sample callback");
	took = GetTickCount() - t0;
	printf("       0.2 s sample at double rate ended after %lu ms\n", (unsigned long)took);
	CHECK(took < 300, "playback rate change takes effect");

	// --- streams, as MilesAudioManager::playStream and getFileLengthMS ---
	g_streamFile = PcmWav(Tone(rate / 4, rate, 220.0), rate);	// 0.25 s
	HSTREAM probe = AIL_open_stream(dig, "Data\\Audio\\Tracks\\music.wav", 0);
	CHECK(probe != NULL, "AIL_open_stream through the file callbacks");
	long ms = 0;
	AIL_stream_ms_position(probe, &ms, NULL);
	printf("       stream length %ld ms\n", ms);
	CHECK(ms >= 249 && ms <= 250, "stream length in ms");	// 5512 frames at 22050 Hz = 249.98 ms
	AIL_close_stream(probe);
	CHECK(AIL_open_stream(dig, "Data\\missing.wav", 0) == NULL, "missing stream file fails cleanly");

	HSTREAM music = AIL_open_stream(dig, "Data\\Audio\\Tracks\\music.wav", 0);
	AIL_set_stream_loop_count(music, 3);
	AIL_register_stream_callback(music, StreamDone);
	AIL_set_stream_volume_pan(music, 0.5f, 0.5f);
	t0 = GetTickCount();
	AIL_start_stream(music);
	Sleep(350);
	S32 left = AIL_stream_loop_count(music);
	CHECK(left == 2, "loop count drops after the first play");
	CHECK(WaitFor(g_streamDone, 1, 4000), "stream callback after 3 plays");
	took = GetTickCount() - t0;
	printf("       3 x 0.25 s stream ended after %lu ms\n", (unsigned long)took);
	CHECK(took >= 600, "stream looped 3 times");
	AIL_close_stream(music);

	// pause_stream
	g_streamDone = 0;
	music = AIL_open_stream(dig, "Data\\Audio\\Tracks\\music.wav", 0);
	AIL_register_stream_callback(music, StreamDone);
	AIL_start_stream(music);
	AIL_pause_stream(music, 1);
	Sleep(400);
	CHECK(g_streamDone == 0, "paused stream does not finish");
	AIL_pause_stream(music, 0);
	CHECK(WaitFor(g_streamDone, 1, 3000), "unpaused stream finishes");
	AIL_close_stream(music);

	// --- quick API (mission briefings) ---
	HAUDIO quick = AIL_quick_load_and_play("Data\\Audio\\Tracks\\music.wav", 1, 0);
	CHECK(quick != NULL, "AIL_quick_load_and_play");
	AIL_quick_set_volume(quick, 0.7f, 0.5f);
	AIL_quick_unload(quick);

	// --- shutdown, as MilesAudioManager::closeDevice ---
	AIL_release_sample_handle(s2d);
	AIL_release_3D_sample_handle(s3d);
	AIL_close_3D_listener(listener);
	AIL_close_3D_provider(fast2D);
	AIL_mem_free_lock(decoded);
	AIL_shutdown();
	CHECK(g_openFiles == 0, "every stream file was closed");

	printf("%s\n", g_failures ? "TESTS FAILED" : "ALL TESTS PASSED");
	return g_failures ? 1 : 0;
}
