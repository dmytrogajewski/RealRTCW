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

#include "g_local.h"
#include "g_tts.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <signal.h>
#include <stdlib.h>
#include <errno.h>

// CVARs are declared in g_main.c and extern'd in g_local.h

// Cache entry structure (must be defined before tts_state_t)
#define MAX_CACHE_ENTRIES 256
typedef struct {
	unsigned int hash;
	tts_audio_buffer_t *audio;
	int last_used;
} cache_entry_t;

// TTS State
typedef struct {
	qboolean initialized;
	qboolean enabled;
	tts_config_t config;
	
	// Worker thread
	pthread_t worker_thread;
	pthread_mutex_t queue_mutex;
	pthread_cond_t queue_condition;
	qboolean shutdown_requested;
	
	// Request queue
	#define MAX_TTS_REQUESTS 32
	tts_request_t request_queue[MAX_TTS_REQUESTS];
	int queue_head;
	int queue_tail;
	int queue_count;
	
	// Completed requests (for main thread to process)
	tts_request_t completed_requests[MAX_TTS_REQUESTS];
	int completed_count;
	pthread_mutex_t completed_mutex;
	
	// Cache
	cache_entry_t cache[MAX_CACHE_ENTRIES];
	int cache_size;
	int cache_clock;  // For LRU
	pthread_mutex_t cache_mutex;
} tts_state_t;

static tts_state_t tts_state;

// Forward declarations
static void* TTS_WorkerThread( void *arg );
static qboolean TTS_SpawnPiper( const char *text, const tts_voice_params_t *params, tts_audio_buffer_t **out_audio );
static qboolean TTS_ParseWAV( const byte *wav_data, int wav_size, tts_audio_buffer_t **out_audio );
static unsigned int TTS_HashText( const char *text, const tts_voice_params_t *params );
static void TTS_ProcessCompletedRequests( void );

/*
================
TTS_DefaultParams
================
*/
void TTS_DefaultParams( tts_voice_params_t *params ) {
	if ( !params ) {
		return;
	}
	memset( params, 0, sizeof( tts_voice_params_t ) );
	params->gain = 1.0f;
	params->pitch_shift_semitones = 0.0f;
	params->speed = 1.0f;
	params->shout = qfalse;
}

/*
================
TTS_HashText
================
*/
static unsigned int TTS_HashText( const char *text, const tts_voice_params_t *params ) {
	unsigned int hash = 0;
	const char *p = text;
	
	// Hash text
	while ( *p ) {
		hash = hash * 31 + (unsigned char)*p;
		p++;
	}
	
	// Hash params
	if ( params ) {
		unsigned int *params_hash = (unsigned int*)params;
		hash ^= params_hash[0];
		hash ^= params_hash[1];
		hash ^= params_hash[2];
		hash ^= (unsigned int)params->shout;
	}
	
	return hash;
}

/*
================
TTS_ParseWAV
================
Parse WAV file data and extract PCM audio
================
*/
static qboolean TTS_ParseWAV( const byte *wav_data, int wav_size, tts_audio_buffer_t **out_audio ) {
	const byte *p = wav_data;
	const byte *end = wav_data + wav_size;
	
	if ( !wav_data || wav_size < 44 ) {  // Minimum WAV header size
		return qfalse;
	}
	
	// Check RIFF header
	if ( wav_size < 12 || memcmp( p, "RIFF", 4 ) != 0 ) {
		return qfalse;
	}
	p += 8;  // Skip RIFF and size
	
	// Check WAVE header
	if ( p + 4 > end || memcmp( p, "WAVE", 4 ) != 0 ) {
		return qfalse;
	}
	p += 4;
	
	// Find fmt chunk
	qboolean found_fmt = qfalse;
	int sample_rate = 22050;
	int channels = 1;
	int bits_per_sample = 16;
	int data_offset = 0;
	int data_size = 0;
	
	while ( p + 8 <= end ) {
		char chunk_id[4];
		int chunk_size;
		
		memcpy( chunk_id, p, 4 );
		p += 4;
		chunk_size = *(int*)p;
		p += 4;
		
		if ( memcmp( chunk_id, "fmt ", 4 ) == 0 ) {
			if ( chunk_size >= 16 && p + chunk_size <= end ) {
				(void)*(short*)( p + 0 ); // audio_format - unused but parsed
				channels = *(short*)( p + 2 );
				sample_rate = *(int*)( p + 4 );
				bits_per_sample = *(short*)( p + 14 );
				found_fmt = qtrue;
			}
			p += chunk_size;
		} else if ( memcmp( chunk_id, "data", 4 ) == 0 ) {
			data_offset = p - wav_data;
			data_size = chunk_size;
			p += chunk_size;
			break;
		} else {
			p += chunk_size;
		}
		
		// Align to word boundary
		if ( chunk_size % 2 ) {
			p++;
		}
	}
	
	if ( !found_fmt || data_size == 0 ) {
		return qfalse;
	}
	
	// Allocate audio buffer
	tts_audio_buffer_t *audio = (tts_audio_buffer_t*)malloc( sizeof( tts_audio_buffer_t ) );
	audio->sample_rate = sample_rate;
	audio->channels = channels;
	audio->bits_per_sample = bits_per_sample;
	audio->size = data_size;
	audio->data = (byte*)malloc( data_size );
	
	// Copy PCM data
	if ( wav_data + data_offset + data_size <= end ) {
		memcpy( audio->data, wav_data + data_offset, data_size );
	} else {
		// Truncate if needed
		int copy_size = end - ( wav_data + data_offset );
		if ( copy_size > 0 ) {
			memcpy( audio->data, wav_data + data_offset, copy_size );
			audio->size = copy_size;
		} else {
			free( audio->data );
			free( audio );
			return qfalse;
		}
	}
	
	*out_audio = audio;
	return qtrue;
}

/*
================
TTS_SpawnPiper
================
Spawn Piper process and synthesize text using fork/exec with pipes
================
*/
static qboolean TTS_SpawnPiper( const char *text, const tts_voice_params_t *params, tts_audio_buffer_t **out_audio ) {
	int stdin_pipe[2];
	int stdout_pipe[2];
	pid_t pid;
	byte *wav_buffer = NULL;
	int wav_size = 0;
	int wav_capacity = 65536;  // 64KB initial
	qboolean success = qfalse;
	char piper_path[MAX_QPATH];
	char model_path[MAX_QPATH];
	char config_path[MAX_QPATH];
	int timeout_ms;
	int start_time;
	
	if ( !text || !text[0] ) {
		return qfalse;
	}
	
	// Get absolute paths
	Q_strncpyz( piper_path, g_tts_piper_path.string, sizeof( piper_path ) );
	Q_strncpyz( model_path, g_tts_model_path.string, sizeof( model_path ) );
	Q_strncpyz( config_path, g_tts_model_config_path.string, sizeof( config_path ) );
	timeout_ms = g_tts_timeout.integer;
	if ( timeout_ms <= 0 ) {
		timeout_ms = 500;  // Default 500ms
	}
	
	if ( g_tts_debug.integer ) {
		// G_Printf( "^3[TTS] Synthesizing: %s (timeout: %dms)\n", text, timeout_ms );
	}
	
	// start_time = trap_Milliseconds(); // trap_Milliseconds is not thread-safe!
	start_time = (int)time(NULL) * 1000;

	// Create pipes
	if ( pipe( stdin_pipe ) < 0 || pipe( stdout_pipe ) < 0 ) {
		// if ( g_tts_debug.integer ) {
		// 	G_Printf( "^1[TTS] Failed to create pipes: %s\n", strerror( errno ) );
		// }
		return qfalse;
	}
	
	// Fork process
	pid = fork();
	if ( pid < 0 ) {
		// if ( g_tts_debug.integer ) {
		// 	G_Printf( "^1[TTS] Failed to fork process: %s\n", strerror( errno ) );
		// }
		close( stdin_pipe[0] );
		close( stdin_pipe[1] );
		close( stdout_pipe[0] );
		close( stdout_pipe[1] );
		return qfalse;
	}
	
	if ( pid == 0 ) {
		// Child process
		close( stdin_pipe[1] );  // Close write end
		close( stdout_pipe[0] );  // Close read end
		
		// Redirect stdin/stdout
		dup2( stdin_pipe[0], STDIN_FILENO );
		dup2( stdout_pipe[1], STDOUT_FILENO );
		
		close( stdin_pipe[0] );
		close( stdout_pipe[1] );
		
		// Set LD_LIBRARY_PATH to find piper's shared libraries
		// Extract directory from piper_path
		char lib_dir[MAX_QPATH];
		char *last_slash = strrchr( piper_path, '/' );
		if ( last_slash ) {
			int dir_len = last_slash - piper_path;
			Q_strncpyz( lib_dir, piper_path, dir_len + 1 );
			setenv( "LD_LIBRARY_PATH", lib_dir, 1 );
		}
		
		// Execute Piper
		// Pass speaker ID based on shout param
		// 4 = Neutral, 1 = Angry/Shout (based on Thorsten emotional model config)
		const char *speaker_id = params && params->shout ? "1" : "4";
		const char *length_scale = params && params->shout ? "0.75" : "1.0";
		const char *noise_scale = params && params->shout ? "0.9" : "0.667";
		const char *noise_w = params && params->shout ? "1.2" : "0.8";
		
		execl( piper_path, "piper", 
			"-m", model_path, 
			"-c", config_path, 
			"--speaker", speaker_id, 
			"--length_scale", length_scale,
			"--noise_scale", noise_scale,
			"--noise_w", noise_w,
			"-f", "-", 
			NULL );
		
		// If we get here, exec failed
		_exit( 1 );
	}
	
	// Parent process
	close( stdin_pipe[0] );  // Close read end
	close( stdout_pipe[1] );  // Close write end
	
	// Write text to stdin
	int text_len = strlen( text );
	int written = write( stdin_pipe[1], text, text_len );
	close( stdin_pipe[1] );
	
	if ( written != text_len ) {
		// if ( g_tts_debug.integer ) {
		// 	G_Printf( "^1[TTS] Failed to write text to Piper\n" );
		// }
		close( stdout_pipe[0] );
		waitpid( pid, NULL, 0 );
		return qfalse;
	}
	
	// Read WAV data from stdout with timeout
	wav_buffer = (byte*)malloc( wav_capacity );
	
	while ( 1 ) {
		// Check timeout
		if ( (int)time(NULL) * 1000 - start_time > timeout_ms ) {
			// if ( g_tts_debug.integer ) {
			// 	G_Printf( "^1[TTS] Synthesis timeout after %dms\n", timeout_ms );
			// }
			// Kill child process
			kill( pid, SIGTERM );
			waitpid( pid, NULL, 0 );
			close( stdout_pipe[0] );
			free( wav_buffer );
			return qfalse;
		}
		
		// Use select/poll for non-blocking read, but for simplicity, use a small read with timeout check
		fd_set readfds;
		struct timeval tv;
		FD_ZERO( &readfds );
		FD_SET( stdout_pipe[0], &readfds );
		tv.tv_sec = 0;
		tv.tv_usec = 100000;  // 100ms
		
		int ret = select( stdout_pipe[0] + 1, &readfds, NULL, NULL, &tv );
		if ( ret > 0 && FD_ISSET( stdout_pipe[0], &readfds ) ) {
			int bytes_read = read( stdout_pipe[0], wav_buffer + wav_size, wav_capacity - wav_size );
			if ( bytes_read > 0 ) {
				wav_size += bytes_read;
				if ( wav_size >= wav_capacity - 1024 ) {
					// Expand buffer
					wav_capacity *= 2;
					byte *new_buffer = (byte*)malloc( wav_capacity );
					memcpy( new_buffer, wav_buffer, wav_size );
					free( wav_buffer );
					wav_buffer = new_buffer;
				}
			} else if ( bytes_read == 0 ) {
				// EOF
				break;
			} else if ( errno != EINTR ) {
				// Error
				break;
			}
		} else if ( ret == 0 ) {
			// Timeout, continue loop to check overall timeout
			continue;
		} else {
			// Select error
			break;
		}
	}
	
	close( stdout_pipe[0] );
	
	// Wait for child to exit
	int status;
	waitpid( pid, &status, 0 );
	
	if ( WIFEXITED( status ) && WEXITSTATUS( status ) != 0 ) {
		// if ( g_tts_debug.integer ) {
		// 	G_Printf( "^1[TTS] Piper process exited with status %d\n", WEXITSTATUS( status ) );
		// }
		free( wav_buffer );
		return qfalse;
	}
	
	if ( wav_size == 0 ) {
		// if ( g_tts_debug.integer ) {
		// 	G_Printf( "^1[TTS] No data received from Piper\n" );
		// }
		free( wav_buffer );
		return qfalse;
	}
	
	// Parse WAV
	success = TTS_ParseWAV( wav_buffer, wav_size, out_audio );
	
	free( wav_buffer );
	
	// if ( success && g_tts_debug.integer ) {
	// 	int elapsed = trap_Milliseconds() - start_time;
	// 	G_Printf( "^2[TTS] Successfully synthesized %d bytes of audio in %dms\n", (*out_audio)->size, elapsed );
	// }
	
	return success;
}

/*
================
TTS_LoadFromCache
================
*/
qboolean TTS_LoadFromCache( const char *text, const tts_voice_params_t *params, tts_audio_buffer_t **out_audio ) {
	unsigned int hash = TTS_HashText( text, params );
	int i;
	qboolean found = qfalse;
	
	if ( !tts_state.initialized || !g_tts_enable.integer ) {
		return qfalse;
	}
	
	pthread_mutex_lock( &tts_state.cache_mutex );
	
	for ( i = 0; i < tts_state.cache_size; i++ ) {
		if ( tts_state.cache[i].hash == hash && tts_state.cache[i].audio != NULL ) {
			// Cache hit - copy audio buffer
			tts_audio_buffer_t *cached = tts_state.cache[i].audio;
			tts_audio_buffer_t *audio = (tts_audio_buffer_t*)malloc( sizeof( tts_audio_buffer_t ) );
			
			if ( !audio ) {
				pthread_mutex_unlock( &tts_state.cache_mutex );
				return qfalse;
			}
			
			audio->sample_rate = cached->sample_rate;
			audio->channels = cached->channels;
			audio->bits_per_sample = cached->bits_per_sample;
			audio->size = cached->size;
			audio->data = (byte*)malloc( audio->size );
			
			if ( !audio->data ) {
				free( audio );
				pthread_mutex_unlock( &tts_state.cache_mutex );
				return qfalse;
			}
			
			memcpy( audio->data, cached->data, audio->size );
			
			tts_state.cache[i].last_used = tts_state.cache_clock++;
			*out_audio = audio;
			found = qtrue;
			
			if ( g_tts_debug.integer ) {
				G_Printf( "^2[TTS] Cache hit for text hash 0x%08x\n", hash );
			}
			
			break;
		}
	}
	
	pthread_mutex_unlock( &tts_state.cache_mutex );
	
	return found;
}

/*
================
TTS_SaveToCache
================
*/
void TTS_SaveToCache( const char *text, const tts_voice_params_t *params, const tts_audio_buffer_t *audio ) {
	unsigned int hash = TTS_HashText( text, params );
	int i, oldest_idx = 0;
	int oldest_time;
	
	if ( !tts_state.initialized || !g_tts_enable.integer || !audio || !audio->data ) {
		return;
	}
	
	pthread_mutex_lock( &tts_state.cache_mutex );
	
	// Check if already cached
	for ( i = 0; i < tts_state.cache_size; i++ ) {
		if ( tts_state.cache[i].hash == hash ) {
			// Update existing entry
			tts_state.cache[i].last_used = tts_state.cache_clock++;
			pthread_mutex_unlock( &tts_state.cache_mutex );
			return;
		}
	}
	
	// Find oldest entry for eviction (only if cache has entries)
	if ( tts_state.cache_size > 0 ) {
		oldest_time = tts_state.cache[0].last_used;
		for ( i = 1; i < tts_state.cache_size; i++ ) {
			if ( tts_state.cache[i].last_used < oldest_time ) {
				oldest_time = tts_state.cache[i].last_used;
				oldest_idx = i;
			}
		}
	}
	
	// Evict oldest if cache is full
	if ( tts_state.cache_size >= MAX_CACHE_ENTRIES ) {
		if ( tts_state.cache[oldest_idx].audio ) {
			TTS_FreeAudioBuffer( tts_state.cache[oldest_idx].audio );
			tts_state.cache[oldest_idx].audio = NULL;
		}
		tts_state.cache[oldest_idx].hash = hash;
		tts_state.cache[oldest_idx].last_used = tts_state.cache_clock++;
		
		// Copy audio buffer
		tts_audio_buffer_t *cached = (tts_audio_buffer_t*)malloc( sizeof( tts_audio_buffer_t ) );
		if ( cached ) {
			cached->sample_rate = audio->sample_rate;
			cached->channels = audio->channels;
			cached->bits_per_sample = audio->bits_per_sample;
			cached->size = audio->size;
			cached->data = (byte*)malloc( cached->size );
			
			if ( cached->data ) {
				memcpy( cached->data, audio->data, cached->size );
				tts_state.cache[oldest_idx].audio = cached;
			} else {
				free( cached );
			}
		}
	} else {
		// Add new entry
		tts_state.cache[tts_state.cache_size].hash = hash;
		tts_state.cache[tts_state.cache_size].last_used = tts_state.cache_clock++;
		
		tts_audio_buffer_t *cached = (tts_audio_buffer_t*)malloc( sizeof( tts_audio_buffer_t ) );
		if ( cached ) {
			cached->sample_rate = audio->sample_rate;
			cached->channels = audio->channels;
			cached->bits_per_sample = audio->bits_per_sample;
			cached->size = audio->size;
			cached->data = (byte*)malloc( cached->size );
			
			if ( cached->data ) {
				memcpy( cached->data, audio->data, cached->size );
				tts_state.cache[tts_state.cache_size].audio = cached;
				tts_state.cache_size++;
			} else {
				free( cached );
			}
		}
	}
	
	pthread_mutex_unlock( &tts_state.cache_mutex );
	
	if ( g_tts_debug.integer ) {
		G_Printf( "^2[TTS] Cached audio for text hash 0x%08x\n", hash );
	}
}

/*
================
TTS_WorkerThread
================
Worker thread that processes synthesis requests
================
*/
static void* TTS_WorkerThread( void *arg ) {
	tts_request_t *request;
	
	while ( !tts_state.shutdown_requested ) {
		// Wait for requests
		pthread_mutex_lock( &tts_state.queue_mutex );
		
		while ( tts_state.queue_count == 0 && !tts_state.shutdown_requested ) {
			pthread_cond_wait( &tts_state.queue_condition, &tts_state.queue_mutex );
		}
		
		if ( tts_state.shutdown_requested ) {
			pthread_mutex_unlock( &tts_state.queue_mutex );
			break;
		}
		
		// Get next request
		request = &tts_state.request_queue[tts_state.queue_head];
		tts_state.queue_head = ( tts_state.queue_head + 1 ) % MAX_TTS_REQUESTS;
		tts_state.queue_count--;
		
		pthread_mutex_unlock( &tts_state.queue_mutex );
		
		// Check cache first
		if ( !TTS_LoadFromCache( request->text, &request->params, &request->audio ) ) {
			// Synthesize
			if ( TTS_SpawnPiper( request->text, &request->params, &request->audio ) ) {
				// Save to cache
				if ( request->audio ) {
					TTS_SaveToCache( request->text, &request->params, request->audio );
				}
			} else {
				request->audio = NULL;
			}
		}
		
		// Mark as ready and add to completed queue
		request->ready = qtrue;
		
		pthread_mutex_lock( &tts_state.completed_mutex );
		if ( tts_state.completed_count < MAX_TTS_REQUESTS ) {
			memcpy( &tts_state.completed_requests[tts_state.completed_count], request, sizeof( tts_request_t ) );
			tts_state.completed_count++;
		}
		pthread_mutex_unlock( &tts_state.completed_mutex );
	}
	
	return NULL;
}

/*
================
TTS_Init
================
*/
qboolean TTS_Init( void ) {
	char piper_path[MAX_QPATH];
	
	G_Printf( "[TTS] TTS_Init called\n" );
	
	if ( tts_state.initialized ) {
		G_Printf( "[TTS] Already initialized\n" );
		return qtrue;
	}
	
	memset( &tts_state, 0, sizeof( tts_state_t ) );
	
	// Check if TTS is enabled
	G_Printf( "[TTS] Checking g_tts_enable: %d\n", g_tts_enable.integer );
	if ( !g_tts_enable.integer ) {
		G_Printf( "[TTS] TTS disabled via CVAR\n" );
		return qfalse;
	}
	
	// Verify Piper binary exists using direct filesystem access (needed for execvp)
	Q_strncpyz( piper_path, g_tts_piper_path.string, sizeof( piper_path ) );
	char alt_path[MAX_QPATH];
	Q_strncpyz( alt_path, piper_path, sizeof( alt_path ) );
	if ( alt_path[0] == 'm' && alt_path[1] == 'a' && alt_path[2] == 'i' && alt_path[3] == 'n' ) {
		alt_path[0] = 'M';
	}
	
	// Use access() to check if file exists and is executable (not game's virtual FS)
	qboolean found = qfalse;
	if ( access( piper_path, F_OK | X_OK ) == 0 ) {
		found = qtrue;
	} else if ( access( alt_path, F_OK | X_OK ) == 0 ) {
		Q_strncpyz( piper_path, alt_path, sizeof( piper_path ) );
		trap_Cvar_Set( "g_tts_piper_path", alt_path );
		G_Printf( "^2[TTS] Using alternative path: %s\n", alt_path );
		found = qtrue;
	}
	
	if ( !found ) {
		G_Printf( "^3[TTS] Piper binary not found at: %s (also tried: %s)\n", piper_path, alt_path );
		G_Printf( "^3[TTS] TTS disabled. Run 'make setup-tts' to download Piper.\n" );
		return qfalse;
	}
	
	// Verify model files exist using direct filesystem access (with fallback for capital M)
	char model_path[MAX_QPATH];
	char config_path[MAX_QPATH];
	char alt_model_path[MAX_QPATH];
	char alt_config_path[MAX_QPATH];
	Q_strncpyz( model_path, g_tts_model_path.string, sizeof( model_path ) );
	Q_strncpyz( config_path, g_tts_model_config_path.string, sizeof( config_path ) );
	Q_strncpyz( alt_model_path, model_path, sizeof( alt_model_path ) );
	Q_strncpyz( alt_config_path, config_path, sizeof( alt_config_path ) );
	if ( alt_model_path[0] == 'm' && alt_model_path[1] == 'a' && alt_model_path[2] == 'i' && alt_model_path[3] == 'n' ) {
		alt_model_path[0] = 'M';
	}
	if ( alt_config_path[0] == 'm' && alt_config_path[1] == 'a' && alt_config_path[2] == 'i' && alt_config_path[3] == 'n' ) {
		alt_config_path[0] = 'M';
	}
	
	// Check model file
	qboolean model_found = qfalse;
	if ( access( model_path, F_OK | R_OK ) == 0 ) {
		model_found = qtrue;
	} else if ( access( alt_model_path, F_OK | R_OK ) == 0 ) {
		Q_strncpyz( model_path, alt_model_path, sizeof( model_path ) );
		trap_Cvar_Set( "g_tts_model_path", alt_model_path );
		model_found = qtrue;
	}
	
	if ( !model_found ) {
		G_Printf( "^3[TTS] Model file not found: %s (also tried: %s)\n", g_tts_model_path.string, alt_model_path );
		return qfalse;
	}
	
	// Check config file (optional but preferred)
	qboolean config_found = qfalse;
	if ( access( config_path, F_OK | R_OK ) == 0 ) {
		config_found = qtrue;
	} else if ( access( alt_config_path, F_OK | R_OK ) == 0 ) {
		Q_strncpyz( config_path, alt_config_path, sizeof( config_path ) );
		trap_Cvar_Set( "g_tts_model_config_path", alt_config_path );
		config_found = qtrue;
	}
	
	if ( !config_found ) {
		G_Printf( "^3[TTS] Warning: Model config file not found (optional): %s\n", g_tts_model_config_path.string );
	}
	
	// Initialize mutexes
	if ( pthread_mutex_init( &tts_state.queue_mutex, NULL ) != 0 ) {
		G_Printf( "^1[TTS] Failed to initialize queue mutex\n" );
		return qfalse;
	}
	
	if ( pthread_mutex_init( &tts_state.completed_mutex, NULL ) != 0 ) {
		G_Printf( "^1[TTS] Failed to initialize completed mutex\n" );
		pthread_mutex_destroy( &tts_state.queue_mutex );
		return qfalse;
	}
	
	if ( pthread_mutex_init( &tts_state.cache_mutex, NULL ) != 0 ) {
		G_Printf( "^1[TTS] Failed to initialize cache mutex\n" );
		pthread_mutex_destroy( &tts_state.queue_mutex );
		pthread_mutex_destroy( &tts_state.completed_mutex );
		return qfalse;
	}
	
	if ( pthread_cond_init( &tts_state.queue_condition, NULL ) != 0 ) {
		G_Printf( "^1[TTS] Failed to initialize condition variable\n" );
		pthread_mutex_destroy( &tts_state.queue_mutex );
		pthread_mutex_destroy( &tts_state.completed_mutex );
		pthread_mutex_destroy( &tts_state.cache_mutex );
		return qfalse;
	}
	
	// Start worker thread
	tts_state.shutdown_requested = qfalse;
	if ( pthread_create( &tts_state.worker_thread, NULL, TTS_WorkerThread, NULL ) != 0 ) {
		G_Printf( "^1[TTS] Failed to create worker thread\n" );
		pthread_mutex_destroy( &tts_state.queue_mutex );
		pthread_mutex_destroy( &tts_state.completed_mutex );
		pthread_mutex_destroy( &tts_state.cache_mutex );
		pthread_cond_destroy( &tts_state.queue_condition );
		return qfalse;
	}
	
	tts_state.initialized = qtrue;
	tts_state.enabled = qtrue;
	
	G_Printf( "^2[TTS] Initialized successfully\n" );
	G_Printf( "^2[TTS] Piper: %s\n", piper_path );
	G_Printf( "^2[TTS] Model: %s\n", g_tts_model_path.string );
	G_Printf( "^2[TTS] Ready for synthesis\n" );
	
	return qtrue;
}

/*
================
TTS_Shutdown
================
*/
void TTS_Shutdown( void ) {
	int i;
	
	if ( !tts_state.initialized ) {
		return;
	}
	
	// Signal shutdown
	tts_state.shutdown_requested = qtrue;
	pthread_cond_broadcast( &tts_state.queue_condition );
	
	// Wait for worker thread
	if ( tts_state.worker_thread ) {
		pthread_join( tts_state.worker_thread, NULL );
	}
	
	// Cleanup mutexes
	pthread_mutex_destroy( &tts_state.queue_mutex );
	pthread_mutex_destroy( &tts_state.completed_mutex );
	pthread_mutex_destroy( &tts_state.cache_mutex );
	pthread_cond_destroy( &tts_state.queue_condition );
	
	// Free cache
	pthread_mutex_lock( &tts_state.cache_mutex );
	for ( i = 0; i < tts_state.cache_size; i++ ) {
		if ( tts_state.cache[i].audio ) {
			TTS_FreeAudioBuffer( tts_state.cache[i].audio );
			tts_state.cache[i].audio = NULL;
		}
	}
	pthread_mutex_unlock( &tts_state.cache_mutex );
	
	memset( &tts_state, 0, sizeof( tts_state_t ) );
	
	G_Printf( "^2[TTS] Shutdown complete\n" );
}

/*
================
TTS_IsEnabled
================
*/
qboolean TTS_IsEnabled( void ) {
	return tts_state.initialized && tts_state.enabled && g_tts_enable.integer;
}

/*
================
TTS_Synthesize
================
Queue a synthesis request (non-blocking)
================
*/
qboolean TTS_Synthesize( const char *text, const tts_voice_params_t *params, int entity_num, tts_audio_buffer_t **out_audio ) {
	tts_request_t *request;
	
	if ( !TTS_IsEnabled() ) {
		return qfalse;
	}
	
	if ( !text || !text[0] ) {
		return qfalse;
	}
	
	// Check cache first (synchronous)
	if ( TTS_LoadFromCache( text, params, out_audio ) ) {
		return qtrue;
	}
	
	// Queue request
	pthread_mutex_lock( &tts_state.queue_mutex );
	
	if ( tts_state.queue_count >= MAX_TTS_REQUESTS ) {
		// Drop the oldest request to make room instead of failing
		tts_state.queue_head = ( tts_state.queue_head + 1 ) % MAX_TTS_REQUESTS;
		tts_state.queue_count--;
		if ( g_tts_debug.integer ) {
			G_Printf( "^3[TTS] Request queue full, dropping oldest and enqueueing new request\n" );
		}
	}
	
	request = &tts_state.request_queue[tts_state.queue_tail];
	memset( request, 0, sizeof( tts_request_t ) );
	Q_strncpyz( request->text, text, sizeof( request->text ) );
	if ( params ) {
		memcpy( &request->params, params, sizeof( tts_voice_params_t ) );
	} else {
		TTS_DefaultParams( &request->params );
	}
	request->entity_num = entity_num;
	request->timestamp = level.time;
	request->ready = qfalse;
	request->audio = NULL;
	
	tts_state.queue_tail = ( tts_state.queue_tail + 1 ) % MAX_TTS_REQUESTS;
	tts_state.queue_count++;
	
	pthread_cond_signal( &tts_state.queue_condition );
	pthread_mutex_unlock( &tts_state.queue_mutex );
	
	// Return NULL for now - caller should check TTS_Update() later
	if ( out_audio ) {
		*out_audio = NULL;
	}
	
	return qtrue;
}

/*
================
TTS_ProcessCompletedRequests
================
Process completed synthesis requests (called from main thread)
================
*/
static void TTS_ProcessCompletedRequests( void ) {
	int i;
	
	pthread_mutex_lock( &tts_state.completed_mutex );
	
	for ( i = 0; i < tts_state.completed_count; i++ ) {
		tts_request_t *req = &tts_state.completed_requests[i];
		
		if ( req->ready && req->audio ) {
			// Request completed - audio is ready
			// Validate text is not NULL or empty before processing
			if ( !req->text[0] ) {
				G_Printf( "^3[TTS] ERROR: Completed request has NULL or empty text, skipping registration\n" );
				TTS_FreeAudioBuffer( req->audio );
				req->audio = NULL;
				continue;
			}
			
			// Automatically register and play the audio
			if ( req->entity_num >= 0 && req->entity_num < MAX_GENTITIES ) {
				gentity_t *ent = &g_entities[req->entity_num];
				if ( ent && ent->inuse ) {
					// Register audio as sound and play it
					int sound_index = TTS_RegisterAudioAsSound( req->audio, req->text );
					if ( sound_index > 0 ) {
						G_AddEvent( ent, EV_GENERAL_SOUND, sound_index );
						const char *entity_name = ( ent->aiName && ent->aiName[0] ) ? ent->aiName : va("entity %d", req->entity_num);
						G_Printf( "^2[TTS] Playing audio for %s: %s\n", entity_name, req->text );
						if ( g_tts_debug.integer ) {
							G_Printf( "^2[TTS] Sound index: %d, Audio size: %d bytes\n", 
							         sound_index, req->audio->size );
						}
					} else {
						G_Printf( "^3[TTS] Failed to register audio for entity %d: %s\n", 
						         req->entity_num, req->text );
					}
				} else {
					G_Printf( "^3[TTS] WARNING: Entity %d not in use, skipping audio playback\n", req->entity_num );
				}
			}
			
			// Free the audio buffer (it's been saved to disk)
			TTS_FreeAudioBuffer( req->audio );
			req->audio = NULL;
		}
	}
	
	tts_state.completed_count = 0;
	
	pthread_mutex_unlock( &tts_state.completed_mutex );
}

/*
================
TTS_Update
================
Call each frame to process completed requests
================
*/
void TTS_Update( void ) {
	if ( !TTS_IsEnabled() ) {
		return;
	}
	
	TTS_ProcessCompletedRequests();
}

/*
================
TTS_FreeAudioBuffer
================
*/
void TTS_FreeAudioBuffer( tts_audio_buffer_t *audio ) {
	if ( !audio ) {
		return;
	}
	
	if ( audio->data ) {
		free( audio->data );
	}
	
	free( audio );
}

/*
================
TTS_WriteWAVFile
================
Write TTS audio buffer to a WAV file that can be registered as a sound
================
*/
static qboolean TTS_WriteWAVFile( const tts_audio_buffer_t *audio, const char *filename ) {
	fileHandle_t f;
	byte wav_header[44];
	int i;
	unsigned int chunk_size;
	unsigned int data_size;
	unsigned int byte_rate;
	unsigned short block_align;
	
	if ( !audio || !audio->data || !filename ) {
		return qfalse;
	}
	
	// Open file for writing
	if ( trap_FS_FOpenFile( filename, &f, FS_WRITE ) < 0 ) {
		if ( g_tts_debug.integer ) {
			G_Printf( "^1[TTS] Failed to open file for writing: %s\n", filename );
		}
		return qfalse;
	}
	
	// Calculate WAV header values
	data_size = audio->size;
	byte_rate = audio->sample_rate * audio->channels * ( audio->bits_per_sample / 8 );
	block_align = audio->channels * ( audio->bits_per_sample / 8 );
	chunk_size = 36 + data_size;
	
	// Write WAV header
	i = 0;
	memcpy( wav_header + i, "RIFF", 4 ); i += 4;
	*(unsigned int*)( wav_header + i ) = chunk_size; i += 4;
	memcpy( wav_header + i, "WAVE", 4 ); i += 4;
	memcpy( wav_header + i, "fmt ", 4 ); i += 4;
	*(unsigned int*)( wav_header + i ) = 16; i += 4;  // fmt chunk size
	*(unsigned short*)( wav_header + i ) = 1; i += 2;  // audio format (PCM)
	*(unsigned short*)( wav_header + i ) = audio->channels; i += 2;
	*(unsigned int*)( wav_header + i ) = audio->sample_rate; i += 4;
	*(unsigned int*)( wav_header + i ) = byte_rate; i += 4;
	*(unsigned short*)( wav_header + i ) = block_align; i += 2;
	*(unsigned short*)( wav_header + i ) = audio->bits_per_sample; i += 2;
	memcpy( wav_header + i, "data", 4 ); i += 4;
	*(unsigned int*)( wav_header + i ) = data_size; i += 4;
	
	// Write header
	if ( trap_FS_Write( wav_header, 44, f ) != 44 ) {
		trap_FS_FCloseFile( f );
		if ( g_tts_debug.integer ) {
			G_Printf( "^1[TTS] Failed to write WAV header\n" );
		}
		return qfalse;
	}
	
	// Write PCM data
	if ( trap_FS_Write( audio->data, audio->size, f ) != audio->size ) {
		trap_FS_FCloseFile( f );
		if ( g_tts_debug.integer ) {
			G_Printf( "^1[TTS] Failed to write PCM data\n" );
		}
		return qfalse;
	}
	
	trap_FS_FCloseFile( f );
	
	return qtrue;
}

/*
================
TTS_RegisterAudioAsSound
================
Register TTS audio as a game sound and return sound index
================
*/
int TTS_RegisterAudioAsSound( const tts_audio_buffer_t *audio, const char *text ) {
	char filename[MAX_QPATH];
	char cache_dir[MAX_QPATH];
	unsigned int hash;
	int sound_index;
	
	if ( !audio || !text || !text[0] ) {
		if ( g_tts_debug.integer ) {
			G_Printf( "^3[TTS] TTS_RegisterAudioAsSound: Invalid parameters (audio=%p, text=%p)\n", audio, text );
		}
		return 0;
	}
	
	// Generate filename based on text hash
	hash = TTS_HashText( text, NULL );
	Com_sprintf( cache_dir, sizeof( cache_dir ), "main/tts/cache" );
	Com_sprintf( filename, sizeof( filename ), "%s/tts_%08x.wav", cache_dir, hash );
	
	// Validate filename is not empty
	if ( !filename[0] ) {
		if ( g_tts_debug.integer ) {
			G_Printf( "^3[TTS] TTS_RegisterAudioAsSound: Generated filename is empty\n" );
		}
		return 0;
	}
	
	// Write WAV file
	if ( !TTS_WriteWAVFile( audio, filename ) ) {
		if ( g_tts_debug.integer ) {
			G_Printf( "^3[TTS] TTS_RegisterAudioAsSound: Failed to write WAV file: %s\n", filename );
		}
		return 0;
	}
	
	// Register as sound - ensure filename is valid before calling
	if ( !filename[0] ) {
		G_Printf( "^1[TTS] ERROR: Filename is empty when registering sound!\n" );
		return 0;
	}
	
	// Double-check filename is valid (not null pointer, not empty, reasonable length)
	if ( strlen( filename ) == 0 || strlen( filename ) >= MAX_QPATH ) {
		G_Printf( "^1[TTS] ERROR: Invalid filename length when registering sound: %d\n", (int)strlen( filename ) );
		return 0;
	}
	
	// Log the filename we're about to register (for debugging)
	if ( g_tts_debug.integer >= 2 ) {
		G_Printf( "^2[TTS] Registering sound with filename: %s (length: %d)\n", filename, (int)strlen( filename ) );
	}
	
	sound_index = G_SoundIndex( filename );
	
	if ( sound_index <= 0 ) {
		if ( g_tts_debug.integer ) {
			G_Printf( "^3[TTS] TTS_RegisterAudioAsSound: G_SoundIndex returned invalid index %d for: %s\n", sound_index, filename );
		}
		return 0;
	}
	
	if ( g_tts_debug.integer ) {
		G_Printf( "^2[TTS] Registered TTS audio as sound: %s (index %d)\n", filename, sound_index );
	}
	
	return sound_index;
}

