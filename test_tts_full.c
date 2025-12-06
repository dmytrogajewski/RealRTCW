/*
===========================================================================
TTS Full Functionality Test
Tests complete TTS pipeline including WAV parsing
===========================================================================
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <signal.h>
#include <errno.h>
#include <stdint.h>
#include <time.h>

#define MAX_QPATH 256
#define qboolean int
#define qtrue 1
#define qfalse 0

// Audio buffer structure (matching game code)
typedef struct {
	uint8_t *data;
	int size;
	int sample_rate;
	int channels;
	int bits_per_sample;
} tts_audio_buffer_t;

// Parse WAV data (matching game code logic)
qboolean parse_wav(const uint8_t *wav_data, int wav_size, tts_audio_buffer_t **out_audio) {
	const uint8_t *p = wav_data;
	const uint8_t *end = wav_data + wav_size;
	qboolean found_fmt = qfalse;
	int sample_rate = 22050;
	int channels = 1;
	int bits_per_sample = 16;
	int data_offset = 0;
	int data_size = 0;
	
	// Check RIFF header
	if (wav_size < 12 || memcmp(p, "RIFF", 4) != 0) {
		printf("ERROR: Not a valid RIFF file\n");
		return qfalse;
	}
	p += 8; // Skip RIFF and size
	
	// Check WAVE header
	if (memcmp(p, "WAVE", 4) != 0) {
		printf("ERROR: Not a WAVE file\n");
		return qfalse;
	}
	p += 4;
	
	// Find fmt chunk
	while (p < end - 8) {
		char chunk_id[4];
		uint32_t chunk_size;
		
		memcpy(chunk_id, p, 4);
		memcpy(&chunk_size, p + 4, 4);
		p += 8;
		
		if (memcmp(chunk_id, "fmt ", 4) == 0) {
			if (chunk_size >= 16 && p + chunk_size <= end) {
				uint16_t audio_format;
				memcpy(&audio_format, p, 2);
				if (audio_format == 1) { // PCM
					memcpy(&channels, p + 2, 2);
					memcpy(&sample_rate, p + 4, 4);
					memcpy(&bits_per_sample, p + 14, 2);
					found_fmt = qtrue;
				}
			}
			p += chunk_size;
		} else if (memcmp(chunk_id, "data", 4) == 0) {
			data_offset = p - wav_data;
			data_size = chunk_size;
			p += chunk_size;
		} else {
			p += chunk_size;
		}
		
		// Align to word boundary
		if (chunk_size % 2) {
			p++;
		}
	}
	
	if (!found_fmt || data_size == 0) {
		printf("ERROR: Could not find fmt or data chunk\n");
		return qfalse;
	}
	
	// Allocate audio buffer
	tts_audio_buffer_t *audio = malloc(sizeof(tts_audio_buffer_t));
	audio->sample_rate = sample_rate;
	audio->channels = channels;
	audio->bits_per_sample = bits_per_sample;
	audio->size = data_size;
	audio->data = malloc(data_size);
	
	// Copy PCM data
	if (wav_data + data_offset + data_size <= end) {
		memcpy(audio->data, wav_data + data_offset, data_size);
	} else {
		int copy_size = end - (wav_data + data_offset);
		if (copy_size > 0) {
			memcpy(audio->data, wav_data + data_offset, copy_size);
			audio->size = copy_size;
		} else {
			free(audio->data);
			free(audio);
			return qfalse;
		}
	}
	
	*out_audio = audio;
	return qtrue;
}

// Test TTS synthesis and parsing
int test_full_pipeline(const char *piper_path, const char *model_path, const char *config_path, const char *text) {
	int stdin_pipe[2];
	int stdout_pipe[2];
	pid_t pid;
	uint8_t *wav_buffer = NULL;
	int wav_size = 0;
	int wav_capacity = 65536;
	
	printf("Testing full TTS pipeline...\n");
	printf("  Text: %s\n", text);
	
	// Check files
	if (access(piper_path, F_OK) != 0) {
		printf("ERROR: Piper binary not found\n");
		return 0;
	}
	if (access(model_path, F_OK) != 0) {
		printf("ERROR: Model file not found\n");
		return 0;
	}
	
	// Create pipes
	if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0) {
		printf("ERROR: Failed to create pipes\n");
		return 0;
	}
	
	// Fork and execute Piper
	pid = fork();
	if (pid < 0) {
		printf("ERROR: Failed to fork\n");
		return 0;
	}
	
	if (pid == 0) {
		close(stdin_pipe[1]);
		close(stdout_pipe[0]);
		dup2(stdin_pipe[0], STDIN_FILENO);
		dup2(stdout_pipe[1], STDOUT_FILENO);
		close(stdin_pipe[0]);
		close(stdout_pipe[1]);
		
		// Set LD_LIBRARY_PATH
		char lib_dir[MAX_QPATH];
		char *last_slash = strrchr(piper_path, '/');
		if (last_slash) {
			int dir_len = last_slash - piper_path;
			strncpy(lib_dir, piper_path, dir_len);
			lib_dir[dir_len] = '\0';
			setenv("LD_LIBRARY_PATH", lib_dir, 1);
		}
		
		execl(piper_path, "piper", "-m", model_path, "-c", config_path, "-f", "-", NULL);
		_exit(1);
	}
	
	// Parent: write text and read WAV
	close(stdin_pipe[0]);
	close(stdout_pipe[1]);
	
	write(stdin_pipe[1], text, strlen(text));
	close(stdin_pipe[1]);
	
	wav_buffer = malloc(wav_capacity);
	time_t start_time = time(NULL);
	
	while (time(NULL) - start_time < 5) {
		fd_set readfds;
		struct timeval tv;
		FD_ZERO(&readfds);
		FD_SET(stdout_pipe[0], &readfds);
		tv.tv_sec = 0;
		tv.tv_usec = 100000;
		
		if (select(stdout_pipe[0] + 1, &readfds, NULL, NULL, &tv) > 0 && FD_ISSET(stdout_pipe[0], &readfds)) {
			int bytes_read = read(stdout_pipe[0], wav_buffer + wav_size, wav_capacity - wav_size);
			if (bytes_read > 0) {
				wav_size += bytes_read;
				if (wav_size >= wav_capacity - 1024) {
					wav_capacity *= 2;
					wav_buffer = realloc(wav_buffer, wav_capacity);
				}
			} else if (bytes_read == 0) {
				break;
			}
		}
	}
	
	close(stdout_pipe[0]);
	waitpid(pid, NULL, 0);
	
	if (wav_size == 0) {
		printf("ERROR: No WAV data received\n");
		free(wav_buffer);
		return 0;
	}
	
	printf("SUCCESS: Received %d bytes of WAV data\n", wav_size);
	
	// Parse WAV
	tts_audio_buffer_t *audio = NULL;
	if (!parse_wav(wav_buffer, wav_size, &audio)) {
		printf("ERROR: Failed to parse WAV data\n");
		free(wav_buffer);
		return 0;
	}
	
	printf("SUCCESS: Parsed WAV data\n");
	printf("  Sample Rate: %d Hz\n", audio->sample_rate);
	printf("  Channels: %d\n", audio->channels);
	printf("  Bits per Sample: %d\n", audio->bits_per_sample);
	printf("  PCM Data Size: %d bytes\n", audio->size);
	printf("  Duration: ~%.2f seconds\n", (float)audio->size / (audio->sample_rate * audio->channels * (audio->bits_per_sample / 8)));
	
	// Save parsed PCM to file
	FILE *f = fopen("test_output_pcm.raw", "wb");
	if (f) {
		fwrite(audio->data, 1, audio->size, f);
		fclose(f);
		printf("SUCCESS: Saved PCM data to test_output_pcm.raw\n");
	}
	
	// Cleanup
	free(audio->data);
	free(audio);
	free(wav_buffer);
	
	return 1;
}

int main(int argc, char *argv[]) {
	const char *piper_path = "main/tts/piper";
	const char *model_path = "main/tts/de_DE-thorsten_emotional-medium.onnx";
	const char *config_path = "main/tts/de_DE-thorsten_emotional-medium.onnx.json";
	
	printf("========================================\n");
	printf("TTS Full Pipeline Test\n");
	printf("========================================\n\n");
	
	// Test multiple phrases
	const char *test_phrases[] = {
		"Hallo Welt!",
		"Feind gesichtet!",
		"Brauche Verstärkung!",
		NULL
	};
	
	int passed = 0;
	int total = 0;
	
	for (int i = 0; test_phrases[i] != NULL; i++) {
		total++;
		printf("\n--- Test %d/%d ---\n", i + 1, total);
		if (test_full_pipeline(piper_path, model_path, config_path, test_phrases[i])) {
			passed++;
			printf("PASSED\n");
		} else {
			printf("FAILED\n");
		}
	}
	
	printf("\n========================================\n");
	printf("Results: %d/%d tests passed\n", passed, total);
	if (passed == total) {
		printf("ALL TESTS PASSED!\n");
		printf("\nThe TTS system is ready for use in the game.\n");
		return 0;
	} else {
		printf("SOME TESTS FAILED\n");
		return 1;
	}
}

