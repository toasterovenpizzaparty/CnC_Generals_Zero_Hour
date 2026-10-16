/*
**	Command & Conquer Generals Zero Hour(tm) - modified version.
**	Licensed under the GNU General Public License v3 or later (see LICENSE.md).
*/
// MSS.h - stand-in for the Miles Sound System 6 SDK header.
//
// The Miles SDK is proprietary and not part of the source release.  This header declares the subset
// of the Miles API that MilesAudioManager.cpp uses, and Compat/MSS/mss_miniaudio.cpp implements it on
// top of miniaudio (Libraries/Source/miniaudio, public domain).  It is our own implementation of the
// interface, not Miles code; behaviour follows what the game expects from each call:
//
//   * samples play a complete .wav file image held in memory (PCM or IMA ADPCM)
//   * streams decode .wav/.mp3 files read through the game's file system (AIL_set_file_callbacks)
//   * 3D samples are positioned around a listener with inverse-distance attenuation
//   * end-of-sample callbacks run on a service thread, never on the real-time audio thread
//
// Build with -DZH_USE_MILES=ON to compile against the real Miles 6 SDK instead.

#ifndef ZH_MSS_H
#define ZH_MSS_H

#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AILCALLBACK __stdcall
#define AILEXPORT	__stdcall

typedef int				S32;
typedef unsigned int	U32;
typedef float			F32;

typedef struct ZhMssSample *	HSAMPLE;
typedef struct ZhMssSample *	H3DSAMPLE;	// 3D samples and listeners share the sample object
typedef struct ZhMssSample *	H3DPOBJECT;
typedef struct ZhMssStream *	HSTREAM;
typedef struct ZhMssStream *	HAUDIO;		// "quick" API handle: a stream
typedef struct ZhMssDriver *	HDIGDRIVER;
typedef struct ZhMssMidi *		HMDIDRIVER;
typedef struct ZhMssDls *		HDLSDEVICE;
typedef U32						HPROVIDER;
typedef U32						HPROENUM;
typedef S32						M3DRESULT;

#define HPROENUM_FIRST			0
#define M3D_NOERR				0

typedef void *AILLPDIRECTSOUND;
typedef void *AILLPDIRECTSOUNDBUFFER;

// Speaker configurations (AIL_set_3D_speaker_type)
enum
{
	AIL_3D_2_SPEAKER = 0,
	AIL_3D_HEADPHONE,
	AIL_3D_SURROUND,
	AIL_3D_4_SPEAKER,
	AIL_3D_51_SPEAKER,
	AIL_3D_71_SPEAKER
};

// Sample pipeline stages (AIL_set_sample_processor)
typedef enum
{
	SP_ASI_DECODER = 0,
	DP_FILTER,
	DP_MERGE,
	N_SAMPLE_STAGES
} SAMPLESTAGE;

#define AIL_FILE_SEEK_BEGIN		0
#define AIL_FILE_SEEK_CURRENT	1
#define AIL_FILE_SEEK_END		2

// Description of a .wav file image (AIL_WAV_info)
typedef struct
{
	S32 format;					// WAVE_FORMAT_PCM, WAVE_FORMAT_IMA_ADPCM, ...
	void const *data_ptr;		// first byte of the sample data
	U32 data_len;				// bytes of sample data
	U32 rate;					// samples per second
	S32 bits;					// bits per sample
	S32 channels;
	U32 samples;				// sample frames
	U32 block_size;				// bytes per block (ADPCM) or per frame (PCM)
	void const *initial_ptr;	// first byte of the file image
} AILSOUNDINFO;

typedef void (AILCALLBACK *AILSAMPLECB)(HSAMPLE sample);
typedef void (AILCALLBACK *AIL3DSAMPLECB)(H3DSAMPLE sample);
typedef void (AILCALLBACK *AILSTREAMCB)(HSTREAM stream);

typedef U32 (AILCALLBACK *AIL_file_open_callback)(char const *filename, U32 *file_handle);
typedef void (AILCALLBACK *AIL_file_close_callback)(U32 file_handle);
typedef S32 (AILCALLBACK *AIL_file_seek_callback)(U32 file_handle, S32 offset, U32 type);
typedef U32 (AILCALLBACK *AIL_file_read_callback)(U32 file_handle, void *buffer, U32 bytes);

// --- System ---------------------------------------------------------------------------------------
char *	AILEXPORT AIL_set_redist_directory(char const *dir);
S32		AILEXPORT AIL_startup(void);
void	AILEXPORT AIL_shutdown(void);
void	AILEXPORT AIL_MSS_version(char *buffer, U32 bufferSize);
U32		AILEXPORT AIL_get_timer_highest_delay(void);
void	AILEXPORT AIL_set_file_callbacks(AIL_file_open_callback opencb, AIL_file_close_callback closecb,
			AIL_file_seek_callback seekcb, AIL_file_read_callback readcb);
void	AILEXPORT AIL_mem_free_lock(void *ptr);
S32		AILEXPORT AIL_get_DirectSound_info(HSAMPLE sample, AILLPDIRECTSOUND *lplpDS, AILLPDIRECTSOUNDBUFFER *lplpDSB);

// --- Quick API ------------------------------------------------------------------------------------
S32		AILEXPORT AIL_quick_startup(S32 use_digital, S32 use_MIDI, U32 output_rate, S32 output_bits, S32 output_channels);
void	AILEXPORT AIL_quick_handles(HDIGDRIVER *pdig, HMDIDRIVER *pmdi, HDLSDEVICE *pdls);
HAUDIO	AILEXPORT AIL_quick_load_and_play(char const *filename, U32 loop_count, S32 wait_request);
void	AILEXPORT AIL_quick_set_volume(HAUDIO audio, F32 volume, F32 extravol);
void	AILEXPORT AIL_quick_unload(HAUDIO audio);

// --- .wav helpers ---------------------------------------------------------------------------------
S32		AILEXPORT AIL_WAV_info(void const *data, AILSOUNDINFO *info);
S32		AILEXPORT AIL_decompress_ADPCM(AILSOUNDINFO const *info, void **outdata, U32 *outsize);

// --- 2D samples -----------------------------------------------------------------------------------
HSAMPLE	AILEXPORT AIL_allocate_sample_handle(HDIGDRIVER dig);
void	AILEXPORT AIL_release_sample_handle(HSAMPLE sample);
void	AILEXPORT AIL_init_sample(HSAMPLE sample);
S32		AILEXPORT AIL_set_sample_file(HSAMPLE sample, void const *file_image, S32 block);
void	AILEXPORT AIL_start_sample(HSAMPLE sample);
void	AILEXPORT AIL_stop_sample(HSAMPLE sample);		// pauses; AIL_resume_sample continues
void	AILEXPORT AIL_resume_sample(HSAMPLE sample);
void	AILEXPORT AIL_set_sample_volume_pan(HSAMPLE sample, F32 volume, F32 pan);	// pan: 0 left, 0.5 center, 1 right
void	AILEXPORT AIL_sample_volume_pan(HSAMPLE sample, F32 *volume, F32 *pan);
void	AILEXPORT AIL_set_sample_playback_rate(HSAMPLE sample, S32 rate);
S32		AILEXPORT AIL_sample_playback_rate(HSAMPLE sample);
void	AILEXPORT AIL_set_sample_user_data(HSAMPLE sample, U32 index, S32 value);
S32		AILEXPORT AIL_sample_user_data(HSAMPLE sample, U32 index);
AILSAMPLECB AILEXPORT AIL_register_EOS_callback(HSAMPLE sample, AILSAMPLECB callback);
HPROVIDER AILEXPORT AIL_set_sample_processor(HSAMPLE sample, SAMPLESTAGE stage, HPROVIDER provider);
S32		AILEXPORT AIL_set_filter_sample_preference(HSAMPLE sample, char const *name, void const *value);
S32		AILEXPORT AIL_enumerate_filters(HPROENUM *next, HPROVIDER *dest, char **name);

// --- 3D samples -----------------------------------------------------------------------------------
S32		AILEXPORT AIL_enumerate_3D_providers(HPROENUM *next, HPROVIDER *dest, char **name);
M3DRESULT AILEXPORT AIL_open_3D_provider(HPROVIDER provider);
void	AILEXPORT AIL_close_3D_provider(HPROVIDER provider);
void	AILEXPORT AIL_set_3D_speaker_type(HPROVIDER provider, S32 speaker_type);
H3DPOBJECT AILEXPORT AIL_open_3D_listener(HPROVIDER provider);
void	AILEXPORT AIL_close_3D_listener(H3DPOBJECT listener);
H3DSAMPLE AILEXPORT AIL_allocate_3D_sample_handle(HPROVIDER provider);
void	AILEXPORT AIL_release_3D_sample_handle(H3DSAMPLE sample);
S32		AILEXPORT AIL_set_3D_sample_file(H3DSAMPLE sample, void const *file_image);
void	AILEXPORT AIL_start_3D_sample(H3DSAMPLE sample);
void	AILEXPORT AIL_stop_3D_sample(H3DSAMPLE sample);
void	AILEXPORT AIL_resume_3D_sample(H3DSAMPLE sample);
void	AILEXPORT AIL_set_3D_sample_volume(H3DSAMPLE sample, F32 volume);
void	AILEXPORT AIL_set_3D_sample_playback_rate(H3DSAMPLE sample, S32 rate);
S32		AILEXPORT AIL_3D_sample_playback_rate(H3DSAMPLE sample);
void	AILEXPORT AIL_set_3D_sample_occlusion(H3DSAMPLE sample, F32 occlusion);
// The game passes (minimum, maximum) distance; either order is accepted.
void	AILEXPORT AIL_set_3D_sample_distances(H3DSAMPLE sample, F32 dist1, F32 dist2);
void	AILEXPORT AIL_set_3D_position(H3DPOBJECT obj, F32 x, F32 y, F32 z);
void	AILEXPORT AIL_set_3D_orientation(H3DPOBJECT obj, F32 x_face, F32 y_face, F32 z_face, F32 x_up, F32 y_up, F32 z_up);
void	AILEXPORT AIL_set_3D_user_data(H3DPOBJECT obj, U32 index, S32 value);
S32		AILEXPORT AIL_3D_user_data(H3DPOBJECT obj, U32 index);
AIL3DSAMPLECB AILEXPORT AIL_register_3D_EOS_callback(H3DSAMPLE sample, AIL3DSAMPLECB callback);

// --- Streams --------------------------------------------------------------------------------------
HSTREAM	AILEXPORT AIL_open_stream(HDIGDRIVER dig, char const *filename, S32 stream_mem);
void	AILEXPORT AIL_close_stream(HSTREAM stream);
void	AILEXPORT AIL_start_stream(HSTREAM stream);
void	AILEXPORT AIL_pause_stream(HSTREAM stream, S32 onoff);
void	AILEXPORT AIL_set_stream_volume_pan(HSTREAM stream, F32 volume, F32 pan);
void	AILEXPORT AIL_stream_volume_pan(HSTREAM stream, F32 *volume, F32 *pan);
void	AILEXPORT AIL_set_stream_loop_count(HSTREAM stream, S32 count);	// 0 = forever
S32		AILEXPORT AIL_stream_loop_count(HSTREAM stream);					// plays remaining, including the current one
AILSTREAMCB AILEXPORT AIL_register_stream_callback(HSTREAM stream, AILSTREAMCB callback);
void	AILEXPORT AIL_stream_ms_position(HSTREAM stream, long *total_milliseconds, long *current_milliseconds);

#ifdef __cplusplus
}
#endif

#endif // ZH_MSS_H
