/*
===========================================================================

Return to Castle Wolfenstein single player GPL Source Code
Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company. 

This file is part of the Return to Castle Wolfenstein single player GPL Source Code ("RTCW SP Source Code").  

RTCW SP Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

RTCW SP Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with RTCW SP Source Code.  If not, see <http://www.gnu.org/licenses/>.

===========================================================================
*/

#ifndef __G_TTS_H__
#define __G_TTS_H__

#include "g_local.h"

// TTS Voice Parameters
typedef struct {
	float gain;                    // Volume gain (0.0 - 2.0, default 1.0)
	float pitch_shift_semitones;   // Pitch shift in semitones (-12 to +12, default 0.0)
	float speed;                    // Playback speed (0.5 - 2.0, default 1.0)
	qboolean shout;                 // Shout mode (louder, higher pitch)
} tts_voice_params_t;

// TTS Configuration
typedef struct {
	char piper_path[MAX_QPATH];
	char model_path[MAX_QPATH];
	char model_config_path[MAX_QPATH];
	int sample_rate;               // Target sample rate (22050 or 44100)
	int timeout_ms;                 // Synthesis timeout in milliseconds
} tts_config_t;

// TTS Audio Buffer
typedef struct {
	byte *data;                    // PCM audio data
	int size;                      // Size in bytes
	int sample_rate;               // Sample rate
	int channels;                  // Number of channels (1 = mono, 2 = stereo)
	int bits_per_sample;           // Bits per sample (16)
} tts_audio_buffer_t;

// TTS Synthesis Request
typedef struct {
	char text[MAX_STRING_CHARS];   // Text to synthesize
	tts_voice_params_t params;     // Voice parameters
	int entity_num;                // Entity requesting synthesis
	int timestamp;                 // Request timestamp
	qboolean ready;                // Synthesis complete
	tts_audio_buffer_t *audio;     // Result audio buffer (allocated on completion)
} tts_request_t;

// TTS API Functions
qboolean TTS_Init( void );
void TTS_Shutdown( void );
qboolean TTS_IsEnabled( void );
qboolean TTS_Synthesize( const char *text, const tts_voice_params_t *params, int entity_num, tts_audio_buffer_t **out_audio );
void TTS_Update( void );  // Call each frame to process completed requests

// Helper functions
void TTS_DefaultParams( tts_voice_params_t *params );
qboolean TTS_LoadFromCache( const char *text, const tts_voice_params_t *params, tts_audio_buffer_t **out_audio );
void TTS_SaveToCache( const char *text, const tts_voice_params_t *params, const tts_audio_buffer_t *audio );
void TTS_FreeAudioBuffer( tts_audio_buffer_t *audio );
int TTS_RegisterAudioAsSound( const tts_audio_buffer_t *audio, const char *text );  // Register TTS audio as game sound

#endif // __G_TTS_H__

