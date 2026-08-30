/* TinySoundFont - v0.9 - SoundFont2 synthesizer - https://github.com/schellingb/TinySoundFont
                                     no warranty implied; use at your own risk
   Do this:
      #define TSF_IMPLEMENTATION
   before you include this file in *one* C or C++ file to create the implementation.
   // i.e. it should look like this:
   #include ...
   #include ...
   #define TSF_IMPLEMENTATION
   #include "tsf.h"

   [OPTIONAL] #define TSF_NO_STDIO to remove stdio dependency
   [OPTIONAL] #define TSF_MALLOC, TSF_REALLOC, and TSF_FREE to avoid stdlib.h
   [OPTIONAL] #define TSF_MEMCPY, TSF_MEMSET to avoid string.h
   [OPTIONAL] #define TSF_POW, TSF_POWF, TSF_EXPF, TSF_LOG, TSF_TAN, TSF_LOG10, TSF_SQRT to avoid math.h

   NOT YET IMPLEMENTED
     - Support for ChorusEffectsSend and ReverbEffectsSend generators
     - Better low-pass filter without lowering performance too much
     - Support for modulators

   LICENSE (MIT)

   Copyright (C) 2017-2025 Bernhard Schelling
   Based on SFZero, Copyright (C) 2012 Steve Folta (https://github.com/stevefolta/SFZero)

   Permission is hereby granted, free of charge, to any person obtaining a copy of this
   software and associated documentation files (the "Software"), to deal in the Software
   without restriction, including without limitation the rights to use, copy, modify, merge,
   publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
   to whom the Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in all
   copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
   PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
   LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
   TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
   USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#ifndef TSF_INCLUDE_TSF_INL
#define TSF_INCLUDE_TSF_INL

#include "mt32_prog_data.h"

#ifdef __cplusplus
extern "C" {
#  define CPP_DEFAULT0 = 0
#else
#  define CPP_DEFAULT0
#endif

//define this if you want the API functions to be static
#ifdef TSF_STATIC
#define TSFDEF static
#else
#define TSFDEF extern
#endif

// The load functions will return a pointer to a struct tsf which all functions
// thereafter take as the first parameter.
// On error the tsf_load* functions will return NULL most likely due to invalid
// data (or if the file did not exist in tsf_load_filename).
typedef struct tsf tsf;

#ifndef TSF_NO_STDIO
// Directly load a SoundFont from a .sf2 file path
TSFDEF tsf* tsf_load_filename(const char* filename);
#endif

// Load a SoundFont from a block of memory
TSFDEF tsf* tsf_load_memory(const void* buffer, int size);

// Stream structure for the generic loading
struct tsf_stream
{
	// Custom data given to the functions as the first parameter
	void* data;

	// Function pointer will be called to read 'size' bytes into ptr (returns number of read bytes)
	int (*read)(void* data, void* ptr, unsigned int size);

	// Function pointer will be called to skip ahead over 'count' bytes (returns 1 on success, 0 on error)
	int (*skip)(void* data, unsigned int count);
};

// Generic SoundFont loading method using the stream structure above
TSFDEF tsf* tsf_load(struct tsf_stream* stream);

// Copy a tsf instance from an existing one, use tsf_close to close it as well.
// All copied tsf instances and their original instance are linked, and share the underlying soundfont.
// This allows loading a soundfont only once, but using it for multiple independent playbacks.
// (This function isn't thread-safe without locking.)
TSFDEF tsf* tsf_copy(tsf* f);

// Free the memory related to this tsf instance
TSFDEF void tsf_close(tsf* f);

// Stop all playing notes immediately and reset all channel parameters
TSFDEF void tsf_reset(tsf* f);

// Returns the preset index from a bank and preset number, or -1 if it does not exist in the loaded SoundFont
TSFDEF int tsf_get_presetindex(const tsf* f, int bank, int preset_number);

// Returns the number of presets in the loaded SoundFont
TSFDEF int tsf_get_presetcount(const tsf* f);

// Returns the name of a preset index >= 0 and < tsf_get_presetcount()
TSFDEF const char* tsf_get_presetname(const tsf* f, int preset_index);

// Returns the name of a preset by bank and preset number
TSFDEF const char* tsf_bank_get_presetname(const tsf* f, int bank, int preset_number);

// Supported output modes by the render methods
enum TSFOutputMode
{
	// Two channels with single left/right samples one after another
	TSF_STEREO_INTERLEAVED,
	// Two channels with all samples for the left channel first then right
	TSF_STEREO_UNWEAVED,
	// A single channel (stereo instruments are mixed into center)
	TSF_MONO
};

// Thread safety:
//
// 1. Rendering / voices:
//
// Your audio output which calls the tsf_render* functions will most likely
// run on a different thread than where the playback tsf_note* functions
// are called. In which case some sort of concurrency control like a
// mutex needs to be used so they are not called at the same time.
// Alternatively, you can pre-allocate a maximum number of voices that can
// play simultaneously by calling tsf_set_max_voices after loading.
// That way memory re-allocation will not happen during tsf_note_on and
// TSF should become mostly thread safe.
// There is a theoretical chance that ending notes would negatively influence
// a voice that is rendering at the time but it is hard to say.
// Also be aware, this has not been tested much.
//
// 2. Channels:
//
// Calls to tsf_channel_set_... functions may allocate new channels
// if no channel with that number was previously used. Make sure to
// create all channels at the beginning as required if you call tsf_render*
// from a different thread.

// Setup the parameters for the voice render methods
//   outputmode: if mono or stereo and how stereo channel data is ordered
//   samplerate: the number of samples per second (output frequency)
//   global_gain_db: volume gain in decibels (>0 means higher, <0 means lower)
TSFDEF void tsf_set_output(tsf* f, enum TSFOutputMode outputmode, int samplerate, float global_gain_db CPP_DEFAULT0);

// Set the global gain as a volume factor
//   global_gain: the desired volume where 1.0 is 100%
TSFDEF void tsf_set_volume(tsf* f, float global_gain);

// Set the maximum number of voices to play simultaneously
// Depending on the soundfond, one note can cause many new voices to be started,
// so don't keep this number too low or otherwise sounds may not play.
//   max_voices: maximum number to pre-allocate and set the limit to
//   (tsf_set_max_voices returns 0 if allocation failed, otherwise 1)
TSFDEF int tsf_set_max_voices(tsf* f, int max_voices);

// Start playing a note
//   preset_index: preset index >= 0 and < tsf_get_presetcount()
//   key: note value between 0 and 127 (60 being middle C)
//   vel: velocity as a float between 0.0 (equal to note off) and 1.0 (full)
//   bank: instrument bank number (alternative to preset_index)
//   preset_number: preset number (alternative to preset_index)
//   (tsf_note_on returns 0 if the allocation of a new voice failed, otherwise 1)
//   (tsf_bank_note_on returns 0 if preset does not exist or allocation failed, otherwise 1)
TSFDEF int tsf_note_on(tsf* f, int preset_index, int key, float vel);
TSFDEF int tsf_bank_note_on(tsf* f, int bank, int preset_number, int key, float vel);

// Stop playing a note
//   (bank_note_off returns 0 if preset does not exist, otherwise 1)
TSFDEF void tsf_note_off(tsf* f, int preset_index, int key);
TSFDEF int  tsf_bank_note_off(tsf* f, int bank, int preset_number, int key);

// Stop playing all notes (end with sustain and release)
TSFDEF void tsf_note_off_all(tsf* f);

// Returns the number of active voices
TSFDEF int tsf_active_voice_count(tsf* f);

// Render output samples into a buffer
// You can either render as signed 16-bit values (tsf_render_short) or
// as 32-bit float values (tsf_render_float)
//   buffer: target buffer of size samples * output_channels * sizeof(type)
//   samples: number of samples to render
//   flag_mixing: if 0 clear the buffer first, otherwise mix into existing data
TSFDEF void tsf_render_short(tsf* f, short* buffer, int samples, int flag_mixing CPP_DEFAULT0);
TSFDEF void tsf_render_float(tsf* f, float* buffer, int samples, int flag_mixing CPP_DEFAULT0);

// Higher level channel based functions, set up channel parameters
//   channel: channel number
//   preset_index: preset index >= 0 and < tsf_get_presetcount()
//   preset_number: preset number (alternative to preset_index)
//   flag_mididrums: 0 for normal channels, otherwise apply MIDI drum channel rules
//   bank: instrument bank number (alternative to preset_index)
//   pan: stereo panning value from 0.0 (left) to 1.0 (right) (default 0.5 center)
//   volume: linear volume scale factor (default 1.0 full)
//   pitch_wheel: pitch wheel position 0 to 16383 (default 8192 unpitched)
//   pitch_range: range of the pitch wheel in semitones (default 2.0, total +/- 2 semitones)
//   tuning: tuning of all playing voices in semitones (default 0.0, standard (A440) tuning)
//   flag_sustain: 0 to end notes that were held sustained and disable holding sustain otherwise enable it
//   (tsf_set_preset_number and set_bank_preset return 0 if preset does not exist, otherwise 1)
//   (tsf_channel_set_... return 0 if a new channel needed allocation and that failed, otherwise 1)
TSFDEF int tsf_channel_set_presetindex(tsf* f, int channel, int preset_index);
TSFDEF int tsf_channel_set_presetnumber(tsf* f, int channel, int preset_number, int flag_mididrums CPP_DEFAULT0);
TSFDEF int tsf_channel_set_bank(tsf* f, int channel, int bank);
TSFDEF int tsf_channel_set_bank_preset(tsf* f, int channel, int bank, int preset_number);
TSFDEF int tsf_channel_set_pan(tsf* f, int channel, float pan);
TSFDEF int tsf_channel_set_volume(tsf* f, int channel, float volume);
TSFDEF int tsf_channel_set_pitchwheel(tsf* f, int channel, int pitch_wheel);
TSFDEF int tsf_channel_set_pitchrange(tsf* f, int channel, float pitch_range);
TSFDEF int tsf_channel_set_tuning(tsf* f, int channel, float tuning);
TSFDEF int tsf_channel_set_sustain(tsf* f, int channel, int flag_sustain);

// Start or stop playing notes on a channel (needs channel preset to be set)
//   channel: channel number
//   key: note value between 0 and 127 (60 being middle C)
//   vel: velocity as a float between 0.0 (equal to note off) and 1.0 (full)
//   (tsf_channel_note_on returns 0 on allocation failure of new voice, otherwise 1)
TSFDEF int tsf_channel_note_on(tsf* f, int channel, int key, float vel);
TSFDEF void tsf_channel_note_off(tsf* f, int channel, int key);
TSFDEF void tsf_channel_note_off_all(tsf* f, int channel); //end with sustain and release
TSFDEF void tsf_channel_sounds_off_all(tsf* f, int channel); //end immediately

// Apply a MIDI control change to the channel (not all controllers are supported!)
//    (tsf_channel_midi_control returns 0 on allocation failure of new channel, otherwise 1)
TSFDEF int tsf_channel_midi_control(tsf* f, int channel, int controller, int control_value);

// Get current values set on the channels
TSFDEF int tsf_channel_get_preset_index(tsf* f, int channel);
TSFDEF int tsf_channel_get_preset_bank(tsf* f, int channel);
TSFDEF int tsf_channel_get_preset_number(tsf* f, int channel);
TSFDEF float tsf_channel_get_pan(tsf* f, int channel);
TSFDEF float tsf_channel_get_volume(tsf* f, int channel);
TSFDEF int tsf_channel_get_pitchwheel(tsf* f, int channel);
TSFDEF float tsf_channel_get_pitchrange(tsf* f, int channel);
TSFDEF float tsf_channel_get_tuning(tsf* f, int channel);

#ifdef __cplusplus
#  undef CPP_DEFAULT0
}
#endif

// end header
// ---------------------------------------------------------------------------------------------------------
#endif //TSF_INCLUDE_TSF_INL

#ifdef TSF_IMPLEMENTATION
#undef TSF_IMPLEMENTATION

// The lower this block size is the more accurate the effects are.
// Increasing the value significantly lowers the CPU usage of the voice rendering.
// If LFO affects the low-pass filter it can be hearable even as low as 8.
#ifndef TSF_RENDER_EFFECTSAMPLEBLOCK
#define TSF_RENDER_EFFECTSAMPLEBLOCK 64
#endif

// When using tsf_render_short, to do the conversion a buffer of a fixed size is
// allocated on the stack. On low memory platforms this could be made smaller.
// Increasing this above 512 should not have a significant impact on performance.
// The value should be a multiple of TSF_RENDER_EFFECTSAMPLEBLOCK.
#ifndef TSF_RENDER_SHORTBUFFERBLOCK
#define TSF_RENDER_SHORTBUFFERBLOCK 512
#endif

// Grace release time for quick voice off (avoid clicking noise)
#define TSF_FASTRELEASETIME 0.01f

#ifndef TSF_YIELD
#  if defined(ESP_PLATFORM) || defined(ARDUINO)
#    include <freertos/FreeRTOS.h>
#    include <freertos/task.h>
#    define TSF_YIELD() vTaskDelay(pdMS_TO_TICKS(1))
#  else
#    define TSF_YIELD() (void)0
#  endif
#endif

#if !defined(TSF_MALLOC) || !defined(TSF_FREE) || !defined(TSF_REALLOC)
#  include <stdlib.h>
#  define TSF_MALLOC  malloc
#  define TSF_FREE    free
#  define TSF_REALLOC realloc
#endif

#if !defined(TSF_MEMCPY) || !defined(TSF_MEMSET)
#  include <string.h>
#  define TSF_MEMCPY  memcpy
#  define TSF_MEMSET  memset
#endif

#if !defined(TSF_POW) || !defined(TSF_POWF) || !defined(TSF_EXPF) || !defined(TSF_LOG) || !defined(TSF_TAN) || !defined(TSF_LOG10) || !defined(TSF_SQRT)
#  include <math.h>
#  if !defined(__cplusplus) && !defined(NAN) && !defined(powf) && !defined(expf) && !defined(sqrtf)
#    define powf (float)pow // deal with old math.h
#    define expf (float)exp // files that come without
#    define sqrtf (float)sqrt // powf, expf and sqrtf
#  endif
#  define TSF_POW     powf
#  define TSF_POWF    powf
#  define TSF_EXPF    expf
#  define TSF_LOG     logf
#  define TSF_LOGF    logf
#  define TSF_TAN     tanf
#  define TSF_TANF    tanf
#  define TSF_LOG10   log10f
#  define TSF_LOG10F  log10f
#  define TSF_SQRTF   sqrtf
#  define TSF_SINF    sinf
#  define TSF_COSF    cosf
#endif

#ifndef TSF_NO_STDIO
#  include <stdio.h>
#endif

#define TSF_TRUE 1
#define TSF_FALSE 0
#define TSF_BOOL unsigned char
#define TSF_PI 3.14159265358979323846264338327950288
#define TSF_NULL 0

#ifdef __cplusplus
extern "C" {
#endif

typedef char tsf_fourcc[4];
typedef signed char tsf_s8;
typedef unsigned char tsf_u8;
typedef unsigned short tsf_u16;
typedef signed short tsf_s16;
typedef unsigned int tsf_u32;
typedef char tsf_char20[20];

#define TSF_FourCCEquals(value1, value2) (value1[0] == value2[0] && value1[1] == value2[1] && value1[2] == value2[2] && value1[3] == value2[3])

struct tsf
{
	struct tsf_preset* presets;
	short* fontSamples;
	struct tsf_voice* voices;
	struct tsf_channels* channels;

	int presetNum;
	int voiceNum;
	int maxVoiceNum;
	unsigned int voicePlayIndex;

	enum TSFOutputMode outputmode;
	float outSampleRate;
	float globalGainDB;
	int* refCount;
	int mt32StdDrumIndex;
	int mt32CmDrumIndex;
};

#ifndef TSF_NO_STDIO
static int tsf_stream_stdio_read(FILE* f, void* ptr, unsigned int size) { return (int)fread(ptr, 1, size, f); }
static int tsf_stream_stdio_skip(FILE* f, unsigned int count) { return !fseek(f, count, SEEK_CUR); }
TSFDEF tsf* tsf_load_filename(const char* filename)
{
	tsf* res;
	struct tsf_stream stream = { TSF_NULL, (int(*)(void*,void*,unsigned int))&tsf_stream_stdio_read, (int(*)(void*,unsigned int))&tsf_stream_stdio_skip };
	#if __STDC_WANT_SECURE_LIB__
	FILE* f = TSF_NULL; fopen_s(&f, filename, "rb");
	#else
	FILE* f = fopen(filename, "rb");
	#endif
	if (!f)
	{
		//if (e) *e = TSF_FILENOTFOUND;
		return TSF_NULL;
	}
	stream.data = f;
	res = tsf_load(&stream);
	fclose(f);
	return res;
}
#endif

struct tsf_stream_memory { const char* buffer; unsigned int total, pos; };
static int tsf_stream_memory_read(struct tsf_stream_memory* m, void* ptr, unsigned int size) { if (size > m->total - m->pos) size = m->total - m->pos; TSF_MEMCPY(ptr, m->buffer+m->pos, size); m->pos += size; return size; }
static int tsf_stream_memory_skip(struct tsf_stream_memory* m, unsigned int count) { if (m->pos + count > m->total) return 0; m->pos += count; return 1; }
TSFDEF tsf* tsf_load_memory(const void* buffer, int size)
{
	struct tsf_stream stream = { TSF_NULL, (int(*)(void*,void*,unsigned int))&tsf_stream_memory_read, (int(*)(void*,unsigned int))&tsf_stream_memory_skip };
	struct tsf_stream_memory f = { 0, 0, 0 };
	f.buffer = (const char*)buffer;
	f.total = size;
	stream.data = &f;
	return tsf_load(&stream);
}

enum { TSF_LOOPMODE_NONE, TSF_LOOPMODE_CONTINUOUS, TSF_LOOPMODE_SUSTAIN };

enum { TSF_SEGMENT_NONE, TSF_SEGMENT_DELAY, TSF_SEGMENT_ATTACK, TSF_SEGMENT_HOLD, TSF_SEGMENT_DECAY, TSF_SEGMENT_SUSTAIN, TSF_SEGMENT_RELEASE, TSF_SEGMENT_DONE };

struct tsf_hydra
{
	struct tsf_hydra_phdr *phdrs; struct tsf_hydra_pbag *pbags; struct tsf_hydra_pmod *pmods;
	struct tsf_hydra_pgen *pgens; struct tsf_hydra_inst *insts; struct tsf_hydra_ibag *ibags;
	struct tsf_hydra_imod *imods; struct tsf_hydra_igen *igens; struct tsf_hydra_shdr *shdrs;
	int phdrNum, pbagNum, pmodNum, pgenNum, instNum, ibagNum, imodNum, igenNum, shdrNum;
};

union tsf_hydra_genamount { struct { tsf_u8 lo, hi; } range; tsf_s16 shortAmount; tsf_u16 wordAmount; };
struct tsf_hydra_phdr { tsf_char20 presetName; tsf_u16 preset, bank, presetBagNdx; tsf_u32 library, genre, morphology; };
struct tsf_hydra_pbag { tsf_u16 genNdx, modNdx; };
struct tsf_hydra_pmod { tsf_u16 modSrcOper, modDestOper; tsf_s16 modAmount; tsf_u16 modAmtSrcOper, modTransOper; };
struct tsf_hydra_pgen { tsf_u16 genOper; union tsf_hydra_genamount genAmount; };
struct tsf_hydra_inst { tsf_char20 instName; tsf_u16 instBagNdx; };
struct tsf_hydra_ibag { tsf_u16 instGenNdx, instModNdx; };
struct tsf_hydra_imod { tsf_u16 modSrcOper, modDestOper; tsf_s16 modAmount; tsf_u16 modAmtSrcOper, modTransOper; };
struct tsf_hydra_igen { tsf_u16 genOper; union tsf_hydra_genamount genAmount; };
struct tsf_hydra_shdr { tsf_char20 sampleName; tsf_u32 start, end, startLoop, endLoop, sampleRate; tsf_u8 originalPitch; tsf_s8 pitchCorrection; tsf_u16 sampleLink, sampleType; };

#define TSFR(FIELD) stream->read(stream->data, &i->FIELD, sizeof(i->FIELD));
static void tsf_hydra_read_phdr(struct tsf_hydra_phdr* i, struct tsf_stream* stream) { TSFR(presetName) TSFR(preset) TSFR(bank) TSFR(presetBagNdx) TSFR(library) TSFR(genre) TSFR(morphology) }
static void tsf_hydra_read_pbag(struct tsf_hydra_pbag* i, struct tsf_stream* stream) { TSFR(genNdx) TSFR(modNdx) }
static void tsf_hydra_read_pmod(struct tsf_hydra_pmod* i, struct tsf_stream* stream) { TSFR(modSrcOper) TSFR(modDestOper) TSFR(modAmount) TSFR(modAmtSrcOper) TSFR(modTransOper) }
static void tsf_hydra_read_pgen(struct tsf_hydra_pgen* i, struct tsf_stream* stream) { TSFR(genOper) TSFR(genAmount) }
static void tsf_hydra_read_inst(struct tsf_hydra_inst* i, struct tsf_stream* stream) { TSFR(instName) TSFR(instBagNdx) }
static void tsf_hydra_read_ibag(struct tsf_hydra_ibag* i, struct tsf_stream* stream) { TSFR(instGenNdx) TSFR(instModNdx) }
static void tsf_hydra_read_imod(struct tsf_hydra_imod* i, struct tsf_stream* stream) { TSFR(modSrcOper) TSFR(modDestOper) TSFR(modAmount) TSFR(modAmtSrcOper) TSFR(modTransOper) }
static void tsf_hydra_read_igen(struct tsf_hydra_igen* i, struct tsf_stream* stream) { TSFR(genOper) TSFR(genAmount) }
static void tsf_hydra_read_shdr(struct tsf_hydra_shdr* i, struct tsf_stream* stream) { TSFR(sampleName) TSFR(start) TSFR(end) TSFR(startLoop) TSFR(endLoop) TSFR(sampleRate) TSFR(originalPitch) TSFR(pitchCorrection) TSFR(sampleLink) TSFR(sampleType) }
#undef TSFR

struct tsf_riffchunk { tsf_fourcc id; tsf_u32 size; };
struct tsf_envelope { float delay, attack, hold, decay, sustain, release, keynumToHold, keynumToDecay; };
struct tsf_voice_envelope { unsigned char segment, segmentIsExponential : 1, isAmpEnv : 1; short midiVelocity; float level, slope; int samplesUntilNextSegment; struct tsf_envelope parameters; };
struct tsf_voice_lowpass { float QInv, a0, a1, b1, b2, z1, z2; TSF_BOOL active; };
struct tsf_voice_lfo { int samplesUntil; float level, delta, fadeStep, currentFade; };

struct tsf_region
{
	int loop_mode;
	unsigned int sample_rate;
	unsigned char lokey, hikey, lovel, hivel;
	unsigned int group, offset, end, loop_start, loop_end;
	int transpose, tune, pitch_keycenter, pitch_keytrack;
	float attenuation, pan;
	struct tsf_envelope ampenv, modenv;
	int initialFilterQ, initialFilterFc;
	int modEnvToPitch, modEnvToFilterFc, modLfoToFilterFc, modLfoToVolume;
	float delayModLFO;
	int freqModLFO, modLfoToPitch;
	float delayVibLFO;
	int freqVibLFO, vibLfoToPitch;
};

struct tsf_preset
{
	tsf_char20 presetName;
	tsf_u16 preset, bank;
	struct tsf_region* regions;
	int regionNum;
};

struct tsf_voice
{
	int playingPreset, playingKey, playingChannel, heldSustain, heldSostenuto;
	struct tsf_region* region;
	float pitchInputTimecents, pitchOutputFactor;
	uint64_t sourceSamplePosition; // 32.32 고정소수점 (상위 32비트: 정수 샘플, 하위 32비트: 서브샘플 위상)
	float  noteGainDB, panFactorLeft, panFactorRight, lowpassCutoffCents;
	unsigned int playIndex, loopStart, loopEnd;
	struct tsf_voice_envelope ampenv, modenv;
	struct tsf_voice_lowpass lowpass;
	struct tsf_voice_lfo modlfo, viblfo;
};

struct tsf_channel
{
	unsigned short presetIndex, bank, pitchWheel, midiPan, midiVolume, midiExpression, midiRPN, midiData : 14, sustain : 1, sostenuto : 1, isDrum : 1;
	float panOffset, gainDB, pitchRange, tuning;
	short filterCutoffOffset;
	short filterResonanceOffset;
	unsigned char modWheel;
	short attackOffset, decayOffset, releaseOffset;
	unsigned char softPedal;
	unsigned short nrpnParam;
	unsigned char portamentoOn;
	unsigned char portamentoTime;
	unsigned char lastKey;
	unsigned char reverbSend;
	unsigned char chorusSend;
	unsigned char isMono;
	float modDepth;
	signed char scaleTuning[12];
	float tuningOffset;
};

struct tsf_channels
{
	void (*setupVoice)(tsf* f, struct tsf_voice* voice);
	int channelNum, activeChannel;
	struct tsf_channel channels[1];
};

static inline float tsf_timecents2Secsd(float timecents) { return exp2f(timecents * (1.0f / 1200.0f)); }
static inline float tsf_timecents2Secsf(float timecents) { return exp2f(timecents * (1.0f / 1200.0f)); }
static inline float tsf_cents2Hertz(float cents) { return 8.176f * exp2f(cents * (1.0f / 1200.0f)); }
static inline float tsf_decibelsToGain(float db) { return (db > -100.f ? expf(db * 0.1151292546497f) : 0.0f); }
static inline float tsf_gainToDecibels(float gain) { return (gain <= .00001f ? -100.f : (8.685889638f * logf(gain))); }

static TSF_BOOL tsf_riffchunk_read(struct tsf_riffchunk* parent, struct tsf_riffchunk* chunk, struct tsf_stream* stream)
{
	TSF_BOOL IsRiff, IsList;
	if (parent && parent->size < sizeof(tsf_fourcc) + sizeof(tsf_u32)) return TSF_FALSE;

	// RIFF 홀수 패딩 바이트(0x00) 자동 보정
	if (!stream->read(stream->data, &chunk->id, sizeof(tsf_fourcc))) return TSF_FALSE;
	if (*chunk->id == 0) {
		chunk->id[0] = chunk->id[1];
		chunk->id[1] = chunk->id[2];
		chunk->id[2] = chunk->id[3];
		if (!stream->read(stream->data, &chunk->id[3], 1)) return TSF_FALSE;
		if (parent && parent->size > 0) parent->size -= 1;
	}

	if (*chunk->id <= ' ' || *chunk->id >= 'z') return TSF_FALSE;
	if (!stream->read(stream->data, &chunk->size, sizeof(tsf_u32))) return TSF_FALSE;

	tsf_u32 totalChunkBytes = sizeof(tsf_fourcc) + sizeof(tsf_u32) + ((chunk->size + 1) & ~1);
	if (parent) {
		if (parent->size >= totalChunkBytes) {
			parent->size -= totalChunkBytes;
		} else {
			parent->size = 0;
		}
	}

	IsRiff = TSF_FourCCEquals(chunk->id, "RIFF"), IsList = TSF_FourCCEquals(chunk->id, "LIST");
	if (IsRiff && parent) return TSF_FALSE; // not allowed
	if (!IsRiff && !IsList) return TSF_TRUE; // custom type without sub type

	if (!stream->read(stream->data, &chunk->id, sizeof(tsf_fourcc))) return TSF_FALSE;
	if (*chunk->id == 0) {
		chunk->id[0] = chunk->id[1];
		chunk->id[1] = chunk->id[2];
		chunk->id[2] = chunk->id[3];
		if (!stream->read(stream->data, &chunk->id[3], 1)) return TSF_FALSE;
	}
	if (*chunk->id <= ' ' || *chunk->id >= 'z') return TSF_FALSE;
	if (chunk->size >= sizeof(tsf_fourcc)) chunk->size -= sizeof(tsf_fourcc);
	return TSF_TRUE;
}

static void tsf_region_clear(struct tsf_region* i, TSF_BOOL for_relative)
{
	TSF_MEMSET(i, 0, sizeof(struct tsf_region));
	i->hikey = i->hivel = 127;
	i->pitch_keycenter = 60; // C4
	if (for_relative) return;

	i->pitch_keytrack = 100;

	i->pitch_keycenter = -1;

	// SF2 defaults in timecents.
	i->ampenv.delay = i->ampenv.attack = i->ampenv.hold = i->ampenv.decay = i->ampenv.release = -12000.0f;
	i->modenv.delay = i->modenv.attack = i->modenv.hold = i->modenv.decay = i->modenv.release = -12000.0f;

	i->initialFilterFc = 13500;

	i->delayModLFO = -12000.0f;
	i->delayVibLFO = -12000.0f;
}

static void tsf_region_operator(struct tsf_region* region, tsf_u16 genOper, union tsf_hydra_genamount* amount, struct tsf_region* merge_region)
{
	enum
	{
		_GEN_TYPE_MASK       = 0x0F,
		GEN_FLOAT            = 0x01,
		GEN_INT              = 0x02,
		GEN_UINT_ADD         = 0x03,
		GEN_UINT_ADD15       = 0x04,
		GEN_KEYRANGE         = 0x05,
		GEN_VELRANGE         = 0x06,
		GEN_LOOPMODE         = 0x07,
		GEN_GROUP            = 0x08,
		GEN_KEYCENTER        = 0x09,

		_GEN_LIMIT_MASK      = 0xF0,
		GEN_INT_LIMIT12K     = 0x10, //min -12000, max 12000
		GEN_INT_LIMITFC      = 0x20, //min 1500, max 13500
		GEN_INT_LIMITQ       = 0x30, //min 0, max 960
		GEN_INT_LIMIT960     = 0x40, //min -960, max 960
		GEN_INT_LIMIT16K4500 = 0x50, //min -16000, max 4500
		GEN_FLOAT_LIMIT12K5K = 0x60, //min -12000, max 5000
		GEN_FLOAT_LIMIT12K8K = 0x70, //min -12000, max 8000
		GEN_FLOAT_LIMIT1200  = 0x80, //min -1200, max 1200
		GEN_FLOAT_LIMITPAN   = 0x90, //* .001f, min -.5f, max .5f,
		GEN_FLOAT_LIMITATTN  = 0xA0, //* .1f, min 0, max 144.0
		GEN_FLOAT_MAX1000    = 0xB0, //min 0, max 1000
		GEN_FLOAT_MAX1440    = 0xC0, //min 0, max 1440

		_GEN_MAX = 59
	};
	#define _TSFREGIONOFFSET(TYPE, FIELD) (unsigned char)(((TYPE*)&((struct tsf_region*)0)->FIELD) - (TYPE*)0)
	#define _TSFREGIONENVOFFSET(TYPE, ENV, FIELD) (unsigned char)(((TYPE*)&((&(((struct tsf_region*)0)->ENV))->FIELD)) - (TYPE*)0)
	static const struct { unsigned char mode, offset; } genMetas[_GEN_MAX] =
	{
		{ GEN_UINT_ADD                     , _TSFREGIONOFFSET(unsigned int, offset               ) }, // 0 StartAddrsOffset
		{ GEN_UINT_ADD                     , _TSFREGIONOFFSET(unsigned int, end                  ) }, // 1 EndAddrsOffset
		{ GEN_UINT_ADD                     , _TSFREGIONOFFSET(unsigned int, loop_start           ) }, // 2 StartloopAddrsOffset
		{ GEN_UINT_ADD                     , _TSFREGIONOFFSET(unsigned int, loop_end             ) }, // 3 EndloopAddrsOffset
		{ GEN_UINT_ADD15                   , _TSFREGIONOFFSET(unsigned int, offset               ) }, // 4 StartAddrsCoarseOffset
		{ GEN_INT   | GEN_INT_LIMIT12K     , _TSFREGIONOFFSET(         int, modLfoToPitch        ) }, // 5 ModLfoToPitch
		{ GEN_INT   | GEN_INT_LIMIT12K     , _TSFREGIONOFFSET(         int, vibLfoToPitch        ) }, // 6 VibLfoToPitch
		{ GEN_INT   | GEN_INT_LIMIT12K     , _TSFREGIONOFFSET(         int, modEnvToPitch        ) }, // 7 ModEnvToPitch
		{ GEN_INT   | GEN_INT_LIMITFC      , _TSFREGIONOFFSET(         int, initialFilterFc      ) }, // 8 InitialFilterFc
		{ GEN_INT   | GEN_INT_LIMITQ       , _TSFREGIONOFFSET(         int, initialFilterQ       ) }, // 9 InitialFilterQ
		{ GEN_INT   | GEN_INT_LIMIT12K     , _TSFREGIONOFFSET(         int, modLfoToFilterFc     ) }, //10 ModLfoToFilterFc
		{ GEN_INT   | GEN_INT_LIMIT12K     , _TSFREGIONOFFSET(         int, modEnvToFilterFc     ) }, //11 ModEnvToFilterFc
		{ GEN_UINT_ADD15                   , _TSFREGIONOFFSET(unsigned int, end                  ) }, //12 EndAddrsCoarseOffset
		{ GEN_INT   | GEN_INT_LIMIT960     , _TSFREGIONOFFSET(         int, modLfoToVolume       ) }, //13 ModLfoToVolume
		{ 0                                , (0                                                  ) }, //   Unused
		{ 0                                , (0                                                  ) }, //15 ChorusEffectsSend (unsupported)
		{ 0                                , (0                                                  ) }, //16 ReverbEffectsSend (unsupported)
		{ GEN_FLOAT | GEN_FLOAT_LIMITPAN   , _TSFREGIONOFFSET(       float, pan                  ) }, //17 Pan
		{ 0                                , (0                                                  ) }, //   Unused
		{ 0                                , (0                                                  ) }, //   Unused
		{ 0                                , (0                                                  ) }, //   Unused
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K5K , _TSFREGIONOFFSET(       float, delayModLFO          ) }, //21 DelayModLFO
		{ GEN_INT   | GEN_INT_LIMIT16K4500 , _TSFREGIONOFFSET(         int, freqModLFO           ) }, //22 FreqModLFO
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K5K , _TSFREGIONOFFSET(       float, delayVibLFO          ) }, //23 DelayVibLFO
		{ GEN_INT   | GEN_INT_LIMIT16K4500 , _TSFREGIONOFFSET(         int, freqVibLFO           ) }, //24 FreqVibLFO
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K5K , _TSFREGIONENVOFFSET(    float, modenv, delay        ) }, //25 DelayModEnv
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K8K , _TSFREGIONENVOFFSET(    float, modenv, attack       ) }, //26 AttackModEnv
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K5K , _TSFREGIONENVOFFSET(    float, modenv, hold         ) }, //27 HoldModEnv
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K8K , _TSFREGIONENVOFFSET(    float, modenv, decay        ) }, //28 DecayModEnv
		{ GEN_FLOAT | GEN_FLOAT_MAX1000    , _TSFREGIONENVOFFSET(    float, modenv, sustain      ) }, //29 SustainModEnv
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K8K , _TSFREGIONENVOFFSET(    float, modenv, release      ) }, //30 ReleaseModEnv
		{ GEN_FLOAT | GEN_FLOAT_LIMIT1200  , _TSFREGIONENVOFFSET(    float, modenv, keynumToHold ) }, //31 KeynumToModEnvHold
		{ GEN_FLOAT | GEN_FLOAT_LIMIT1200  , _TSFREGIONENVOFFSET(    float, modenv, keynumToDecay) }, //32 KeynumToModEnvDecay
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K5K , _TSFREGIONENVOFFSET(    float, ampenv, delay        ) }, //33 DelayVolEnv
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K8K , _TSFREGIONENVOFFSET(    float, ampenv, attack       ) }, //34 AttackVolEnv
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K5K , _TSFREGIONENVOFFSET(    float, ampenv, hold         ) }, //35 HoldVolEnv
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K8K , _TSFREGIONENVOFFSET(    float, ampenv, decay        ) }, //36 DecayVolEnv
		{ GEN_FLOAT | GEN_FLOAT_MAX1440    , _TSFREGIONENVOFFSET(    float, ampenv, sustain      ) }, //37 SustainVolEnv
		{ GEN_FLOAT | GEN_FLOAT_LIMIT12K8K , _TSFREGIONENVOFFSET(    float, ampenv, release      ) }, //38 ReleaseVolEnv
		{ GEN_FLOAT | GEN_FLOAT_LIMIT1200  , _TSFREGIONENVOFFSET(    float, ampenv, keynumToHold ) }, //39 KeynumToVolEnvHold
		{ GEN_FLOAT | GEN_FLOAT_LIMIT1200  , _TSFREGIONENVOFFSET(    float, ampenv, keynumToDecay) }, //40 KeynumToVolEnvDecay
		{ 0                                , (0                                                  ) }, //   Instrument (special)
		{ 0                                , (0                                                  ) }, //   Reserved
		{ GEN_KEYRANGE                     , (0                                                  ) }, //43 KeyRange
		{ GEN_VELRANGE                     , (0                                                  ) }, //44 VelRange
		{ GEN_UINT_ADD15                   , _TSFREGIONOFFSET(unsigned int, loop_start           ) }, //45 StartloopAddrsCoarseOffset
		{ 0                                , (0                                                  ) }, //46 Keynum (special)
		{ 0                                , (0                                                  ) }, //47 Velocity (special)
		{ GEN_FLOAT | GEN_FLOAT_LIMITATTN  , _TSFREGIONOFFSET(       float, attenuation          ) }, //48 InitialAttenuation
		{ 0                                , (0                                                  ) }, //   Reserved
		{ GEN_UINT_ADD15                   , _TSFREGIONOFFSET(unsigned int, loop_end             ) }, //50 EndloopAddrsCoarseOffset
		{ GEN_INT                          , _TSFREGIONOFFSET(         int, transpose            ) }, //51 CoarseTune
		{ GEN_INT                          , _TSFREGIONOFFSET(         int, tune                 ) }, //52 FineTune
		{ 0                                , (0                                                  ) }, //   SampleID (special)
		{ GEN_LOOPMODE                     , _TSFREGIONOFFSET(         int, loop_mode            ) }, //54 SampleModes
		{ 0                                , (0                                                  ) }, //   Reserved
		{ GEN_INT                          , _TSFREGIONOFFSET(         int, pitch_keytrack       ) }, //56 ScaleTuning
		{ GEN_GROUP                        , _TSFREGIONOFFSET(unsigned int, group                ) }, //57 ExclusiveClass
		{ GEN_KEYCENTER                    , _TSFREGIONOFFSET(         int, pitch_keycenter      ) }, //58 OverridingRootKey
	};
	#undef _TSFREGIONOFFSET
	#undef _TSFREGIONENVOFFSET
	if (amount)
	{
		int offset;
		if (genOper >= _GEN_MAX) return;
		offset = genMetas[genOper].offset;
		switch (genMetas[genOper].mode & _GEN_TYPE_MASK)
		{
			case GEN_FLOAT:      ((       float*)region)[offset]  = amount->shortAmount;     return;
			case GEN_INT:        ((         int*)region)[offset]  = amount->shortAmount;     return;
			case GEN_UINT_ADD:   ((unsigned int*)region)[offset] += amount->shortAmount;     return;
			case GEN_UINT_ADD15: ((unsigned int*)region)[offset] += amount->shortAmount<<15; return;
			case GEN_KEYRANGE:   region->lokey = amount->range.lo; region->hikey = amount->range.hi; return;
			case GEN_VELRANGE:   region->lovel = amount->range.lo; region->hivel = amount->range.hi; return;
			case GEN_LOOPMODE:   region->loop_mode       = ((amount->wordAmount&3) == 3 ? TSF_LOOPMODE_SUSTAIN : ((amount->wordAmount&3) == 1 ? TSF_LOOPMODE_CONTINUOUS : TSF_LOOPMODE_NONE)); return;
			case GEN_GROUP:      region->group           = amount->wordAmount;  return;
			case GEN_KEYCENTER:  region->pitch_keycenter = amount->shortAmount; return;
		}
	}
	else //merge regions and clamp values
	{
		for (genOper = 0; genOper != _GEN_MAX; genOper++)
		{
			int offset = genMetas[genOper].offset;
			switch (genMetas[genOper].mode & _GEN_TYPE_MASK)
			{
				case GEN_FLOAT:
				{
					float *val = &((float*)region)[offset], vfactor, vmin, vmax;
					*val += ((float*)merge_region)[offset];
					switch (genMetas[genOper].mode & _GEN_LIMIT_MASK)
					{
						case GEN_FLOAT_LIMIT12K5K: vfactor =   1.0f; vmin = -12000.0f; vmax = 5000.0f; break;
						case GEN_FLOAT_LIMIT12K8K: vfactor =   1.0f; vmin = -12000.0f; vmax = 8000.0f; break;
						case GEN_FLOAT_LIMIT1200:  vfactor =   1.0f; vmin =  -1200.0f; vmax = 1200.0f; break;
						case GEN_FLOAT_LIMITPAN:   vfactor = 0.001f; vmin =     -0.5f; vmax =    0.5f; break;
						case GEN_FLOAT_LIMITATTN:  vfactor =   0.1f; vmin =      0.0f; vmax =  144.0f; break;
						case GEN_FLOAT_MAX1000:    vfactor =   1.0f; vmin =      0.0f; vmax = 1000.0f; break;
						case GEN_FLOAT_MAX1440:    vfactor =   1.0f; vmin =      0.0f; vmax = 1440.0f; break;
						default: continue;
					}
					*val *= vfactor;
					if      (*val < vmin) *val = vmin;
					else if (*val > vmax) *val = vmax;
					continue;
				}
				case GEN_INT:
				{
					int *val = &((int*)region)[offset], vmin, vmax;
					*val += ((int*)merge_region)[offset];
					switch (genMetas[genOper].mode & _GEN_LIMIT_MASK)
					{
						case GEN_INT_LIMIT12K:     vmin = -12000; vmax = 12000; break;
						case GEN_INT_LIMITFC:      vmin =   1500; vmax = 13500; break;
						case GEN_INT_LIMITQ:       vmin =      0; vmax =   960; break;
						case GEN_INT_LIMIT960:     vmin =   -960; vmax =   960; break;
						case GEN_INT_LIMIT16K4500: vmin = -16000; vmax =  4500; break;
						default: continue;
					}
					if      (*val < vmin) *val = vmin;
					else if (*val > vmax) *val = vmax;
					continue;
				}
				case GEN_UINT_ADD:
				{
					((unsigned int*)region)[offset] += ((unsigned int*)merge_region)[offset];
					continue;
				}
			}
		}
	}
}

static void tsf_region_envtosecs(struct tsf_envelope* p, TSF_BOOL sustainIsGain)
{
	// EG times need to be converted from timecents to seconds.
	// Pin very short EG segments.  Timecents don't get to zero, and our EG is
	// happier with zero values.
	p->delay   = (p->delay   < -11950.0f ? 0.0f : tsf_timecents2Secsf(p->delay));
	p->attack  = (p->attack  < -11950.0f ? 0.0f : tsf_timecents2Secsf(p->attack));
	p->release = (p->release < -11950.0f ? 0.0f : tsf_timecents2Secsf(p->release));

	// If we have dynamic hold or decay times depending on key number we need
	// to keep the values in timecents so we can calculate it during startNote
	if (!p->keynumToHold)  p->hold  = (p->hold  < -11950.0f ? 0.0f : tsf_timecents2Secsf(p->hold));
	if (!p->keynumToDecay) p->decay = (p->decay < -11950.0f ? 0.0f : tsf_timecents2Secsf(p->decay));

	if (p->sustain < 0.0f) p->sustain = 0.0f;
	else if (sustainIsGain) p->sustain = tsf_decibelsToGain(-p->sustain / 10.0f);
	else p->sustain = 1.0f - (p->sustain / 1000.0f);
}

static int tsf_load_presets(tsf* res, struct tsf_hydra *hydra, unsigned int fontSampleCount)
{
	enum { GenInstrument = 41, GenKeyRange = 43, GenVelRange = 44, GenSampleID = 53 };
	// Read each preset.
	struct tsf_hydra_phdr *pphdr, *pphdrMax;
	res->presetNum = hydra->phdrNum - 1;
	Serial.printf("[TSF] Loading %d presets into PSRAM...\n", res->presetNum);
	res->presets = (struct tsf_preset*)TSF_MALLOC(res->presetNum * sizeof(struct tsf_preset));
	if (!res->presets) { Serial.println("[TSF] Error: Failed to allocate presets array!"); return 0; }
	else { int i; for (i = 0; i != res->presetNum; i++) res->presets[i].regions = TSF_NULL; }
	for (pphdr = hydra->phdrs, pphdrMax = pphdr + hydra->phdrNum - 1; pphdr != pphdrMax; pphdr++)
	{
		TSF_YIELD();
		int sortedIndex = 0, region_index = 0;
		struct tsf_hydra_phdr *otherphdr;
		struct tsf_preset* preset;
		struct tsf_hydra_pbag *ppbag, *ppbagEnd;
		struct tsf_region globalRegion;
		for (otherphdr = hydra->phdrs; otherphdr != pphdrMax; otherphdr++)
		{
			if (otherphdr == pphdr || otherphdr->bank > pphdr->bank) continue;
			else if (otherphdr->bank < pphdr->bank) sortedIndex++;
			else if (otherphdr->preset > pphdr->preset) continue;
			else if (otherphdr->preset < pphdr->preset) sortedIndex++;
			else if (otherphdr < pphdr) sortedIndex++;
		}

		preset = &res->presets[sortedIndex];
		TSF_MEMCPY(preset->presetName, pphdr->presetName, sizeof(preset->presetName));
		preset->presetName[sizeof(preset->presetName)-1] = '\0'; //should be zero terminated in source file but make sure
		preset->bank = pphdr->bank;
		preset->preset = pphdr->preset;
		preset->regionNum = 0;

		//count regions covered by this preset
		for (ppbag = hydra->pbags + pphdr->presetBagNdx, ppbagEnd = hydra->pbags + pphdr[1].presetBagNdx; ppbag != ppbagEnd; ppbag++)
		{
			unsigned char plokey = 0, phikey = 127, plovel = 0, phivel = 127;
			struct tsf_hydra_pgen *ppgen, *ppgenEnd; struct tsf_hydra_inst *pinst; struct tsf_hydra_ibag *pibag, *pibagEnd; struct tsf_hydra_igen *pigen, *pigenEnd;
			for (ppgen = hydra->pgens + ppbag->genNdx, ppgenEnd = hydra->pgens + ppbag[1].genNdx; ppgen != ppgenEnd; ppgen++)
			{
				if (ppgen->genOper == GenKeyRange) { plokey = ppgen->genAmount.range.lo; phikey = ppgen->genAmount.range.hi; continue; }
				if (ppgen->genOper == GenVelRange) { plovel = ppgen->genAmount.range.lo; phivel = ppgen->genAmount.range.hi; continue; }
				if (ppgen->genOper != GenInstrument) continue;
				if (ppgen->genAmount.wordAmount >= hydra->instNum) continue;
				pinst = hydra->insts + ppgen->genAmount.wordAmount;
				for (pibag = hydra->ibags + pinst->instBagNdx, pibagEnd = hydra->ibags + pinst[1].instBagNdx; pibag != pibagEnd; pibag++)
				{
					unsigned char ilokey = 0, ihikey = 127, ilovel = 0, ihivel = 127;
					for (pigen = hydra->igens + pibag->instGenNdx, pigenEnd = hydra->igens + pibag[1].instGenNdx; pigen != pigenEnd; pigen++)
					{
						if (pigen->genOper == GenKeyRange) { ilokey = pigen->genAmount.range.lo; ihikey = pigen->genAmount.range.hi; continue; }
						if (pigen->genOper == GenVelRange) { ilovel = pigen->genAmount.range.lo; ihivel = pigen->genAmount.range.hi; continue; }
						if (pigen->genOper == GenSampleID && ihikey >= plokey && ilokey <= phikey && ihivel >= plovel && ilovel <= phivel) preset->regionNum++;
					}
				}
			}
		}

		if (preset->regionNum == 0)
		{
			preset->regions = TSF_NULL;
		}
		else
		{
			preset->regions = (struct tsf_region*)TSF_MALLOC(preset->regionNum * sizeof(struct tsf_region));
			if (!preset->regions)
			{
				int i; for (i = 0; i != res->presetNum; i++) TSF_FREE(res->presets[i].regions);
				TSF_FREE(res->presets);
				return 0;
			}
		}
		tsf_region_clear(&globalRegion, TSF_TRUE);

		// Zones.
		for (ppbag = hydra->pbags + pphdr->presetBagNdx, ppbagEnd = hydra->pbags + pphdr[1].presetBagNdx; ppbag != ppbagEnd; ppbag++)
		{
			struct tsf_hydra_pgen *ppgen, *ppgenEnd; struct tsf_hydra_inst *pinst; struct tsf_hydra_ibag *pibag, *pibagEnd; struct tsf_hydra_igen *pigen, *pigenEnd;
			struct tsf_region presetRegion = globalRegion;
			int hadGenInstrument = 0;

			// Generators.
			for (ppgen = hydra->pgens + ppbag->genNdx, ppgenEnd = hydra->pgens + ppbag[1].genNdx; ppgen != ppgenEnd; ppgen++)
			{
				// Instrument.
				if (ppgen->genOper == GenInstrument)
				{
					struct tsf_region instRegion;
					tsf_u16 whichInst = ppgen->genAmount.wordAmount;
					if (whichInst >= hydra->instNum) continue;

					tsf_region_clear(&instRegion, TSF_FALSE);
					pinst = &hydra->insts[whichInst];
					for (pibag = hydra->ibags + pinst->instBagNdx, pibagEnd = hydra->ibags + pinst[1].instBagNdx; pibag != pibagEnd; pibag++)
					{
						// Generators.
						struct tsf_region zoneRegion = instRegion;
						int hadSampleID = 0;
						for (pigen = hydra->igens + pibag->instGenNdx, pigenEnd = hydra->igens + pibag[1].instGenNdx; pigen != pigenEnd; pigen++)
						{
							if (pigen->genOper == GenSampleID)
							{
								struct tsf_hydra_shdr* pshdr;

								//preset region key and vel ranges are a filter for the zone regions
								if (zoneRegion.hikey < presetRegion.lokey || zoneRegion.lokey > presetRegion.hikey) continue;
								if (zoneRegion.hivel < presetRegion.lovel || zoneRegion.lovel > presetRegion.hivel) continue;
								if (presetRegion.lokey > zoneRegion.lokey) zoneRegion.lokey = presetRegion.lokey;
								if (presetRegion.hikey < zoneRegion.hikey) zoneRegion.hikey = presetRegion.hikey;
								if (presetRegion.lovel > zoneRegion.lovel) zoneRegion.lovel = presetRegion.lovel;
								if (presetRegion.hivel < zoneRegion.hivel) zoneRegion.hivel = presetRegion.hivel;

								//sum regions
								tsf_region_operator(&zoneRegion, 0, TSF_NULL, &presetRegion);

								// EG times need to be converted from timecents to seconds.
								tsf_region_envtosecs(&zoneRegion.ampenv, TSF_TRUE);
								tsf_region_envtosecs(&zoneRegion.modenv, TSF_FALSE);

								// LFO times need to be converted from timecents to seconds.
								zoneRegion.delayModLFO = (zoneRegion.delayModLFO < -11950.0f ? 0.0f : tsf_timecents2Secsf(zoneRegion.delayModLFO));
								zoneRegion.delayVibLFO = (zoneRegion.delayVibLFO < -11950.0f ? 0.0f : tsf_timecents2Secsf(zoneRegion.delayVibLFO));

								// Fixup sample positions
								pshdr = &hydra->shdrs[pigen->genAmount.wordAmount];
								zoneRegion.offset += pshdr->start;
								zoneRegion.end += pshdr->end;
								zoneRegion.loop_start += pshdr->startLoop;
								zoneRegion.loop_end += pshdr->endLoop;
								if (pshdr->endLoop > 0) zoneRegion.loop_end -= 1;
								if (zoneRegion.loop_end > fontSampleCount) zoneRegion.loop_end = fontSampleCount;
								if (zoneRegion.pitch_keycenter == -1) zoneRegion.pitch_keycenter = pshdr->originalPitch;
								zoneRegion.tune += pshdr->pitchCorrection;
								zoneRegion.sample_rate = pshdr->sampleRate;
								if (zoneRegion.end && zoneRegion.end < fontSampleCount) zoneRegion.end++;
								else zoneRegion.end = fontSampleCount;

								preset->regions[region_index] = zoneRegion;
								region_index++;
								hadSampleID = 1;
							}
							else tsf_region_operator(&zoneRegion, pigen->genOper, &pigen->genAmount, TSF_NULL);
						}

						// Handle instrument's global zone.
						if (pibag == hydra->ibags + pinst->instBagNdx && !hadSampleID)
							instRegion = zoneRegion;

						// Modulators (TODO)
						//if (ibag->instModNdx < ibag[1].instModNdx) addUnsupportedOpcode("any modulator");
					}
					hadGenInstrument = 1;
				}
				else tsf_region_operator(&presetRegion, ppgen->genOper, &ppgen->genAmount, TSF_NULL);
			}

			// Modulators (TODO)
			//if (pbag->modNdx < pbag[1].modNdx) addUnsupportedOpcode("any modulator");

			// Handle preset's global zone.
			if (ppbag == hydra->pbags + pphdr->presetBagNdx && !hadGenInstrument)
				globalRegion = presetRegion;
		}
	}
	return 1;
}

#ifdef STB_VORBIS_INCLUDE_STB_VORBIS_H
static int tsf_decode_ogg(const tsf_u8 *pSmpl, const tsf_u8 *pSmplEnd, float** pRes, tsf_u32* pResNum, tsf_u32* pResMax, tsf_u32 resInitial)
{
	float *res = *pRes, *oldres; tsf_u32 resNum = *pResNum; tsf_u32 resMax = *pResMax; stb_vorbis *v;

	// Use whatever stb_vorbis API that is available (either pull or push)
	#if !defined(STB_VORBIS_NO_PULLDATA_API) && !defined(STB_VORBIS_NO_FROMMEMORY)
	v = stb_vorbis_open_memory(pSmpl, (int)(pSmplEnd - pSmpl), TSF_NULL, TSF_NULL);
	#else
	{ int use, err; v = stb_vorbis_open_pushdata(pSmpl, (int)(pSmplEnd - pSmpl), &use, &err, TSF_NULL); pSmpl += use; }
	#endif
	if (v == TSF_NULL) return 0;

	for (;;)
	{
		float** outputs; int n_samples;

		// Decode one frame of vorbis samples with whatever stb_vorbis API that is available
		#if !defined(STB_VORBIS_NO_PULLDATA_API) && !defined(STB_VORBIS_NO_FROMMEMORY)
		n_samples = stb_vorbis_get_frame_float(v, TSF_NULL, &outputs);
		if (!n_samples) break;
		#else
		if (pSmpl >= pSmplEnd) break;
		{ int use = stb_vorbis_decode_frame_pushdata(v, pSmpl, (int)(pSmplEnd - pSmpl), TSF_NULL, &outputs, &n_samples); pSmpl += use; }
		if (!n_samples) continue;
		#endif

		// Expand our output buffer if necessary then copy over the decoded frame samples
		resNum += n_samples;
		if (resNum > resMax)
		{
			do { resMax += (resMax ? (resMax < 1048576 ? resMax : 1048576) : resInitial); } while (resNum > resMax);
			oldres = res;
			res = (float*)TSF_REALLOC(res, resMax * sizeof(float));
			if (!res) { TSF_FREE(oldres); stb_vorbis_close(v); return 0; }
		}
		TSF_MEMCPY(res + resNum - n_samples, outputs[0], n_samples * sizeof(float));
	}
	stb_vorbis_close(v);
	*pRes = res; *pResNum = resNum; *pResMax = resMax;
	return 1;
}

static int tsf_decode_sf3_samples(const void* rawBuffer, float** pFloatBuffer, unsigned int* pSmplCount, struct tsf_hydra *hydra)
{
	const tsf_u8* smplBuffer = (const tsf_u8*)rawBuffer;
	tsf_u32 smplLength = *pSmplCount, resNum = 0, resMax = 0, resInitial = (smplLength > 0x100000 ? (smplLength & ~0xFFFFF) : 65536);
	float *res = TSF_NULL, *oldres;
	int i, shdrLast = hydra->shdrNum - 1, is_sf3 = 0;
	for (i = 0; i <= shdrLast; i++)
	{
		struct tsf_hydra_shdr *shdr = &hydra->shdrs[i];
		if (shdr->sampleType & 0x30) // compression flags (sometimes Vorbis flag)
		{
			const tsf_u8 *pSmpl = smplBuffer + shdr->start, *pSmplEnd = smplBuffer + shdr->end;
			if (pSmpl + 4 > pSmplEnd || !TSF_FourCCEquals(pSmpl, "OggS"))
			{
				shdr->start = shdr->end = shdr->startLoop = shdr->endLoop = 0;
				continue;
			}

			// Fix up sample indices in shdr (end index is set after decoding)
			shdr->start = resNum;
			shdr->startLoop += resNum;
			shdr->endLoop += resNum;
			if (!tsf_decode_ogg(pSmpl, pSmplEnd, &res, &resNum, &resMax, resInitial)) { TSF_FREE(res); return 0; }
			shdr->end = resNum;
			is_sf3 = 1;
		}
		else // raw PCM sample
		{
			float *out; short *in = (short*)smplBuffer + resNum, *inEnd; tsf_u32 oldResNum = resNum;
			if (is_sf3) // Fix up sample indices in shdr
			{
				tsf_u32 fix_offset = resNum - shdr->start;
				in -= fix_offset;
				shdr->start = resNum;
				shdr->end += fix_offset;
				shdr->startLoop += fix_offset;
				shdr->endLoop += fix_offset;
			}
			inEnd = in + ((shdr->end >= shdr->endLoop ? shdr->end : shdr->endLoop) - resNum);
			if (i == shdrLast || (tsf_u8*)inEnd > (smplBuffer + smplLength)) inEnd = (short*)(smplBuffer + smplLength);
			if (inEnd <= in) continue;

			// expand our output buffer if necessary then convert the PCM data from short to float
			resNum += (tsf_u32)(inEnd - in);
			if (resNum > resMax)
			{
				do { resMax += (resMax ? (resMax < 1048576 ? resMax : 1048576) : resInitial); } while (resNum > resMax);
				oldres = res;
				res = (float*)TSF_REALLOC(res, resMax * sizeof(float));
				if (!res) { TSF_FREE(oldres); return 0; }
			}

			// Convert the samples from short to float
			for (out = res + oldResNum; in < inEnd;)
				*(out++) = (float)(*(in++) / 32767.0);
		}
	}

	// Trim the sample buffer down then return success (unless out of memory)
	if (!(*pFloatBuffer = (float*)TSF_REALLOC(res, resNum * sizeof(float)))) *pFloatBuffer = res;
	*pSmplCount = resNum;
	return (res ? 1 : 0);
}
#endif

static int tsf_load_samples(short** pShortBuffer, unsigned int* pSmplCount, struct tsf_riffchunk *chunkSmpl, struct tsf_stream* stream)
{
	*pSmplCount = chunkSmpl->size / (unsigned int)sizeof(short);
	Serial.printf("[TSF] Allocating samples: %u bytes (%u samples)... Free PSRAM: %u KB\n",
	              chunkSmpl->size, *pSmplCount, (unsigned int)(ESP.getFreePsram() / 1024));
	*pShortBuffer = (short*)TSF_MALLOC(chunkSmpl->size + 4096);
	if (!*pShortBuffer) {
		Serial.printf("[TSF] Error: Failed to allocate %u bytes in PSRAM!\n", chunkSmpl->size);
		DisplayUI::showToast(DisplayUI::isKoreanMode() ? "에러: 샘플 메모리 부족" : "Err: Sample PSRAM", 3000);
		return 0;
	}
	TSF_MEMSET((char*)(*pShortBuffer) + chunkSmpl->size, 0, 4096);
	Serial.println("[TSF] Reading sample data from LittleFS stream...");
	int readBytes = stream->read(stream->data, *pShortBuffer, chunkSmpl->size);
	Serial.printf("[TSF] Read sample stream: %d / %u bytes (Free PSRAM: %u KB)\n",
	              readBytes, chunkSmpl->size, (unsigned int)(ESP.getFreePsram() / 1024));
	if (readBytes <= 0 || (unsigned int)readBytes < chunkSmpl->size) {
		Serial.println("[TSF] Error: Incomplete sample data read from stream!");
		DisplayUI::showToast(DisplayUI::isKoreanMode() ? "에러: 샘플 읽기 실패" : "Err: Sample Read", 3000);
		return 0;
	}
	return 1;
}

static int tsf_voice_envelope_release_samples(struct tsf_voice_envelope* e, float outSampleRate)
{
	return (int)((e->parameters.release <= 0 ? TSF_FASTRELEASETIME : e->parameters.release) * outSampleRate);
}

static void tsf_voice_envelope_nextsegment(struct tsf_voice_envelope* e, short active_segment, float outSampleRate)
{
	switch (active_segment)
	{
		case TSF_SEGMENT_NONE:
			e->samplesUntilNextSegment = (int)(e->parameters.delay * outSampleRate);
			if (e->samplesUntilNextSegment > 0)
			{
				e->segment = TSF_SEGMENT_DELAY;
				e->segmentIsExponential = TSF_FALSE;
				e->level = 0.0;
				e->slope = 0.0;
				return;
			}
			/* fall through */
		case TSF_SEGMENT_DELAY:
			e->samplesUntilNextSegment = (int)(e->parameters.attack * outSampleRate);
			if (e->isAmpEnv && e->samplesUntilNextSegment < 8) e->samplesUntilNextSegment = 8; // 최소 8샘플(0.18ms) 안티클릭 소프트 어택 보장 (틱 노이즈 100% 제거)
			if (e->samplesUntilNextSegment > 0)
			{
				if (!e->isAmpEnv)
				{
					//mod env attack duration scales with velocity (velocity of 1 is full duration, max velocity is 0.125 times duration)
					e->samplesUntilNextSegment = (int)(e->parameters.attack * ((145 - e->midiVelocity) / 144.0f) * outSampleRate);
				}
				e->segment = TSF_SEGMENT_ATTACK;
				e->segmentIsExponential = TSF_FALSE;
				e->level = 0.0f;
				e->slope = 1.0f / e->samplesUntilNextSegment;
				return;
			}
			/* fall through */
		case TSF_SEGMENT_ATTACK:
			e->samplesUntilNextSegment = (int)(e->parameters.hold * outSampleRate);
			if (e->samplesUntilNextSegment > 0)
			{
				e->segment = TSF_SEGMENT_HOLD;
				e->segmentIsExponential = TSF_FALSE;
				e->level = 1.0f;
				e->slope = 0.0f;
				return;
			}
			/* fall through */
		case TSF_SEGMENT_HOLD:
			e->samplesUntilNextSegment = (int)(e->parameters.decay * outSampleRate);
			if (e->samplesUntilNextSegment > 0)
			{
				e->segment = TSF_SEGMENT_DECAY;
				e->level = 1.0f;
				if (e->isAmpEnv)
				{
					// I don't truly understand this; just following what LinuxSampler does.
					float mysterySlope = -9.226f / e->samplesUntilNextSegment;
					e->slope = TSF_EXPF(mysterySlope);
					e->segmentIsExponential = TSF_TRUE;
					if (e->parameters.sustain > 0.0f)
					{
						// Again, this is following LinuxSampler's example, which is similar to
						// SF2-style decay, where "decay" specifies the time it would take to
						// get to zero, not to the sustain level.  The SFZ spec is not that
						// specific about what "decay" means, so perhaps it's really supposed
						// to specify the time to reach the sustain level.
						e->samplesUntilNextSegment = (int)(TSF_LOGF(e->parameters.sustain) / mysterySlope);
					}
				}
				else
				{
					e->slope = -1.0f / e->samplesUntilNextSegment;
					e->samplesUntilNextSegment = (int)(e->parameters.decay * (1.0f - e->parameters.sustain) * outSampleRate);
					e->segmentIsExponential = TSF_FALSE;
				}
				return;
			}
			/* fall through */
		case TSF_SEGMENT_DECAY:
			e->segment = TSF_SEGMENT_SUSTAIN;
			e->level = e->parameters.sustain;
			e->slope = 0.0f;
			e->samplesUntilNextSegment = 0x7FFFFFFF;
			e->segmentIsExponential = TSF_FALSE;
			return;
		case TSF_SEGMENT_SUSTAIN:
			e->segment = TSF_SEGMENT_RELEASE;
			e->samplesUntilNextSegment = tsf_voice_envelope_release_samples(e, outSampleRate);
			if (e->isAmpEnv)
			{
				// I don't truly understand this; just following what LinuxSampler does.
				float mysterySlope = -9.226f / e->samplesUntilNextSegment;
				e->slope = TSF_EXPF(mysterySlope);
				e->segmentIsExponential = TSF_TRUE;
			}
			else
			{
				e->slope = -e->level / e->samplesUntilNextSegment;
				e->segmentIsExponential = TSF_FALSE;
			}
			return;
		case TSF_SEGMENT_RELEASE:
		default:
			e->segment = TSF_SEGMENT_DONE;
			e->segmentIsExponential = TSF_FALSE;
			e->level = e->slope = 0.0f;
			e->samplesUntilNextSegment = 0x7FFFFFF;
	}
}

static void tsf_voice_envelope_setup(struct tsf_voice_envelope* e, struct tsf_envelope* new_parameters, int midiNoteNumber, short midiVelocity, TSF_BOOL isAmpEnv, float outSampleRate)
{
	e->parameters = *new_parameters;
	if (e->parameters.keynumToHold)
	{
		e->parameters.hold += e->parameters.keynumToHold * (60.0f - midiNoteNumber);
		e->parameters.hold = (e->parameters.hold < -10000.0f ? 0.0f : tsf_timecents2Secsf(e->parameters.hold));
	}
	if (e->parameters.keynumToDecay)
	{
		e->parameters.decay += e->parameters.keynumToDecay * (60.0f - midiNoteNumber);
		e->parameters.decay = (e->parameters.decay < -10000.0f ? 0.0f : tsf_timecents2Secsf(e->parameters.decay));
	}
	e->midiVelocity = midiVelocity;
	e->isAmpEnv = isAmpEnv;
	tsf_voice_envelope_nextsegment(e, TSF_SEGMENT_NONE, outSampleRate);
}

static void tsf_voice_envelope_process(struct tsf_voice_envelope* e, int numSamples, float outSampleRate)
{
	if (e->slope)
	{
		if (e->segmentIsExponential) e->level *= TSF_POWF(e->slope, (float)numSamples);
		else e->level += (e->slope * numSamples);
	}
	// [수정] 드럼 등 릴리즈 단계가 아니더라도 볼륨이 0에 도달하면 즉시 보이스 종료 (유령 보이스 누적 차단!)
	if (e->level < 0.0001f && (e->segment >= TSF_SEGMENT_DECAY || e->parameters.sustain <= 0.001f))
	{
		e->segment = TSF_SEGMENT_DONE;
		e->level = e->slope = 0.0f;
		e->samplesUntilNextSegment = 0x7FFFFFF;
		return;
	}
	if ((e->samplesUntilNextSegment -= numSamples) <= 0)
		tsf_voice_envelope_nextsegment(e, e->segment, outSampleRate);
}

static void tsf_voice_lowpass_setup(struct tsf_voice_lowpass* e, float Fc)
{
    if (Fc > 0.45f) Fc = 0.45f; // [안전 클램핑] 나이퀴스트 근접 시 계수 발진 원천 차단
    if (Fc < 0.001f) Fc = 0.001f;

    float K = TSF_TANF(3.14159265358979323846f * Fc), KK = K * K;
    float norm = 1.0f / (1.0f + K * e->QInv + KK);
    e->a0 = KK * norm;
    e->a1 = 2.0f * e->a0;
    e->b1 = 2.0f * (KK - 1.0f) * norm;
    e->b2 = (1.0f - K * e->QInv + KK) * norm;
}

static inline float tsf_voice_lowpass_process(struct tsf_voice_lowpass* e, float In)
{
	float Out = In * e->a0 + e->z1;
	e->z1 = In * e->a1 + e->z2 - e->b1 * Out;
	e->z2 = In * e->a0 - e->b2 * Out;
	return Out;
}

static void tsf_voice_lfo_setup(struct tsf_voice_lfo* e, float delay, int freqCents, float outSampleRate)
{
	e->samplesUntil = (int)(delay * outSampleRate);
	e->delta = (4.0f * tsf_cents2Hertz((float)freqCents) / outSampleRate);
	e->level = 0.0f;
	int fadeSamples = (int)(0.25f * outSampleRate); // 0.25초 부드러운 아날로그 비브라토 페이드인
	e->fadeStep = (fadeSamples > 0 ? (1.0f / (float)fadeSamples) : 1.0f);
	e->currentFade = 0.0f;
}

static void tsf_voice_lfo_process(struct tsf_voice_lfo* e, int blockSamples)
{
	if (e->samplesUntil > blockSamples) { e->samplesUntil -= blockSamples; return; }
	if (e->samplesUntil > 0) { blockSamples -= e->samplesUntil; e->samplesUntil = 0; }
	if (e->currentFade < 1.0f)
	{
		e->currentFade += e->fadeStep * blockSamples;
		if (e->currentFade > 1.0f) e->currentFade = 1.0f;
	}
	e->level += e->delta * blockSamples;
	if      (e->level >  1.0f) { e->delta = -e->delta; e->level =  2.0f - e->level; }
	else if (e->level < -1.0f) { e->delta = -e->delta; e->level = -2.0f - e->level; }
}

static void tsf_voice_kill(struct tsf_voice* v)
{
	v->playingPreset = -1;
	v->playingChannel = -1;
	v->playingKey = -1;
	v->playIndex = 0;
	v->heldSustain = 0;
	v->heldSostenuto = 0;
	v->lowpass.z1 = 0.0f;
	v->lowpass.z2 = 0.0f;
	v->ampenv.segment = TSF_SEGMENT_DONE;
	v->ampenv.level = 0.0f;
	v->ampenv.slope = 0.0f;
	v->modenv.segment = TSF_SEGMENT_DONE;
	v->modenv.level = 0.0f;
	v->modenv.slope = 0.0f;
}

static void tsf_voice_end(tsf* f, struct tsf_voice* v)
{
	// if maxVoiceNum is set, assume that voice rendering and note queuing are on separate threads
	// so to minimize the chance that voice rendering would advance the segment at the same time
	// we just do it twice here and hope that it sticks
	int repeats = (f->maxVoiceNum ? 2 : 1);
	while (repeats--)
	{
		tsf_voice_envelope_nextsegment(&v->ampenv, TSF_SEGMENT_SUSTAIN, f->outSampleRate);
		tsf_voice_envelope_nextsegment(&v->modenv, TSF_SEGMENT_SUSTAIN, f->outSampleRate);
		if (v->region->loop_mode == TSF_LOOPMODE_SUSTAIN)
		{
			// Continue playing, but stop looping.
			v->loopEnd = v->loopStart;
		}
	}
}

static void tsf_voice_endquick(tsf* f, struct tsf_voice* v)
{
	// if maxVoiceNum is set, assume that voice rendering and note queuing are on separate threads
	// so to minimize the chance that voice rendering would advance the segment at the same time
	// we just do it twice here and hope that it sticks
	int repeats = (f->maxVoiceNum ? 2 : 1);
	while (repeats--)
	{
		v->ampenv.parameters.release = 0.0f; tsf_voice_envelope_nextsegment(&v->ampenv, TSF_SEGMENT_SUSTAIN, f->outSampleRate);
		v->modenv.parameters.release = 0.0f; tsf_voice_envelope_nextsegment(&v->modenv, TSF_SEGMENT_SUSTAIN, f->outSampleRate);
	}
}

static void tsf_voice_calcpitchratio(struct tsf_voice* v, float pitchShift, float outSampleRate)
{
	float note = (float)v->playingKey + (float)v->region->transpose + (float)v->region->tune / 100.0f;
	float adjustedPitch = (float)v->region->pitch_keycenter + (note - (float)v->region->pitch_keycenter) * ((float)v->region->pitch_keytrack / 100.0f);
	if (pitchShift) adjustedPitch += pitchShift;
	v->pitchInputTimecents = adjustedPitch * 100.0f;
	v->pitchOutputFactor = (float)v->region->sample_rate / (tsf_timecents2Secsf((float)v->region->pitch_keycenter * 100.0f) * outSampleRate);
}

static void tsf_voice_render(tsf* f, struct tsf_voice* v, float* outputBuffer, int numSamples)
{
	struct tsf_region* region = v->region;
	struct tsf_channel* curChan = (f->channels && v->playingChannel < f->channels->channelNum) ? &f->channels->channels[v->playingChannel] : TSF_NULL;
	float chanModWheel = (curChan ? (float)curChan->modWheel : 0.0f);
	float chanModDepth = (curChan && curChan->modDepth > 0.0f ? curChan->modDepth : 50.0f);
	TSF_BOOL hasModWheel = (chanModWheel > 0.0f);

	short* input = f->fontSamples;
	float* outL = outputBuffer;
	float* outR = (f->outputmode == TSF_STEREO_UNWEAVED ? outL + numSamples : TSF_NULL);

	// Cache some values, to give them at least some chance of ending up in registers.
	TSF_BOOL updateModEnv = (region->modEnvToPitch || region->modEnvToFilterFc);
	TSF_BOOL updateModLFO = (v->modlfo.delta && (region->modLfoToPitch || region->modLfoToFilterFc || region->modLfoToVolume));
	TSF_BOOL updateVibLFO = ((v->viblfo.delta != 0.0f) && (region->vibLfoToPitch || hasModWheel));
	TSF_BOOL isLooping    = (v->loopStart < v->loopEnd);
	unsigned int tmpLoopStart = v->loopStart, tmpLoopEnd = v->loopEnd;
	uint64_t tmpSampleEndFP = ((uint64_t)region->end) << 32;
	uint64_t tmpLoopStartFP = ((uint64_t)tmpLoopStart) << 32;
	uint64_t tmpLoopEndFP = ((uint64_t)tmpLoopEnd + 1) << 32;
	uint64_t tmpLoopLenFP = ((uint64_t)(tmpLoopEnd - tmpLoopStart + 1)) << 32;
	uint64_t tmpSourceSamplePosition = v->sourceSamplePosition;
	struct tsf_voice_lowpass tmpLowpass = v->lowpass;

	TSF_BOOL dynamicLowpass = (region->modLfoToFilterFc || region->modEnvToFilterFc);
	float tmpSampleRate = f->outSampleRate, tmpInitialFilterFc, tmpModLfoToFilterFc, tmpModEnvToFilterFc;

	TSF_BOOL dynamicPitchRatio = (region->modLfoToPitch || region->modEnvToPitch || region->vibLfoToPitch || hasModWheel);
	uint64_t pitchRatioFP;
	float tmpModLfoToPitch, tmpVibLfoToPitch, tmpModEnvToPitch;

	TSF_BOOL dynamicGain = (region->modLfoToVolume != 0);
	float noteGain = 0, tmpModLfoToVolume;

	if (dynamicLowpass) tmpInitialFilterFc = (float)v->lowpassCutoffCents, tmpModLfoToFilterFc = (float)region->modLfoToFilterFc, tmpModEnvToFilterFc = (float)region->modEnvToFilterFc;
	else tmpInitialFilterFc = 0, tmpModLfoToFilterFc = 0, tmpModEnvToFilterFc = 0;

	if (dynamicPitchRatio) pitchRatioFP = 0, tmpModLfoToPitch = (float)region->modLfoToPitch, tmpVibLfoToPitch = (float)region->vibLfoToPitch, tmpModEnvToPitch = (float)region->modEnvToPitch;
	else pitchRatioFP = (uint64_t)((double)(tsf_timecents2Secsf(v->pitchInputTimecents) * v->pitchOutputFactor) * 4294967296.0), tmpModLfoToPitch = 0, tmpVibLfoToPitch = 0, tmpModEnvToPitch = 0;

	if (dynamicGain) tmpModLfoToVolume = (float)region->modLfoToVolume * 0.1f;
	else noteGain = tsf_decibelsToGain(v->noteGainDB), tmpModLfoToVolume = 0;

	while (numSamples)
	{
		float gainMono, gainLeft, gainRight;
		int blockSamples = (numSamples > TSF_RENDER_EFFECTSAMPLEBLOCK ? TSF_RENDER_EFFECTSAMPLEBLOCK : numSamples);
		numSamples -= blockSamples;

		if (dynamicLowpass)
		{
    		float fres = tmpInitialFilterFc + v->modlfo.level * tmpModLfoToFilterFc + v->modenv.level * tmpModEnvToFilterFc;
    		if (fres < 400.0f) fres = 400.0f;
    		float lowpassFc = tsf_cents2Hertz(fres) / tmpSampleRate;
    
    		if (lowpassFc > 0.45f) lowpassFc = 0.45f; // 나이퀴스트 발진 차단

    		if (fres >= 13000.0f || lowpassFc >= 0.44f) {
        		if (tmpLowpass.active) {
            		// 필터가 꺼지는 순간 잔류 에너지를 0으로 초기화하여 재진입 시 폭주 방지
            		tmpLowpass.z1 = 0.0f;
            		tmpLowpass.z2 = 0.0f;
            		tmpLowpass.active = TSF_FALSE;
        		}
    		} else {
        		tmpLowpass.active = TSF_TRUE;
        		tsf_voice_lowpass_setup(&tmpLowpass, lowpassFc);
    		}
		}

		if (dynamicPitchRatio)
		{
    		float vibFade = (v->viblfo.currentFade > 0.0f ? v->viblfo.currentFade : 1.0f);
    		float modWheelPitch = (hasModWheel ? ((chanModWheel / 127.0f) * chanModDepth * vibFade * v->viblfo.level) : 0.0f);
    		float pr = tsf_timecents2Secsf(v->pitchInputTimecents + (v->modlfo.level * tmpModLfoToPitch + (v->viblfo.level * vibFade) * tmpVibLfoToPitch + v->modenv.level * tmpModEnvToPitch + modWheelPitch)) * v->pitchOutputFactor;

    		// [추가] 주파수 배율 안전 상/하한 제한 (0.001배 ~ 16.0배)
    		if (pr > 16.0f) pr = 16.0f;
    		if (pr < 0.001f) pr = 0.001f;

    		pitchRatioFP = (uint64_t)((double)pr * 4294967296.0);
		}

		if (dynamicGain)
			noteGain = tsf_decibelsToGain(v->noteGainDB + (v->modlfo.level * tmpModLfoToVolume));

		gainMono = noteGain * v->ampenv.level * (1.0f / 32767.0f);

		// Update EG.
		tsf_voice_envelope_process(&v->ampenv, blockSamples, tmpSampleRate);
		if (updateModEnv) tsf_voice_envelope_process(&v->modenv, blockSamples, tmpSampleRate);

		// Update LFOs.
		if (updateModLFO) tsf_voice_lfo_process(&v->modlfo, blockSamples);
		if (updateVibLFO) tsf_voice_lfo_process(&v->viblfo, blockSamples);

		switch (f->outputmode)
		{
			case TSF_STEREO_INTERLEAVED:
				gainLeft = gainMono * v->panFactorLeft, gainRight = gainMono * v->panFactorRight;
				if (tmpLowpass.active)
				{
					while (blockSamples-- && tmpSourceSamplePosition < tmpSampleEndFP)
					{
						unsigned int pos = (unsigned int)(tmpSourceSamplePosition >> 32);
						unsigned int nextPos = (pos >= tmpLoopEnd && isLooping ? tmpLoopStart : pos + 1);
						float alpha = (float)(uint32_t)(tmpSourceSamplePosition & 0xFFFFFFFF) * (1.0f / 4294967296.0f);
						float s0 = (float)input[pos];
						float s1 = (float)input[nextPos];
						float val = s0 + alpha * (s1 - s0);

						val = tsf_voice_lowpass_process(&tmpLowpass, val);

						*outL++ += val * gainLeft;
						*outL++ += val * gainRight;

						tmpSourceSamplePosition += pitchRatioFP;
						if (isLooping && tmpSourceSamplePosition >= tmpLoopEndFP) {
							// 루프 길이가 극도로 짧아 단번에 여러 주기를 건너뛰더라도 정확하게 루프 안으로 복귀
							tmpSourceSamplePosition = tmpLoopStartFP + ((tmpSourceSamplePosition - tmpLoopStartFP) % tmpLoopLenFP);
						}
					}
				}
				else
				{
					while (blockSamples-- && tmpSourceSamplePosition < tmpSampleEndFP)
					{
						unsigned int pos = (unsigned int)(tmpSourceSamplePosition >> 32);
						unsigned int nextPos = (pos >= tmpLoopEnd && isLooping ? tmpLoopStart : pos + 1);
						float alpha = (float)(uint32_t)(tmpSourceSamplePosition & 0xFFFFFFFF) * (1.0f / 4294967296.0f);
						float s0 = (float)input[pos];
						float s1 = (float)input[nextPos];
						float val = s0 + alpha * (s1 - s0);

						*outL++ += val * gainLeft;
						*outL++ += val * gainRight;

						tmpSourceSamplePosition += pitchRatioFP;
						if (isLooping && tmpSourceSamplePosition >= tmpLoopEndFP) {
							// 루프 길이가 극도로 짧아 단번에 여러 주기를 건너뛰더라도 정확하게 루프 안으로 복귀
							tmpSourceSamplePosition = tmpLoopStartFP + ((tmpSourceSamplePosition - tmpLoopStartFP) % tmpLoopLenFP);
						}
					}
				}
				break;

			case TSF_STEREO_UNWEAVED:
				gainLeft = gainMono * v->panFactorLeft, gainRight = gainMono * v->panFactorRight;
				if (tmpLowpass.active)
				{
					while (blockSamples-- && tmpSourceSamplePosition < tmpSampleEndFP)
					{
						unsigned int pos = (unsigned int)(tmpSourceSamplePosition >> 32);
						unsigned int nextPos = (pos >= tmpLoopEnd && isLooping ? tmpLoopStart : pos + 1);
						float alpha = (float)(uint32_t)(tmpSourceSamplePosition & 0xFFFFFFFF) * (1.0f / 4294967296.0f);
						float s0 = (float)input[pos];
						float s1 = (float)input[nextPos];
						float val = s0 + alpha * (s1 - s0);

						val = tsf_voice_lowpass_process(&tmpLowpass, val);

						*outL++ += val * gainLeft;
						*outR++ += val * gainRight;

						tmpSourceSamplePosition += pitchRatioFP;
						if (isLooping && tmpSourceSamplePosition >= tmpLoopEndFP) {
							// 루프 길이가 극도로 짧아 단번에 여러 주기를 건너뛰더라도 정확하게 루프 안으로 복귀
							tmpSourceSamplePosition = tmpLoopStartFP + ((tmpSourceSamplePosition - tmpLoopStartFP) % tmpLoopLenFP);
						}
					}
				}
				else
				{
					while (blockSamples-- && tmpSourceSamplePosition < tmpSampleEndFP)
					{
						unsigned int pos = (unsigned int)(tmpSourceSamplePosition >> 32);
						unsigned int nextPos = (pos >= tmpLoopEnd && isLooping ? tmpLoopStart : pos + 1);
						float alpha = (float)(uint32_t)(tmpSourceSamplePosition & 0xFFFFFFFF) * (1.0f / 4294967296.0f);
						float s0 = (float)input[pos];
						float s1 = (float)input[nextPos];
						float val = s0 + alpha * (s1 - s0);

						*outL++ += val * gainLeft;
						*outR++ += val * gainRight;

						tmpSourceSamplePosition += pitchRatioFP;
						if (isLooping && tmpSourceSamplePosition >= tmpLoopEndFP) {
							// 루프 길이가 극도로 짧아 단번에 여러 주기를 건너뛰더라도 정확하게 루프 안으로 복귀
							tmpSourceSamplePosition = tmpLoopStartFP + ((tmpSourceSamplePosition - tmpLoopStartFP) % tmpLoopLenFP);
						}
					}
				}
				break;

			case TSF_MONO:
				if (tmpLowpass.active)
				{
					while (blockSamples-- && tmpSourceSamplePosition < tmpSampleEndFP)
					{
						unsigned int pos = (unsigned int)(tmpSourceSamplePosition >> 32);
						unsigned int nextPos = (pos >= tmpLoopEnd && isLooping ? tmpLoopStart : pos + 1);
						float alpha = (float)(uint32_t)(tmpSourceSamplePosition & 0xFFFFFFFF) * (1.0f / 4294967296.0f);
						float s0 = (float)input[pos];
						float s1 = (float)input[nextPos];
						float val = s0 + alpha * (s1 - s0);

						val = tsf_voice_lowpass_process(&tmpLowpass, val);

						*outL++ += val * gainMono;

						tmpSourceSamplePosition += pitchRatioFP;
						if (isLooping && tmpSourceSamplePosition >= tmpLoopEndFP) {
							// 루프 길이가 극도로 짧아 단번에 여러 주기를 건너뛰더라도 정확하게 루프 안으로 복귀
							tmpSourceSamplePosition = tmpLoopStartFP + ((tmpSourceSamplePosition - tmpLoopStartFP) % tmpLoopLenFP);
						}
					}
				}
				else
				{
					while (blockSamples-- && tmpSourceSamplePosition < tmpSampleEndFP)
					{
						unsigned int pos = (unsigned int)(tmpSourceSamplePosition >> 32);
						unsigned int nextPos = (pos >= tmpLoopEnd && isLooping ? tmpLoopStart : pos + 1);
						float alpha = (float)(uint32_t)(tmpSourceSamplePosition & 0xFFFFFFFF) * (1.0f / 4294967296.0f);
						float s0 = (float)input[pos];
						float s1 = (float)input[nextPos];
						float val = s0 + alpha * (s1 - s0);

						*outL++ += val * gainMono;

						tmpSourceSamplePosition += pitchRatioFP;
						if (isLooping && tmpSourceSamplePosition >= tmpLoopEndFP) {
							// 루프 길이가 극도로 짧아 단번에 여러 주기를 건너뛰더라도 정확하게 루프 안으로 복귀
							tmpSourceSamplePosition = tmpLoopStartFP + ((tmpSourceSamplePosition - tmpLoopStartFP) % tmpLoopLenFP);
						}
					}
				}
				break;
		}

		if (tmpSourceSamplePosition >= tmpSampleEndFP || v->ampenv.segment == TSF_SEGMENT_DONE || (v->ampenv.segment >= TSF_SEGMENT_RELEASE && v->ampenv.level < 0.00005f))
		{
			tsf_voice_kill(v);
			return;
		}
	}

	v->sourceSamplePosition = tmpSourceSamplePosition;
	if (tmpLowpass.active || dynamicLowpass) v->lowpass = tmpLowpass;
}

TSFDEF tsf* tsf_load(struct tsf_stream* stream)
{
	tsf* res = TSF_NULL;
	struct tsf_riffchunk chunkHead;
	struct tsf_riffchunk chunkList;
	struct tsf_hydra hydra;
	void* rawBuffer = TSF_NULL;
	short* shortBuffer = TSF_NULL;
	tsf_u32 smplCount = 0;

	Serial.printf("[TSF] Starting tsf_load... Free PSRAM: %u KB, Free Heap: %u KB\n",
	              (unsigned int)(ESP.getFreePsram() / 1024), (unsigned int)(ESP.getFreeHeap() / 1024));

	if (!tsf_riffchunk_read(TSF_NULL, &chunkHead, stream) || !TSF_FourCCEquals(chunkHead.id, "sfbk"))
	{
		Serial.printf("[TSF] Error: Invalid SF2 Header! ID: %c%c%c%c, Size: %u\n",
		              chunkHead.id[0], chunkHead.id[1], chunkHead.id[2], chunkHead.id[3], chunkHead.size);
		DisplayUI::showToast(DisplayUI::isKoreanMode() ? "에러: 잘못된 폰트 헤더" : "Err: Bad SF2 Header", 3000);
		return res;
	}
	Serial.printf("[TSF] Valid SF2 Header found! Total File Size: %u bytes\n", chunkHead.size);

	// Read hydra and locate sample data.
	TSF_MEMSET(&hydra, 0, sizeof(hydra));
	while (tsf_riffchunk_read(&chunkHead, &chunkList, stream))
	{
		Serial.printf("[TSF] Found LIST Chunk: %c%c%c%c, Size: %u\n",
		              chunkList.id[0], chunkList.id[1], chunkList.id[2], chunkList.id[3], chunkList.size);
		struct tsf_riffchunk chunk;
		if (TSF_FourCCEquals(chunkList.id, "pdta"))
		{
			while (tsf_riffchunk_read(&chunkList, &chunk, stream))
			{
				#define HandleChunk(chunkName) (TSF_FourCCEquals(chunk.id, #chunkName) && !(chunk.size % chunkName##SizeInFile)) \
					{ \
						int num = chunk.size / chunkName##SizeInFile, i; \
						hydra.chunkName##Num = num; \
						hydra.chunkName##s = (struct tsf_hydra_##chunkName*)TSF_MALLOC(num ? (num * sizeof(struct tsf_hydra_##chunkName)) : 1); \
						Serial.printf("[TSF]   pdta chunk: " #chunkName " (%u bytes, %d items, ptr=%p)\n", chunk.size, num, hydra.chunkName##s); \
						if (!hydra.chunkName##s) { DisplayUI::showToast(DisplayUI::isKoreanMode() ? "에러: 프리셋 메모리 부족" : "Err: pdta OOM", 3000); goto out_of_memory; } \
						if (chunk.size > 0) { \
							uint8_t* rawChunk = (uint8_t*)TSF_MALLOC(chunk.size); \
							if (!rawChunk || !stream->read(stream->data, rawChunk, chunk.size)) { \
								TSF_FREE(rawChunk); \
								DisplayUI::showToast(DisplayUI::isKoreanMode() ? "에러: 프리셋 읽기 실패" : "Err: Read " #chunkName, 3000); \
								goto out_of_memory; \
							} \
							struct tsf_stream_memory memStream = { (const char*)rawChunk, chunk.size, 0 }; \
							struct tsf_stream subStream = { &memStream, (int(*)(void*,void*,unsigned int))&tsf_stream_memory_read, (int(*)(void*,unsigned int))&tsf_stream_memory_skip }; \
							for (i = 0; i < num; ++i) { \
								tsf_hydra_read_##chunkName(&hydra.chunkName##s[i], &subStream); \
							} \
							TSF_FREE(rawChunk); \
						} \
						if (chunk.size & 1) stream->skip(stream->data, 1); \
					}
				enum
				{
					phdrSizeInFile = 38, pbagSizeInFile =  4, pmodSizeInFile = 10,
					pgenSizeInFile =  4, instSizeInFile = 22, ibagSizeInFile =  4,
					imodSizeInFile = 10, igenSizeInFile =  4, shdrSizeInFile = 46
				};
				if      HandleChunk(phdr) else if HandleChunk(pbag) else if HandleChunk(pmod)
				else if HandleChunk(pgen) else if HandleChunk(inst) else if HandleChunk(ibag)
				else if HandleChunk(imod) else if HandleChunk(igen) else if HandleChunk(shdr)
				else { Serial.printf("[TSF]   pdta skip unknown subchunk: %c%c%c%c (%u bytes)\n", chunk.id[0], chunk.id[1], chunk.id[2], chunk.id[3], chunk.size); stream->skip(stream->data, (chunk.size + 1) & ~1); }
				#undef HandleChunk
			}
		}
		else if (TSF_FourCCEquals(chunkList.id, "sdta"))
		{
			while (tsf_riffchunk_read(&chunkList, &chunk, stream))
			{
				Serial.printf("[TSF]   sdta subchunk: %c%c%c%c, Size: %u\n", chunk.id[0], chunk.id[1], chunk.id[2], chunk.id[3], chunk.size);
				if (TSF_FourCCEquals(chunk.id, "smpl") && !shortBuffer && chunk.size >= sizeof(short))
				{
					if (!tsf_load_samples(&shortBuffer, &smplCount, &chunk, stream)) { Serial.println("[TSF] Error: tsf_load_samples failed!"); goto out_of_memory; }
					if (chunk.size & 1) stream->skip(stream->data, 1);
				}
				else stream->skip(stream->data, (chunk.size + 1) & ~1);
			}
		}
		else {
			Serial.printf("[TSF] Skipping LIST Chunk: %c%c%c%c (%u bytes)\n", chunkList.id[0], chunkList.id[1], chunkList.id[2], chunkList.id[3], chunkList.size);
			stream->skip(stream->data, (chunkList.size + 1) & ~1);
		}
	}
	if (!hydra.phdrs || !hydra.pbags || !hydra.pgens || !hydra.insts || !hydra.ibags || !hydra.igens || !hydra.shdrs)
	{
		Serial.printf("[TSF] Error: Incomplete hydra headers! phdrs=%p, pbags=%p, pgens=%p, insts=%p, ibags=%p, igens=%p, shdrs=%p\n",
		              hydra.phdrs, hydra.pbags, hydra.pgens, hydra.insts, hydra.ibags, hydra.igens, hydra.shdrs);
		char errBuf[32];
		snprintf(errBuf, sizeof(errBuf), "Miss:%s%s%s%s",
		         !hydra.phdrs ? " phdr" : "",
		         !hydra.insts ? " inst" : "",
		         !hydra.igens ? " igen" : "",
		         !hydra.shdrs ? " shdr" : "");
		DisplayUI::showToast(errBuf, 4000);
	}
	else if (!shortBuffer)
	{
		Serial.println("[TSF] Error: No sample data loaded!");
		DisplayUI::showToast(DisplayUI::isKoreanMode() ? "에러: 샘플 데이터 없음" : "Err: No Samples", 3000);
	}
	else
	{
		Serial.println("[TSF] Allocating main TSF struct and loading presets...");
		res = (tsf*)TSF_MALLOC(sizeof(tsf));
		if (res) TSF_MEMSET(res, 0, sizeof(tsf));
		if (!res || !tsf_load_presets(res, &hydra, smplCount)) {
			Serial.println("[TSF] Error: tsf_load_presets failed!");
			DisplayUI::showToast(DisplayUI::isKoreanMode() ? "에러: 프리셋 메모리 부족" : "Err: Presets OOM", 3000);
			goto out_of_memory;
		}
		res->outSampleRate = 44100.0f;
		res->fontSamples = shortBuffer;
		res->mt32StdDrumIndex = tsf_get_presetindex(res, 128, 0);
		res->mt32CmDrumIndex = tsf_get_presetindex(res, 128, 127);
		shortBuffer = TSF_NULL; // don't free below
		Serial.printf("[TSF] Successfully loaded SF2! Presets: %d, Samples: %u, Free PSRAM: %u KB\n",
		              res->presetNum, smplCount, (unsigned int)(ESP.getFreePsram() / 1024));
	}
	if (0)
	{
		out_of_memory:
		Serial.println("[TSF] Error: OUT OF MEMORY triggered!");
		TSF_FREE(res);
		res = TSF_NULL;
	}
	TSF_FREE(hydra.phdrs); TSF_FREE(hydra.pbags); TSF_FREE(hydra.pmods);
	TSF_FREE(hydra.pgens); TSF_FREE(hydra.insts); TSF_FREE(hydra.ibags);
	TSF_FREE(hydra.imods); TSF_FREE(hydra.igens); TSF_FREE(hydra.shdrs);
	TSF_FREE(rawBuffer);   TSF_FREE(shortBuffer);
	return res;
}

TSFDEF tsf* tsf_copy(tsf* f)
{
	tsf* res;
	if (!f) return TSF_NULL;
	if (!f->refCount)
	{
		f->refCount = (int*)TSF_MALLOC(sizeof(int));
		if (!f->refCount) return TSF_NULL;
		*f->refCount = 1;
	}
	res = (tsf*)TSF_MALLOC(sizeof(tsf));
	if (!res) return TSF_NULL;
	TSF_MEMCPY(res, f, sizeof(tsf));
	res->voices = TSF_NULL;
	res->voiceNum = 0;
	res->channels = TSF_NULL;
	(*res->refCount)++;
	return res;
}

TSFDEF void tsf_close(tsf* f)
{
	if (!f) return;
	if (!f->refCount || !--(*f->refCount))
	{
		struct tsf_preset *preset = f->presets, *presetEnd = preset + f->presetNum;
		for (; preset != presetEnd; preset++) TSF_FREE(preset->regions);
		TSF_FREE(f->presets);
		TSF_FREE(f->fontSamples);
		TSF_FREE(f->refCount);
	}
	TSF_FREE(f->channels);
	TSF_FREE(f->voices);
	TSF_FREE(f);
}

TSFDEF void tsf_reset(tsf* f)
{
	struct tsf_voice *v = f->voices, *vEnd = v + f->voiceNum;
	for (; v != vEnd; v++)
		if (v->playingPreset != -1 && (v->ampenv.segment < TSF_SEGMENT_RELEASE || v->ampenv.parameters.release))
			tsf_voice_endquick(f, v);
	if (f->channels) { TSF_FREE(f->channels); f->channels = TSF_NULL; }
}

TSFDEF int tsf_get_presetindex(const tsf* f, int bank, int preset_number)
{
	const struct tsf_preset *presets;
	int i, iMax;
	for (presets = f->presets, i = 0, iMax = f->presetNum; i < iMax; i++)
		if (presets[i].preset == preset_number && presets[i].bank == bank)
			return i;
	return -1;
}

TSFDEF int tsf_get_presetcount(const tsf* f)
{
	return f->presetNum;
}

TSFDEF const char* tsf_get_presetname(const tsf* f, int preset)
{
	return (preset < 0 || preset >= f->presetNum ? TSF_NULL : f->presets[preset].presetName);
}

TSFDEF const char* tsf_bank_get_presetname(const tsf* f, int bank, int preset_number)
{
	return tsf_get_presetname(f, tsf_get_presetindex(f, bank, preset_number));
}

TSFDEF void tsf_set_output(tsf* f, enum TSFOutputMode outputmode, int samplerate, float global_gain_db)
{
	f->outputmode = outputmode;
	f->outSampleRate = (float)(samplerate >= 1 ? samplerate : 44100.0f);
	f->globalGainDB = global_gain_db;
}

TSFDEF void tsf_set_volume(tsf* f, float global_volume)
{
	f->globalGainDB = -6.0f + (global_volume <= 0.0001f ? -100.0f : (20.0f * TSF_LOG10F(global_volume)));
}

TSFDEF int tsf_set_max_voices(tsf* f, int max_voices)
{
	int i = f->voiceNum;
	int newVoiceNum = (f->voiceNum > max_voices ? f->voiceNum : max_voices);
	struct tsf_voice *newVoices = (struct tsf_voice*)TSF_REALLOC(f->voices, newVoiceNum * sizeof(struct tsf_voice));
	if (!newVoices) return 0;
	f->voices = newVoices;
	f->voiceNum = f->maxVoiceNum = newVoiceNum;
	for (; i < max_voices; i++)
		f->voices[i].playingPreset = -1;
	return 1;
}

TSFDEF int tsf_note_on(tsf* f, int preset_index, int key, float vel)
{
	short midiVelocity = (short)(vel * 127);
	unsigned int voicePlayIndex;
	struct tsf_region *region, *regionEnd;

	if (preset_index < 0 || preset_index >= f->presetNum) return 1;
	if (vel <= 0.0f) { tsf_note_off(f, preset_index, key); return 1; }

	// Play all matching regions.
	voicePlayIndex = f->voicePlayIndex++;
	for (region = f->presets[preset_index].regions, regionEnd = region + f->presets[preset_index].regionNum; region != regionEnd; region++)
	{
		struct tsf_voice *voice, *v, *vEnd; TSF_BOOL doLoop; float lowpassFilterQDB, lowpassFc;
		if (key < region->lokey || key > region->hikey || midiVelocity < region->lovel || midiVelocity > region->hivel) continue;

		voice = TSF_NULL, v = f->voices, vEnd = v + f->voiceNum;
		int activeChan = (f->channels ? f->channels->activeChannel : -1);
		int curPresetNum = (preset_index >= 0 && preset_index < f->presetNum) ? f->presets[preset_index].preset : preset_index;
		TSF_BOOL isBassPreset = (curPresetNum >= 32 && curPresetNum <= 39); // GM Bass: 32~39
		TSF_BOOL isDrumChan = (activeChan != -1 && f->channels && f->channels->channels[activeChan].isDrum); // [1. 추가]

		for (; v != vEnd; v++)
		{
			// 멜로디 악기는 동일 건반 즉시 컷 (보이스 낭비 방지)
			if (!isDrumChan && v->playingPreset != -1 && v->playIndex < voicePlayIndex && v->playingKey == key && (activeChan == -1 || v->playingChannel == activeChan))
				tsf_voice_endquick(f, v);
			// [수정] 드럼 채널: 동일 타악기 연속 타격 시 이전 타악기를 25ms 소프트 페이드아웃하여 롤(Roll) 타격감 보존 & 32보이스 고갈 방지!
			else if (isDrumChan && v->playingPreset != -1 && v->playIndex < voicePlayIndex && v->playingKey == key && v->playingChannel == activeChan)
			{
				v->ampenv.parameters.release = 0.025f; // 25ms 자연스러운 스네어/톰 잔향 겹침
				v->ampenv.segment = TSF_SEGMENT_SUSTAIN;
				tsf_voice_end(f, v);
			}
			// 베이스 악기는 동일 채널에서 새 노트 연주 시 이전 베이스 잔여음(Release)을 즉각 컷하여 저음 뭉개짐 0% 달성!
			else if (isBassPreset && v->playingPreset != -1 && v->playIndex < voicePlayIndex && (activeChan == -1 || v->playingChannel == activeChan))
				tsf_voice_endquick(f, v);
			// Mono 모드(CC 126) 활성화 시 동일 채널의 모든 이전 보이스 즉각 컷
			else if (activeChan != -1 && f->channels->channels[activeChan].isMono && v->playingPreset != -1 && v->playIndex < voicePlayIndex && v->playingChannel == activeChan)
				tsf_voice_endquick(f, v);
			else if (region->group && v->playingPreset == preset_index && v->region->group == region->group)
				tsf_voice_endquick(f, v);
			// Roland GS 표준 드럼 초크 그룹 (하이햇 42/44 -> 46 오픈하이햇 초크, 트라이앵글 80 -> 81 초크)
			else if (isDrumChan && v->playingPreset != -1 && v->playingChannel == activeChan)
			{
				if ((key == 42 || key == 44) && v->playingKey == 46) tsf_voice_endquick(f, v); // Closed/Pedal Hi-Hat stops Open Hi-Hat
				else if (key == 80 && v->playingKey == 81) tsf_voice_endquick(f, v);            // Mute Triangle stops Open Triangle
				else if (key == 78 && v->playingKey == 79) tsf_voice_endquick(f, v);            // Mute Cuica stops Open Cuica
			}
			else if (v->playingPreset == -1 && !voice)
				voice = v;
		}

		if (!voice)
		{
			if (f->maxVoiceNum)
			{
				// Gervill / FluidSynth 하이브리드 스마트 스틸링 (릴리즈 진행률 + 볼륨 감쇠율 종합 판정)
				// - 릴리즈가 짧은 베이스/타악기는 0.05초 만에 즉시 회수 -> 베이스 뭉개짐/지저분함 0%
				// - 방금 켜진 스트링은 릴리즈 진행률이 낮아 끝까지 보호 -> Whatever 음 전환 끊김 0%
				// - 오래된 이전 마디 스트링은 자연스럽게 회수 -> 해석남녀 스트링 무한 누적 0%
				float bestKillScore = -999999.0f;
				for (v = f->voices; v != vEnd; v++)
				{
					if (v->ampenv.segment >= TSF_SEGMENT_RELEASE)
					{
						int totalReleaseSamples = tsf_voice_envelope_release_samples(&v->ampenv, f->outSampleRate);
						if (totalReleaseSamples <= 0) totalReleaseSamples = 1;
						int samplesDone = totalReleaseSamples - v->ampenv.samplesUntilNextSegment;
						if (samplesDone < 0) samplesDone = 0;
						float releaseProgress = (float)samplesDone / (float)totalReleaseSamples;
						if (releaseProgress > 1.0f) releaseProgress = 1.0f;
						float volumeDecay = (1.0f - v->ampenv.level);
						if (volumeDecay < 0.0f) volumeDecay = 0.0f;

						float killScore = releaseProgress * 1.5f + volumeDecay * 1.0f;
						if (killScore > bestKillScore)
						{
							bestKillScore = killScore;
							voice = v;
						}
					}
				}
				if (!voice)
				{
					// [2단계] Release 중인 보이스가 전혀 없을 때 (32개 건반을 모두 누르고 있는 상황):
					// 🛡️ 새로 켜진 음표(ATTACK/DELAY) 및 웅장하게 울리는 활성 메인 지속음(Strings/Pad/Lead: level >= 0.15)은 절대 보호!
					// ➔ 소리가 거의 다 꺼진 배경 감쇠음(DECAY 중 level < 0.15)부터 안전하게 회수!
					unsigned int oldestPlayIndex = 0xFFFFFFFF;
					for (v = f->voices; v != vEnd; v++)
					{
						if (v->ampenv.segment >= TSF_SEGMENT_DECAY && v->ampenv.level < 0.15f)
						{
							if (v->playIndex < oldestPlayIndex)
							{
								oldestPlayIndex = v->playIndex;
								voice = v;
							}
						}
					}

					// 만약 모든 보이스가 0.15 이상으로 울리고 있을 때는 가장 오래된 DECAY/SUSTAIN 음표 중 최저 볼륨 선택
					if (!voice)
					{
						oldestPlayIndex = 0xFFFFFFFF;
						float lowestLevel = 999999.0f;
						for (v = f->voices; v != vEnd; v++)
						{
							if (v->ampenv.segment >= TSF_SEGMENT_DECAY)
							{
								if (v->playIndex < oldestPlayIndex)
								{
									oldestPlayIndex = v->playIndex;
									lowestLevel = v->ampenv.level;
									voice = v;
								}
								else if (v->playIndex == oldestPlayIndex && v->ampenv.level < lowestLevel)
								{
									lowestLevel = v->ampenv.level;
									voice = v;
								}
							}
						}
					}

					// 예외 폴백: 모든 보이스가 어택 중인 극단적 상황 시 가장 오래된 보이스 선택
					if (!voice)
					{
						unsigned int oldestFallback = 0xFFFFFFFF;
						for (v = f->voices; v != vEnd; v++)
						{
							if (v->playIndex < oldestFallback)
							{
								oldestFallback = v->playIndex;
								voice = v;
							}
						}
					}
				}
				if (!voice)
					continue;
				tsf_voice_kill(voice);
				DEBUG_RECORD_VOICE_STEAL();
			}
			else
			{
				// Allocate more voices so we don't need to kill one off.
				struct tsf_voice* newVoices;
				f->voiceNum += 4;
				newVoices = (struct tsf_voice*)TSF_REALLOC(f->voices, f->voiceNum * sizeof(struct tsf_voice));
				if (!newVoices) return 0;
				f->voices = newVoices;
				voice = &f->voices[f->voiceNum - 4];
				voice[1].playingPreset = voice[2].playingPreset = voice[3].playingPreset = -1;
			}
		}

		// 안티 클릭(Anti-Click): 직전 보이스의 잔여 IIR 필터 상태(z1, z2) 및 DC 오프셋 완전 제로화
		voice->lowpass.z1 = 0.0f;
		voice->lowpass.z2 = 0.0f;
		voice->ampenv.level = 0.0f;
		voice->ampenv.slope = 0.0f;
		voice->modenv.level = 0.0f;
		voice->modenv.slope = 0.0f;

		voice->region = region;
		voice->playingPreset = preset_index;
		voice->playingKey = key;
		voice->playIndex = voicePlayIndex;
		voice->heldSustain = voice->heldSostenuto = 0;
		voice->noteGainDB = f->globalGainDB - region->attenuation - (40.0f * TSF_LOG10F(1.0f / vel));

		if (f->channels)
		{
			f->channels->setupVoice(f, voice);
		}
		else
		{
			tsf_voice_calcpitchratio(voice, 0, f->outSampleRate);
			// Gervill / SoundFont 2.04 Equal Power Pan Law (cos/sin)
			float panNorm = region->pan + 0.5f;
			if (panNorm < 0.0f) panNorm = 0.0f;
			else if (panNorm > 1.0f) panNorm = 1.0f;
			float panRad = panNorm * 1.57079632679f;
			voice->panFactorLeft  = TSF_COSF(panRad);
			voice->panFactorRight = TSF_SINF(panRad);
		}

		// Offset/end (32.32 Fixed-Point 초기화).
		voice->sourceSamplePosition = ((uint64_t)region->offset) << 32;

		// Loop.
		doLoop = (region->loop_mode != TSF_LOOPMODE_NONE && region->loop_start < region->loop_end);
		voice->loopStart = (doLoop ? region->loop_start : 0);
		voice->loopEnd = (doLoop ? region->loop_end : 0);

		struct tsf_channel* curChan = (f->channels && f->channels->activeChannel < f->channels->channelNum) ? &f->channels->channels[f->channels->activeChannel] : TSF_NULL;

		// Setup envelopes with channel offsets.
		struct tsf_envelope ampenv = region->ampenv;
		struct tsf_envelope modenv = region->modenv;
		if (curChan)
		{
			if (curChan->attackOffset) {
				float mul = tsf_timecents2Secsf((float)curChan->attackOffset);
				ampenv.attack *= mul;
				modenv.attack *= mul;
			}
			if (curChan->decayOffset) {
				float mul = tsf_timecents2Secsf((float)curChan->decayOffset);
				ampenv.decay *= mul;
				modenv.decay *= mul;
			}
			if (curChan->releaseOffset) {
				float mul = tsf_timecents2Secsf((float)curChan->releaseOffset);
				ampenv.release *= mul;
				modenv.release *= mul;
			}
			if (curChan->softPedal) {
				voice->noteGainDB -= 4.0f;
				ampenv.attack *= 1.3f;
			}
			curChan->lastKey = (unsigned char)key;

			// Roland MT-32 실기 1:1 엔벨로프 정밀 보정 (SF2 23종 악기 결함 완벽 교정)
			if (MIDIParser::getSynthMode() == SYNTH_MODE_MT32 && !curChan->isDrum)
			{
				int prog = (voice->playingPreset >= 0 && voice->playingPreset < f->presetNum) ? f->presets[voice->playingPreset].preset : 0;
				// [1] 지속음(Sustained) 악기 조기 소멸 방지 및 서스테인 복원
				if (prog >= 28 && prog <= 31) // Syn Bass 1~4 (G02.MID "띠로리~~~~" 지속)
				{
					ampenv.sustain = 0.70f;       // 실기 MT-32 70% 서스테인
					ampenv.decay = 6.0f;          // 6.0초 디케이
					ampenv.keynumToDecay = 0.0f;  // timecents 변환 축소 오버라이드 방지!
					ampenv.keynumToHold = 0.0f;
				}
				else if (prog == 88 || prog == 89) // Trumpet 1, 2 (0.24초 소멸 결함 복원)
				{
					ampenv.sustain = 0.75f;
					ampenv.decay = 4.0f;
					ampenv.keynumToDecay = 0.0f;
					ampenv.keynumToHold = 0.0f;
				}
				else if (prog == 108 || prog == 109) // Whistle 1, 2 (0.05초 소멸 결함 복원)
				{
					ampenv.sustain = 0.80f;
					ampenv.decay = 4.0f;
					ampenv.keynumToDecay = 0.0f;
					ampenv.keynumToHold = 0.0f;
				}
				else if (prog == 39 || prog == 40 || prog == 41) // Funny Vox, Echo Bell, Ice Rain
				{
					ampenv.sustain = 0.60f;
					ampenv.decay = 4.0f;
					ampenv.keynumToDecay = 0.0f;
					ampenv.keynumToHold = 0.0f;
				}
				// [2] 단타음/타악기 과도한 릴리즈(30초 웅웅 잔향) 실기 댐퍼/뮤트 컷
				else if (prog == 122) // Orchestral Hit MT (32.7초 잔향 -> 0.9초 실기 컷)
				{
					if (ampenv.release > 1.0f) ampenv.release = 0.90f;
				}
				else if (prog == 119) // Cymbal (28.2초 잔향 -> 1.8초 실기 컷)
				{
					if (ampenv.release > 2.0f) ampenv.release = 1.80f;
				}
				else if (prog == 120 || prog == 121) // Castanets, Triangle (5~8초 잔향 -> 0.45초 실기 컷)
				{
					if (ampenv.release > 0.60f) ampenv.release = 0.45f;
				}
				else if (prog == 0 || prog == 1 || prog == 7) // Piano 1, 2, Honkytonk (5초 잔향 -> 0.55초 실기 댐퍼)
				{
					if (ampenv.release > 0.70f) ampenv.release = 0.55f;
				}
				else if (prog == 63 || prog == 100 || prog == 101) // Sitar, Windbell, Glock
				{
					if (ampenv.release > 1.50f) ampenv.release = 1.20f;
				}
			}
		}
		tsf_voice_envelope_setup(&voice->ampenv, &ampenv, key, midiVelocity, TSF_TRUE, f->outSampleRate);
		tsf_voice_envelope_setup(&voice->modenv, &modenv, key, midiVelocity, TSF_FALSE, f->outSampleRate);

		// Setup lowpass filter (SoundFont 2.04 Standard)
        float cutoffCents = (float)region->initialFilterFc;
        if (curChan) cutoffCents += (float)curChan->filterCutoffOffset;
        extern int8_t g_drum_cutoff[128];
        if (curChan && curChan->isDrum && (key >= 0 && key < 128)) {
            cutoffCents += (float)(g_drum_cutoff[key] * 50); // -64..+63 -> -3200..+3150 cents
        }
        if (cutoffCents < 400.0f) cutoffCents = 400.0f;
        if (cutoffCents > 13500.0f) cutoffCents = 13500.0f;
        voice->lowpassCutoffCents = cutoffCents;
        lowpassFc = (cutoffCents <= 13400.0f ? (tsf_cents2Hertz(cutoffCents) / f->outSampleRate) : 1.0f);
        if (lowpassFc > 0.45f) lowpassFc = 0.45f;

        int resQ = region->initialFilterQ;
        if (curChan) resQ += curChan->filterResonanceOffset;
        if (resQ < 0) resQ = 0;
        if (resQ > 80) resQ = 80; // [수정] 최대 8dB 제한으로 Q 발진 방지
        lowpassFilterQDB = resQ / 10.0f;

        voice->lowpass.QInv = 1.0f / TSF_POWF(10.0f, (lowpassFilterQDB / 20.0f));
        voice->lowpass.z1 = voice->lowpass.z2 = 0;
        // 컷오프가 완전히 열려있으면 필터를 꺼서 완전 클린 바이패스 처리
        voice->lowpass.active = (cutoffCents < 13000.0f && lowpassFc < 0.44f);
        if (voice->lowpass.active) tsf_voice_lowpass_setup(&voice->lowpass, lowpassFc);

		// Setup LFO filters.
		tsf_voice_lfo_setup(&voice->modlfo, region->delayModLFO, region->freqModLFO, f->outSampleRate);
		tsf_voice_lfo_setup(&voice->viblfo, region->delayVibLFO, (region->freqVibLFO ? region->freqVibLFO : -1000), f->outSampleRate);
	}
	return 1;
}

TSFDEF int tsf_bank_note_on(tsf* f, int bank, int preset_number, int key, float vel)
{
	int preset_index = tsf_get_presetindex(f, bank, preset_number);
	if (preset_index == -1) return 0;
	return tsf_note_on(f, preset_index, key, vel);
}

TSFDEF void tsf_note_off(tsf* f, int preset_index, int key)
{
	struct tsf_voice *v = f->voices, *vEnd = v + f->voiceNum, *vMatchFirst = TSF_NULL, *vMatchLast = TSF_NULL;
	for (; v != vEnd; v++)
	{
		//Find the first and last entry in the voices list with matching preset, key and look up the smallest play index
		if (v->playingPreset != preset_index || v->playingKey != key || v->ampenv.segment >= TSF_SEGMENT_RELEASE) continue;
		else if (!vMatchFirst || v->playIndex < vMatchFirst->playIndex) vMatchFirst = vMatchLast = v;
		else if (v->playIndex == vMatchFirst->playIndex) vMatchLast = v;
	}
	if (!vMatchFirst) return;
	for (v = vMatchFirst; v <= vMatchLast; v++)
	{
		//Stop all voices with matching preset, key and the smallest play index which was enumerated above
		if (v != vMatchFirst && v != vMatchLast &&
			(v->playIndex != vMatchFirst->playIndex || v->playingPreset != preset_index || v->playingKey != key || v->ampenv.segment >= TSF_SEGMENT_RELEASE)) continue;
		tsf_voice_end(f, v);
	}
}

TSFDEF int tsf_bank_note_off(tsf* f, int bank, int preset_number, int key)
{
	int preset_index = tsf_get_presetindex(f, bank, preset_number);
	if (preset_index == -1) return 0;
	tsf_note_off(f, preset_index, key);
	return 1;
}

TSFDEF void tsf_note_off_all(tsf* f)
{
	struct tsf_voice *v = f->voices, *vEnd = v + f->voiceNum;
	for (; v != vEnd; v++) if (v->playingPreset != -1 && v->ampenv.segment < TSF_SEGMENT_RELEASE)
		tsf_voice_end(f, v);
}

TSFDEF int tsf_active_voice_count(tsf* f)
{
	int count = 0;
	struct tsf_voice *v = f->voices, *vEnd = v + f->voiceNum;
	for (; v != vEnd; v++) if (v->playingPreset != -1) count++;
	return count;
}

TSFDEF void tsf_render_short(tsf* f, short* buffer, int samples, int flag_mixing)
{
	float outputSamples[TSF_RENDER_SHORTBUFFERBLOCK];
	int channels = (f->outputmode == TSF_MONO ? 1 : 2), maxChannelSamples = TSF_RENDER_SHORTBUFFERBLOCK / channels;
	while (samples > 0)
	{
		int channelSamples = (samples > maxChannelSamples ? maxChannelSamples : samples);
		short* bufferEnd = buffer + channelSamples * channels;
		float *floatSamples = outputSamples;
		tsf_render_float(f, floatSamples, channelSamples, TSF_FALSE);
		samples -= channelSamples;

		if (flag_mixing)
			while (buffer != bufferEnd)
			{
				float v = *floatSamples++;
				if (v > 0.75f) {
					float diff = (v - 0.75f) * 4.0f;
					v = 0.75f + 0.24f * (diff / (1.0f + diff));
				} else if (v < -0.75f) {
					float diff = (-v - 0.75f) * 4.0f;
					v = -(0.75f + 0.24f * (diff / (1.0f + diff)));
				}
				int vi = (int)*buffer + (int)(v * 32767.0f);
				if (vi > 30000) {
					float diff = (float)(vi - 30000) * 0.000125f;
					vi = 30000 + (int)(2700.0f * (diff / (1.0f + diff)));
				} else if (vi < -30000) {
					float diff = (float)(-vi - 30000) * 0.000125f;
					vi = -(30000 + (int)(2700.0f * (diff / (1.0f + diff))));
				}
				*buffer++ = (short)vi;
			}
		else
			while (buffer != bufferEnd)
			{
				float v = *floatSamples++;
				if (v > 0.75f) {
					float diff = (v - 0.75f) * 4.0f;
					v = 0.75f + 0.24f * (diff / (1.0f + diff));
				} else if (v < -0.75f) {
					float diff = (-v - 0.75f) * 4.0f;
					v = -(0.75f + 0.24f * (diff / (1.0f + diff)));
				}
				*buffer++ = (short)(v * 32767.0f);
			}
	}
}

TSFDEF void tsf_render_float(tsf* f, float* buffer, int samples, int flag_mixing)
{
	struct tsf_voice *v = f->voices, *vEnd = v + f->voiceNum;
	if (!flag_mixing) TSF_MEMSET(buffer, 0, (f->outputmode == TSF_MONO ? 1 : 2) * sizeof(float) * samples);
	for (; v != vEnd; v++)
		if (v->playingPreset != -1)
			tsf_voice_render(f, v, buffer, samples);
}

// Roland Sound Canvas / General MIDI standard 3D Drum Pan Table (-0.5f: Left, 0.0f: Center, +0.5f: Right)
static const float tsf_gs_drum_pan[128] = {
	// 0~26: None
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	// 27~34: High Q, Slap, Scratch Push/Pull, Sticks, Square Click, Metronome Click/Bell
	-0.10f, 0.00f, -0.25f, -0.25f, 0.00f, 0.00f, 0.00f, 0.00f,
	// 35: Acoustic Bass Drum (Center)
	0.00f,
	// 36: Bass Drum 1 (Center)
	0.00f,
	// 37: Side Stick (Hard Left -38%)
	-0.38f,
	// 38: Acoustic Snare (Hard Left -35%)
	-0.35f,
	// 39: Hand Clap (Hard Left -38%, 찹- 찹-)
	-0.38f,
	// 40: Electric Snare (Hard Left -35%)
	-0.35f,
	// 41: Low Floor Tom (Right +35%)
	0.35f,
	// 42: Closed Hi-Hat (Hard Left -35%)
	-0.35f,
	// 43: High Floor Tom (Right +25%)
	0.25f,
	// 44: Pedal Hi-Hat (Hard Left -35%)
	-0.35f,
	// 45: Low Tom (Right +15%)
	0.15f,
	// 46: Open Hi-Hat (Hard Left -35%)
	-0.35f,
	// 47: Low-Mid Tom (Left -15%)
	-0.15f,
	// 48: High-Mid Tom (Left -25%)
	-0.25f,
	// 49: Crash Cymbal 1 (Left -30%)
	-0.30f,
	// 50: High Tom (Left -35%)
	-0.35f,
	// 51: Ride Cymbal 1 (Right +35%)
	0.35f,
	// 52: Chinese Cymbal (Hard Right +40%)
	0.40f,
	// 53: Ride Bell (Right +25%)
	0.25f,
	// 54: Tambourine (Right +35%)
	0.35f,
	// 55: Splash Cymbal (Left -25%)
	-0.25f,
	// 56: Cowbell (Left -15%)
	-0.15f,
	// 57: Crash Cymbal 2 (Right +35%)
	0.35f,
	// 58: Vibraslap (Left -30%)
	-0.30f,
	// 59: Ride Cymbal 2 (Right +25%)
	0.25f,
	// 60~68: Bongos, Congas, Timbales, Agogos
	0.15f, -0.15f, 0.20f, 0.00f, -0.20f, 0.25f, -0.25f, 0.20f, -0.20f,
	// 69~70: Cabasa, Maracas
	0.30f, 0.35f,
	// 71~72: Short/Long Whistle
	0.00f, 0.00f,
	// 73~75: Guiros, Claves
	0.20f, 0.20f, 0.25f,
	// 76~77: Woodblocks
	0.20f, -0.20f,
	// 78~79: Cuicas
	0.15f, -0.15f,
	// 80~81: Triangles
	0.40f, 0.40f,
	// 82: Shaker (Hard Right +40%)
	0.40f,
	// 83~87: Jingle Bell, Belltree, Castanets (Left -35%), Surdo
	0.30f, 0.35f, -0.35f, -0.15f, -0.15f
};

static void tsf_channel_setup_voice(tsf* f, struct tsf_voice* v)
{
	struct tsf_channel* c = &f->channels->channels[f->channels->activeChannel];
	float panVal = v->region->pan;
	if (c->isDrum && panVal == 0.0f)
	{
		panVal = tsf_gs_drum_pan[v->playingKey & 0x7F];
	}
	float newpan = panVal + c->panOffset;
	if (MIDIParser::getSynthMode() == SYNTH_MODE_MT32)
	{
		newpan *= 0.70f; // MT-32 실기 7단계 아날로그 믹서 좁은 스테레오 패닝 (중앙 융합)
	}
	v->playingChannel = f->channels->activeChannel;
	v->noteGainDB += c->gainDB;

	// MT-32 모드: 악기별 데시벨 캘리브레이션 및 드럼 채널(-6dB) 정규화
	if (MIDIParser::getSynthMode() == SYNTH_MODE_MT32)
	{
		if (c->isDrum)
		{
			v->noteGainDB -= 2.5f; // MT-32 드럼 볼륨 진정 (-2.5dB) -> 멜로딕 악기와 자연스러운 앙상블 믹싱
		}
		else
		{
			int prog = (v->playingPreset >= 0 && v->playingPreset < f->presetNum) ? f->presets[v->playingPreset].preset : 0;
			v->noteGainDB += MT32_PROG_GAIN_DB[prog & 0x7F];
		}
	}

float scaleCents = (float)c->scaleTuning[v->playingKey % 12];
	float totalTuning = c->tuning + c->tuningOffset + (scaleCents / 100.0f);

	// [추가] Roland GS 드럼 키별 피치 오프셋(NRPN 0x18)을 재생 주파수에 실시간 반영!
	extern int8_t g_drum_pitch[128];
	if (c->isDrum && (v->playingKey >= 0 && v->playingKey < 128))
	{
		totalTuning += (float)g_drum_pitch[v->playingKey];
	}

	tsf_voice_calcpitchratio(v, (c->pitchWheel == 8192 ? totalTuning : (((float)c->pitchWheel - 8192.0f) / 8192.0f * c->pitchRange + totalTuning)), f->outSampleRate);
	
	// Gervill / SoundFont 2.04 Equal Power Pan Law (cos/sin)
	float panNorm = newpan + 0.5f;
	if (panNorm < 0.0f) panNorm = 0.0f;
	else if (panNorm > 1.0f) panNorm = 1.0f;
	float panRad = panNorm * 1.57079632679f;
	v->panFactorLeft  = TSF_COSF(panRad);
	v->panFactorRight = TSF_SINF(panRad);
}
static struct tsf_channel* tsf_channel_init(tsf* f, int channel)
{
	int i;
	if (f->channels && channel < f->channels->channelNum) return &f->channels->channels[channel];
	if (!f->channels)
	{
		f->channels = (struct tsf_channels*)TSF_MALLOC(sizeof(struct tsf_channels) + sizeof(struct tsf_channel) * channel);
		if (!f->channels) return TSF_NULL;
		f->channels->setupVoice = &tsf_channel_setup_voice;
		f->channels->channelNum = 0;
		f->channels->activeChannel = 0;
	}
	else
	{
		struct tsf_channels *newChannels = (struct tsf_channels*)TSF_REALLOC(f->channels, sizeof(struct tsf_channels) + sizeof(struct tsf_channel) * channel);
		if (!newChannels) return TSF_NULL;
		f->channels = newChannels;
	}
	i = f->channels->channelNum;
	f->channels->channelNum = channel + 1;
	for (; i <= channel; i++)
	{
		struct tsf_channel* c = &f->channels->channels[i];
		c->presetIndex = c->bank = 0;
		c->pitchWheel = c->midiPan = 8192;
		c->midiVolume = c->midiExpression = 16383;
		c->midiRPN = 0xFFFF;
		c->midiData = c->sustain = c->sostenuto = 0;
		c->isDrum = (i == 9 ? 1 : 0);
		c->panOffset = 0.0f;
		c->gainDB = 0.0f;
		c->pitchRange = 2.0f;
		c->tuning = 0.0f;
		c->filterCutoffOffset = 0;
		c->filterResonanceOffset = 0;
		c->modWheel = 0;
		c->attackOffset = c->decayOffset = c->releaseOffset = 0;
		c->softPedal = 0;
		c->nrpnParam = 0xFFFF;
		c->portamentoOn = 0;
		c->portamentoTime = 0;
		c->lastKey = 0xFF;
		c->reverbSend = (i == 9 ? 0 : 40);
		c->chorusSend = 0;
		c->isMono = 0;
		c->modDepth = 50.0f;
		memset(c->scaleTuning, 0, sizeof(c->scaleTuning));
		c->tuningOffset = 0.0f;
	}
	return &f->channels->channels[channel];
}

static void tsf_channel_applypitch(tsf* f, int channel, struct tsf_channel* c)
{
	struct tsf_voice *v, *vEnd;
	for (v = f->voices, vEnd = v + f->voiceNum; v != vEnd; v++)
	{
		if (v->playingPreset != -1 && v->playingChannel == channel)
		{
			float scaleCents = (float)c->scaleTuning[v->playingKey % 12];
			float totalTuning = c->tuning + c->tuningOffset + (scaleCents / 100.0f);
			float pitchShift = (c->pitchWheel == 8192 ? totalTuning : (((float)c->pitchWheel - 8192.0f) / 8192.0f * c->pitchRange + totalTuning));
			tsf_voice_calcpitchratio(v, pitchShift, f->outSampleRate);
		}
	}
}

TSFDEF int tsf_channel_set_drum_mode(tsf* f, int channel, int is_drum)
{
	struct tsf_channel *c = tsf_channel_init(f, channel);
	if (!c) return 0;
	c->isDrum = (unsigned short)(is_drum != 0);
	return 1;
}

TSFDEF int tsf_channel_get_drum_mode(tsf* f, int channel)
{
	return (f->channels && channel < f->channels->channelNum ? f->channels->channels[channel].isDrum : (channel == 9 ? 1 : 0));
}

TSFDEF int tsf_channel_set_presetindex(tsf* f, int channel, int preset_index)
{
	struct tsf_channel *c = tsf_channel_init(f, channel);
	if (!c) return 0;
	c->presetIndex = (unsigned short)preset_index;
	return 1;
}

TSFDEF int tsf_channel_set_presetnumber(tsf* f, int channel, int preset_number, int flag_mididrums)
{
	int preset_index = -1;
	struct tsf_channel *c = tsf_channel_init(f, channel);
	if (!c) return 0;
	// 새 악기 번호 지정 시 직전 곡의 NRPN 필터 컷오프/레조넌스/엔벨로프 오프셋 초기화
	c->filterCutoffOffset = 0;
	c->filterResonanceOffset = 0;
	c->attackOffset = 0;
	c->decayOffset = 0;
	c->releaseOffset = 0;
	c->nrpnParam = 0xFFFF;

	if (flag_mididrums || c->isDrum)
	{
		// 1) 128 | Bank MSB (GS Drum Sets)
		if (c->bank > 0) preset_index = tsf_get_presetindex(f, 128 | (c->bank >> 7), preset_number);
		// 2) Bank 128 with preset_number
		if (preset_index == -1) preset_index = tsf_get_presetindex(f, 128, preset_number);
		// 3) Standard Drum Kit (Bank 128, Preset 0) Fallback
		if (preset_index == -1) preset_index = tsf_get_presetindex(f, 128, 0);
	}
	else
	{
		// [수정] 멜로디 채널: Bank MSB(c->bank >> 7)가 1~126이면 해당 Variation Bank 우선 검색
		int varBank = (c->bank >> 7);
		if (varBank > 0 && varBank < 128)
			preset_index = tsf_get_presetindex(f, varBank, preset_number);

		// 2) MT-32 뱅크 127 등 직접 대입된 뱅크 (단, 드럼 뱅크 128은 제외)
		if (preset_index == -1 && c->bank != 0 && c->bank != 128)
			preset_index = tsf_get_presetindex(f, c->bank, preset_number);

		// 3) General MIDI 표준 기본 뱅크 (Bank 0) Fallback
		if (preset_index == -1)
			preset_index = tsf_get_presetindex(f, 0, preset_number);
	}
	if (preset_index != -1)
	{
		c->presetIndex = (unsigned short)preset_index;
		return 1;
	}
	return 0;
}

TSFDEF int tsf_channel_set_bank(tsf* f, int channel, int bank)
{
	struct tsf_channel *c = tsf_channel_init(f, channel);
	if (!c) return 0;
	c->bank = (unsigned short)bank;
	return 1;
}

TSFDEF int tsf_channel_set_bank_preset(tsf* f, int channel, int bank, int preset_number)
{
	int preset_index;
	struct tsf_channel *c = tsf_channel_init(f, channel);
	if (!c) return 0;
	c->bank = (unsigned short)bank;
	preset_index = tsf_get_presetindex(f, bank, preset_number);
	if (preset_index != -1)
	{
		c->presetIndex = (unsigned short)preset_index;
		return 1;
	}
	return 0;
}

TSFDEF int tsf_channel_set_pan(tsf* f, int channel, float pan)
{
	struct tsf_voice *v, *vEnd;
	struct tsf_channel *c = tsf_channel_init(f, channel);
	if (!c) return 0;
	for (v = f->voices, vEnd = v + f->voiceNum; v != vEnd; v++)
		if (v->playingPreset != -1 && v->playingChannel == channel)
		{
			float basePan = (c->isDrum && v->region->pan == 0.0f) ? tsf_gs_drum_pan[v->playingKey & 0x7F] : v->region->pan;
			float newpan = basePan + pan - 0.5f;
			if      (newpan <= -0.5f) { v->panFactorLeft = 1.0f; v->panFactorRight = 0.0f; }
			else if (newpan >=  0.5f) { v->panFactorLeft = 0.0f; v->panFactorRight = 1.0f; }
			else { v->panFactorLeft = TSF_SQRTF(0.5f - newpan); v->panFactorRight = TSF_SQRTF(0.5f + newpan); }
		}
	c->panOffset = pan - 0.5f;
	return 1;
}

TSFDEF int tsf_channel_set_volume(tsf* f, int channel, float volume)
{
	float gainDB = tsf_gainToDecibels(volume), gainDBChange;
	struct tsf_voice *v, *vEnd;
	struct tsf_channel *c = tsf_channel_init(f, channel);
	if (!c) return 0;
	if (gainDB == c->gainDB) return 1;
	for (v = f->voices, vEnd = v + f->voiceNum, gainDBChange = gainDB - c->gainDB; v != vEnd; v++)
		if (v->playingPreset != -1 && v->playingChannel == channel)
			v->noteGainDB += gainDBChange;
	c->gainDB = gainDB;
	return 1;
}

TSFDEF int tsf_channel_set_pitchwheel(tsf* f, int channel, int pitch_wheel)
{
	struct tsf_channel *c = tsf_channel_init(f, channel);
	if (!c) return 0;
	if (c->pitchWheel == pitch_wheel) return 1;
	c->pitchWheel = (unsigned short)pitch_wheel;
	tsf_channel_applypitch(f, channel, c);
	return 1;
}

TSFDEF int tsf_channel_set_pitchrange(tsf* f, int channel, float pitch_range)
{
	struct tsf_channel *c = tsf_channel_init(f, channel);
	if (!c) return 0;
	if (c->pitchRange == pitch_range) return 1;
	c->pitchRange = pitch_range;
	if (c->pitchWheel != 8192) tsf_channel_applypitch(f, channel, c);
	return 1;
}

TSFDEF int tsf_channel_set_tuning(tsf* f, int channel, float tuning)
{
	struct tsf_channel *c = tsf_channel_init(f, channel);
	if (!c) return 0;
	if (c->tuning == tuning) return 1;
	c->tuning = tuning;
	tsf_channel_applypitch(f, channel, c);
	return 1;
}

TSFDEF int tsf_channel_set_sustain(tsf* f, int channel, int flag_sustain)
{
	struct tsf_channel *c = tsf_channel_init(f, channel);
	if (!c) return 0;
	if (!c->sustain == !flag_sustain) return 1;
	c->sustain = (unsigned short)(flag_sustain != 0);
	//Turning on sustain does no action now, just starts note_off behaving differently
	if (flag_sustain) return 1;
	//Turning off sustain, actually end voices that got a note_off and were set to heldSustain status
	struct tsf_voice *v = f->voices, *vEnd = v + f->voiceNum;
	for (; v != vEnd; v++)
		if (v->playingPreset != -1 && v->playingChannel == channel && v->ampenv.segment < TSF_SEGMENT_RELEASE && v->heldSustain)
			tsf_voice_end(f, v);
	return 1;
}

TSFDEF int tsf_channel_set_sostenuto(tsf* f, int channel, int flag_sostenuto)
{
	struct tsf_channel *c = tsf_channel_init(f, channel);
	struct tsf_voice *v, *vEnd;
	if (!c) return 0;
	if (!c->sostenuto == !flag_sostenuto) return 1;
	c->sostenuto = (unsigned short)(flag_sostenuto != 0);
	if (flag_sostenuto)
	{
		for (v = f->voices, vEnd = v + f->voiceNum; v != vEnd; v++)
			if (v->playingPreset != -1 && v->playingChannel == channel && v->ampenv.segment < TSF_SEGMENT_RELEASE)
				v->heldSostenuto = 1;
	}
	else
	{
		for (v = f->voices, vEnd = v + f->voiceNum; v != vEnd; v++)
			if (v->playingPreset != -1 && v->playingChannel == channel && v->heldSostenuto)
			{
				v->heldSostenuto = 0;
				if (v->heldSustain && !c->sustain) tsf_voice_end(f, v);
			}
	}
	return 1;
}

TSFDEF int tsf_channel_note_on(tsf* f, int channel, int key, float vel)
{
	if (!f->channels || channel >= f->channels->channelNum) return 1;
	f->channels->activeChannel = channel;
	if (!vel)
	{
		tsf_channel_note_off(f, channel, key);
		return 1;
	}
	int presetIndex = f->channels->channels[channel].presetIndex;

	// [수정] 오직 MT-32 모드이면서 (channel == 9 || isDrum)일 때:
	// 35~81번 타악기 키는 Standard 드럼 키트(Preset 128)로, 그 외 키(효과음)는 CM-64 셋으로 100% 라우팅!
	if (MIDIParser::getSynthMode() == SYNTH_MODE_MT32 && (channel == 9 || f->channels->channels[channel].isDrum))
	{
		if (key >= 35 && key <= 81)
		{
			if (f->mt32StdDrumIndex != -1) presetIndex = f->mt32StdDrumIndex;
		}
		else
		{
			if (f->mt32CmDrumIndex != -1) presetIndex = f->mt32CmDrumIndex;
		}
	}
	return tsf_note_on(f, presetIndex, key, vel);
}

TSFDEF void tsf_channel_note_off(tsf* f, int channel, int key)
{
	if (!f->channels || channel >= f->channels->channelNum) return;

	TSF_BOOL isDrumChan = (channel == 9 || f->channels->channels[channel].isDrum);
	TSF_BOOL isSFXKey = (f->channels->channels[channel].presetIndex == 56 || key == 58 || key == 67 || key == 70 || key == 73);

	// 일반 원샷 드럼(Standard/Room/Power 킷: 킥, 스네어, 심벌 등)은 실기 표준대로 Note-Off 무시 (자연 감쇠 펀치 100% 보존!)
	// 오직 SFX 세트(박수소리 등 지속형 루프) 및 멜로디 악기만 Note-Off 정상 처리!
	if (isDrumChan && !isSFXKey)
	{
		return;
	}

	unsigned sustain = f->channels->channels[channel].sustain;
	struct tsf_voice *v, *vEnd = f->voices + f->voiceNum;
	unsigned int targetPlayIndex = 0xFFFFFFFF;

	// 1단계: 실기(Roland SC-55) 표준 FIFO - 해당 (channel, key)에서 현재 발음 중인 가장 오래된 Note-On의 playIndex 탐색
	for (v = f->voices; v != vEnd; v++)
	{
		if (v->playingPreset != -1 && v->playingChannel == channel && v->playingKey == key && v->ampenv.segment < TSF_SEGMENT_RELEASE && !v->heldSustain)
		{
			if (v->playIndex < targetPlayIndex)
			{
				targetPlayIndex = v->playIndex;
			}
		}
	}

	if (targetPlayIndex == 0xFFFFFFFF) return;

	// 2단계: 그 정확한 playIndex에 속한 모든 레이어(스테레오 L/R 등) 보이스들을 찾아 안전하게 Note-Off 처리
	for (v = f->voices; v != vEnd; v++)
	{
		if (v->playingPreset != -1 && v->playingChannel == channel && v->playingKey == key && v->playIndex == targetPlayIndex && v->ampenv.segment < TSF_SEGMENT_RELEASE && !v->heldSustain)
		{
			if (sustain || v->heldSostenuto)
				v->heldSustain = 1;
			else
				tsf_voice_end(f, v);
		}
	}
}

TSFDEF void tsf_channel_note_off_all(tsf* f, int channel)
{
	if (!f->channels || channel >= f->channels->channelNum) return;

	// 🌟 Roland SC-55 / MT-32 실기 표준: 일반 타악기 드럼 채널은 All Notes Off (CC 123) 무시!
	// (G17.MID 등 2,522번 CC 123 쏟아지는 곡에서 2,232개 하이햇/심벌이 5ms 만에 싹둑 잘리는 문제 원천 차단)
	// 단, 지속형 루프 사운드인 SFX 드럼 킷(Preset 56 - 박수소리, 사이렌)만 Note-Off 수용!
	if ((channel == 9 || f->channels->channels[channel].isDrum) && f->channels->channels[channel].presetIndex != 56)
	{
		return;
	}

	unsigned sustain = f->channels->channels[channel].sustain;
	struct tsf_voice *v = f->voices, *vEnd = v + f->voiceNum;
	for (; v != vEnd; v++)
	{
		if (v->playingPreset != -1 && v->playingChannel == channel && v->ampenv.segment < TSF_SEGMENT_RELEASE)
		{
			if (sustain || v->heldSostenuto)
				v->heldSustain = 1;
			else
			{
				// 5ms (0.005초) 초고속 안티-클릭 마이크로 램프로 스르륵 닫고 즉시 보이스 회수! (뚜둑 끊김 잡음 100% 원천 차단)
				v->ampenv.parameters.release = 0.005f;
				v->ampenv.segment = TSF_SEGMENT_SUSTAIN; // Force transition to release
				tsf_voice_end(f, v);
			}
		}
	}
}

TSFDEF void tsf_channel_sounds_off_all(tsf* f, int channel)
{
	struct tsf_voice *v = f->voices, *vEnd = v + f->voiceNum;
	for (; v != vEnd; v++)
		if (v->playingPreset != -1 && (channel == -1 || v->playingChannel == channel)) {
			v->heldSustain = 0;
			v->heldSostenuto = 0;
			tsf_voice_kill(v); // 즉각 강제 킬로 잔여 유령 보이스 100% 소멸!
		}
}

TSFDEF int tsf_channel_midi_control(tsf* f, int channel, int controller, int control_value)
{
	struct tsf_channel* c = tsf_channel_init(f, channel);
	if (!c) return 0;
	switch (controller)
	{
		case   1 /*MODULATION*/      : c->modWheel       = (unsigned char)control_value; return 1;
		case   7 /*VOLUME_MSB*/      : c->midiVolume     = (unsigned short)((c->midiVolume     & 0x7F  ) | (control_value << 7)); goto TCMC_SET_VOLUME;
		case  39 /*VOLUME_LSB*/      : c->midiVolume     = (unsigned short)((c->midiVolume     & 0x3F80) |  control_value);       goto TCMC_SET_VOLUME;
		case  11 /*EXPRESSION_MSB*/  : c->midiExpression = (unsigned short)((c->midiExpression & 0x7F  ) | (control_value << 7)); goto TCMC_SET_VOLUME;
		case  43 /*EXPRESSION_LSB*/  : c->midiExpression = (unsigned short)((c->midiExpression & 0x3F80) |  control_value);       goto TCMC_SET_VOLUME;
		case  10 /*PAN_MSB*/         : c->midiPan        = (unsigned short)((c->midiPan        & 0x7F  ) | (control_value << 7)); goto TCMC_SET_PAN;
		case  42 /*PAN_LSB*/         : c->midiPan        = (unsigned short)((c->midiPan        & 0x3F80) |  control_value);       goto TCMC_SET_PAN;
		case   5 /*PORTAMENTO_TIME*/ : c->portamentoTime = (unsigned char)control_value; return 1;
		case  65 /*PORTAMENTO_ON*/   : c->portamentoOn   = (unsigned char)(control_value >= 64); return 1;
		case  66 /*SOSTENUTO*/       : tsf_channel_set_sostenuto(f, channel, (int)(control_value >= 64)); return 1;
		case  67 /*SOFT_PEDAL*/      : c->softPedal      = (unsigned char)(control_value >= 64); return 1;
		case  71 /*RESONANCE*/       : c->filterResonanceOffset = (short)((control_value - 64) * 5); return 1;
		case  74 /*CUTOFF/BRIGHT*/   : c->filterCutoffOffset    = (short)((control_value - 64) * 100); return 1;
		case  72 /*RELEASE_TIME*/    : c->releaseOffset         = (short)((control_value - 64) * 100); return 1;
		case  73 /*ATTACK_TIME*/     : c->attackOffset          = (short)((control_value - 64) * 100); return 1;
		case  75 /*DECAY_TIME*/      : c->decayOffset           = (short)((control_value - 64) * 100); return 1;
		case   6 /*DATA_ENTRY_MSB*/  :
			c->midiData = (unsigned short)((c->midiData & 0x7F) | (control_value << 7));
			if (c->nrpnParam != 0xFFFF)
			{
				if      (c->nrpnParam == 0x0120) { c->filterCutoffOffset = (short)((control_value - 64) * 100); return 1; }
				else if (c->nrpnParam == 0x0121) { c->filterResonanceOffset = (short)((control_value - 64) * 5); return 1; }
				else if (c->nrpnParam == 0x0163) { c->attackOffset = (short)((control_value - 64) * 100); return 1; }
				else if (c->nrpnParam == 0x0164) { c->decayOffset = (short)((control_value - 64) * 100); return 1; }
				else if (c->nrpnParam == 0x0166) { c->releaseOffset = (short)((control_value - 64) * 100); return 1; }
				else if (c->nrpnParam == 0x0108 || c->nrpnParam == 0x0109) { c->modWheel = (unsigned char)control_value; return 1; }
				else {
					uint8_t nrpnMsb = (uint8_t)(c->nrpnParam >> 8);
					uint8_t drumKey = (uint8_t)(c->nrpnParam & 0x7F);
					if (nrpnMsb == 0x18) { AudioEngine::setDrumKeyPitchDirect(drumKey, (int8_t)control_value - 64); return 1; }
					else if (nrpnMsb == 0x1A) { AudioEngine::setDrumKeyCutoffDirect(drumKey, (int8_t)control_value - 64); return 1; }
					else if (nrpnMsb == 0x1C) { AudioEngine::setDrumKeyLevelDirect(drumKey, (uint8_t)control_value); return 1; }
					else if (nrpnMsb == 0x1D) { AudioEngine::setDrumKeyPanDirect(drumKey, (uint8_t)control_value); return 1; }
				}
				return 1; // 지원하지 않는 NRPN은 RPN으로 흘리지 않고 안전하게 종료!
			}
			goto TCMC_SET_DATA;
		case  38 /*DATA_ENTRY_LSB*/  :
			c->midiData = (unsigned short)((c->midiData & 0x3F80) | control_value);
			if (c->nrpnParam != 0xFFFF) return 1;
			goto TCMC_SET_DATA;
		case  96 /*DATA_INCREMENT*/  :
			if (c->midiRPN == 0) { tsf_channel_set_pitchrange(f, channel, c->pitchRange + 1.0f); }
			return 1;
		case  97 /*DATA_DECREMENT*/  :
			if (c->midiRPN == 0 && c->pitchRange > 0.0f) { tsf_channel_set_pitchrange(f, channel, c->pitchRange - 1.0f); }
			return 1;
		case   0 /*BANK_SELECT_MSB*/ : c->bank = (unsigned short)((c->bank & 0x007F) | ((control_value & 0x7F) << 7)); return 1;
		case  32 /*BANK_SELECT_LSB*/ : c->bank = (unsigned short)((c->bank & 0x3F80) | (control_value & 0x7F)); return 1;
		case 101 /*RPN_MSB*/         :
			c->nrpnParam = 0xFFFF; // RPN 수신 시 NRPN 즉시 무효화!
			c->midiRPN = (unsigned short)(((c->midiRPN == 0xFFFF ? 0 : c->midiRPN) & 0x7F) | ((control_value & 0x7F) << 7));
			if (control_value == 127 && (c->midiRPN & 0x7F) == 127) c->midiRPN = 0xFFFF; // Null RPN
			return 1;
		case 100 /*RPN_LSB*/         :
			c->nrpnParam = 0xFFFF; // RPN 수신 시 NRPN 즉시 무효화!
			c->midiRPN = (unsigned short)(((c->midiRPN == 0xFFFF ? 0 : c->midiRPN) & 0x3F80) | (control_value & 0x7F));
			if (control_value == 127 && (c->midiRPN >> 7) == 127) c->midiRPN = 0xFFFF; // Null RPN
			return 1;
		case  98 /*NRPN_LSB*/        :
			c->midiRPN = 0xFFFF; // NRPN 수신 시 RPN 즉시 무효화!
			c->nrpnParam = (unsigned short)(((c->nrpnParam == 0xFFFF ? 0 : c->nrpnParam) & 0xFF00) | (control_value & 0x7F));
			if (control_value == 127 && (c->nrpnParam >> 8) == 127) c->nrpnParam = 0xFFFF; // Null NRPN
			return 1;
		case  99 /*NRPN_MSB*/        :
			c->midiRPN = 0xFFFF; // NRPN 수신 시 RPN 즉시 무효화!
			c->nrpnParam = (unsigned short)(((c->nrpnParam == 0xFFFF ? 0 : c->nrpnParam) & 0x00FF) | ((control_value & 0x7F) << 8));
			if (control_value == 127 && (c->nrpnParam & 0xFF) == 127) c->nrpnParam = 0xFFFF; // Null NRPN
			return 1;
		case  91 /*REVERB_SEND*/     : c->reverbSend     = (unsigned char)control_value; return 1;
		case  93 /*CHORUS_SEND*/     : c->chorusSend     = (unsigned char)control_value; return 1;
		case  84 /*PORTAMENTO_CTRL*/ : c->portamentoOn = 1; return 1;
		case  64 /*SUSTAIN*/         : tsf_channel_set_sustain(f, channel, (int)(control_value >= 64)); return 1;
		case 126 /*MONO_ON*/         : c->isMono = 1; return 1;
		case 127 /*POLY_ON*/         : c->isMono = 0; return 1;
		case 120 /*ALL_SOUND_OFF*/   : tsf_channel_sounds_off_all(f, channel); return 1;
		case 123 /*ALL_NOTES_OFF*/   :
		case 124 /*OMNI_OFF*/        :
		case 125 /*OMNI_ON*/         : tsf_channel_note_off_all(f, channel);   return 1;
		case 121 /*ALL_CTRL_OFF*/    :
			// 🌟 Roland SC-55 / MT-32 & MIDI 1.0 표준: Volume(CC 7), Pan(CC 10), Program/Bank는 유지!
			// Expression(CC 11), Pitch Bend(0), Modulation(0), Sustain(0), NRPN(0xFFFF)만 리셋
			c->midiExpression = 16383;
			c->midiRPN = 0xFFFF;
			c->midiData = 0;
			c->pitchWheel = 8192;
			c->filterCutoffOffset = 0;
			c->filterResonanceOffset = 0;
			c->modWheel = 0;
			c->attackOffset = c->decayOffset = c->releaseOffset = 0;
			c->softPedal = 0;
			c->sostenuto = 0;
			tsf_channel_set_sustain(f, channel, 0); // 서스테인 페달(CC 64) 100% 해제 및 heldSustain 보이스 릴리즈!
			c->nrpnParam = 0xFFFF;
			c->portamentoOn = 0;
			c->portamentoTime = 0;
			c->isMono = 0;
			c->modDepth = 50.0f;
			memset(c->scaleTuning, 0, sizeof(c->scaleTuning));
			c->tuningOffset = 0.0f;
			tsf_channel_set_volume(f, channel, TSF_POWF((c->midiVolume / 16383.0f), 2.0f));
			tsf_channel_set_pitchrange(f, channel, (MIDIParser::getSynthMode() == SYNTH_MODE_MT32) ? 12.0f : 2.0f);
			tsf_channel_set_tuning(f, channel, 0);
			tsf_channel_applypitch(f, channel, c);
			return 1;
	}
	return 1;
TCMC_SET_VOLUME:
	// General MIDI / DLS-1 standard quadratic volume curve (40dB attenuation range)
	tsf_channel_set_volume(f, channel, TSF_POWF((c->midiVolume / 16383.0f) * (c->midiExpression / 16383.0f), 2.0f));
	return 1;
TCMC_SET_PAN:
	tsf_channel_set_pan(f, channel, c->midiPan / 16383.0f);
	return 1;
TCMC_SET_DATA:
	if (c->midiRPN == 0) {
		float range = (float)(c->midiData >> 7) + 0.01f * (float)(c->midiData & 0x7F);
		if (range > 24.0f) range = 24.0f; // 최대 24반음(2옥타브) 클램핑
		tsf_channel_set_pitchrange(f, channel, range);
	}
	else if (c->midiRPN == 1) tsf_channel_set_tuning(f, channel, (int)c->tuning + ((float)c->midiData - 8192.0f) / 8192.0f);
	else if (c->midiRPN == 2 && controller == 6) tsf_channel_set_tuning(f, channel, ((float)control_value - 64.0f) + (c->tuning - (int)c->tuning));
	else if (c->midiRPN == 5) { c->modDepth = (float)((c->midiData >> 7) * 100 + (c->midiData & 0x7F)); }
	return 1;
}

TSFDEF int tsf_channel_get_preset_index(tsf* f, int channel)
{
	return (f->channels && channel < f->channels->channelNum ? f->channels->channels[channel].presetIndex : 0);
}

TSFDEF int tsf_channel_get_preset_bank(tsf* f, int channel)
{
	return (f->channels && channel < f->channels->channelNum ? (f->channels->channels[channel].bank & 0x7FFF) : 0);
}

TSFDEF int tsf_channel_get_preset_number(tsf* f, int channel)
{
	return (f->channels && channel < f->channels->channelNum ? f->presets[f->channels->channels[channel].presetIndex].preset : 0);
}

TSFDEF float tsf_channel_get_pan(tsf* f, int channel)
{
	return (f->channels && channel < f->channels->channelNum ? f->channels->channels[channel].panOffset + 0.5f : 0.5f);
}

TSFDEF float tsf_channel_get_volume(tsf* f, int channel)
{
	return (f->channels && channel < f->channels->channelNum ? tsf_decibelsToGain(f->channels->channels[channel].gainDB) : 1.0f);
}

TSFDEF int tsf_channel_get_pitchwheel(tsf* f, int channel)
{
	return (f->channels && channel < f->channels->channelNum ? f->channels->channels[channel].pitchWheel : 8192);
}

TSFDEF float tsf_channel_get_pitchrange(tsf* f, int channel)
{
	return (f->channels && channel < f->channels->channelNum ? f->channels->channels[channel].pitchRange : 2.0f);
}

TSFDEF float tsf_channel_get_tuning(tsf* f, int channel)
{
	return (f->channels && channel < f->channels->channelNum ? f->channels->channels[channel].tuning : 0.0f);
}

TSFDEF int tsf_channel_set_scale_tuning(tsf* f, int channel, const signed char* scale12)
{
	struct tsf_channel *c = tsf_channel_init(f, channel);
	if (!c || !scale12) return 0;
	memcpy(c->scaleTuning, scale12, 12);
	tsf_channel_applypitch(f, channel, c);
	return 1;
}

TSFDEF int tsf_channel_set_tuning_offset(tsf* f, int channel, float semitones)
{
	struct tsf_channel *c = tsf_channel_init(f, channel);
	if (!c) return 0;
	c->tuningOffset = semitones;
	tsf_channel_applypitch(f, channel, c);
	return 1;
}

TSFDEF int tsf_channel_set_mono(tsf* f, int channel, int is_mono)
{
	struct tsf_channel *c = tsf_channel_init(f, channel);
	if (!c) return 0;
	c->isMono = (unsigned char)(is_mono != 0);
	return 1;
}

#ifdef __cplusplus
}
#endif

#endif //TSF_IMPLEMENTATION
