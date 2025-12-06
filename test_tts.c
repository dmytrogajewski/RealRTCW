/*
===========================================================================
TTS Test Program
Standalone test for TTS functionality
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

// Simple WAV header structure
typedef struct {
	char riff[4];
	uint32_t file_size;
	char wave[4];
	char fmt[4];
	uint32_t fmt_size;
	uint16_t audio_format;
	uint16_t channels;
	uint32_t sample_rate;
	uint32_t byte_rate;
	uint16_t block_align;
	uint16_t bits_per_sample;
	char data[4];
	uint32_t data_size;
} wav_header_t;

// Test TTS synthesis
int test_piper_synthesis(const char *piper_path, const char *model_path, const char *config_path, const char *text) {
	int stdin_pipe[2];
	int stdout_pipe[2];
	pid_t pid;
	uint8_t *wav_buffer = NULL;
	int wav_size = 0;
	int wav_capacity = 65536;
	
	printf("Testing Piper synthesis...\n");
	printf("  Piper: %s\n", piper_path);
	printf("  Model: %s\n", model_path);
	printf("  Config: %s\n", config_path);
	printf("  Text: %s\n", text);
	
	// Check if files exist
	if (access(piper_path, F_OK) != 0) {
		printf("ERROR: Piper binary not found: %s\n", piper_path);
		return 0;
	}
	if (access(model_path, F_OK) != 0) {
		printf("ERROR: Model file not found: %s\n", model_path);
		return 0;
	}
	if (access(config_path, F_OK) != 0) {
		printf("WARNING: Config file not found: %s (may still work)\n", config_path);
	}
	
	// Create pipes
	if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0) {
		printf("ERROR: Failed to create pipes: %s\n", strerror(errno));
		return 0;
	}
	
	// Fork process
	pid = fork();
	if (pid < 0) {
		printf("ERROR: Failed to fork process: %s\n", strerror(errno));
		close(stdin_pipe[0]);
		close(stdin_pipe[1]);
		close(stdout_pipe[0]);
		close(stdout_pipe[1]);
		return 0;
	}
	
	if (pid == 0) {
		// Child process
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
		
		// Execute Piper
		execl(piper_path, "piper", "-m", model_path, "-c", config_path, "-f", "-", NULL);
		
		_exit(1);
	}
	
	// Parent process
	close(stdin_pipe[0]);
	close(stdout_pipe[1]);
	
	// Write text to stdin
	int text_len = strlen(text);
	int written = write(stdin_pipe[1], text, text_len);
	close(stdin_pipe[1]);
	
	if (written != text_len) {
		printf("ERROR: Failed to write text to Piper\n");
		close(stdout_pipe[0]);
		waitpid(pid, NULL, 0);
		return 0;
	}
	
	// Read WAV data from stdout
	wav_buffer = malloc(wav_capacity);
	
	int start_time = time(NULL);
	int timeout = 5; // 5 second timeout
	
	while (1) {
		// Check timeout
		if (time(NULL) - start_time > timeout) {
			printf("ERROR: Synthesis timeout after %d seconds\n", timeout);
			kill(pid, SIGTERM);
			waitpid(pid, NULL, 0);
			close(stdout_pipe[0]);
			free(wav_buffer);
			return 0;
		}
		
		fd_set readfds;
		struct timeval tv;
		FD_ZERO(&readfds);
		FD_SET(stdout_pipe[0], &readfds);
		tv.tv_sec = 0;
		tv.tv_usec = 100000; // 100ms
		
		int ret = select(stdout_pipe[0] + 1, &readfds, NULL, NULL, &tv);
		if (ret > 0 && FD_ISSET(stdout_pipe[0], &readfds)) {
			int bytes_read = read(stdout_pipe[0], wav_buffer + wav_size, wav_capacity - wav_size);
			if (bytes_read > 0) {
				wav_size += bytes_read;
				if (wav_size >= wav_capacity - 1024) {
					wav_capacity *= 2;
					wav_buffer = realloc(wav_buffer, wav_capacity);
				}
			} else if (bytes_read == 0) {
				break; // EOF
			} else if (errno != EINTR) {
				break;
			}
		} else if (ret == 0) {
			continue;
		} else {
			break;
		}
	}
	
	close(stdout_pipe[0]);
	
	// Wait for child
	int status;
	waitpid(pid, &status, 0);
	
	if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
		printf("ERROR: Piper process exited with status %d\n", WEXITSTATUS(status));
		free(wav_buffer);
		return 0;
	}
	
	if (wav_size == 0) {
		printf("ERROR: No data received from Piper\n");
		free(wav_buffer);
		return 0;
	}
	
	printf("SUCCESS: Received %d bytes of WAV data\n", wav_size);
	
	// Parse WAV header
	if ((size_t)wav_size < sizeof(wav_header_t)) {
		printf("ERROR: WAV data too small\n");
		free(wav_buffer);
		return 0;
	}
	
	wav_header_t *header = (wav_header_t *)wav_buffer;
	
	// Verify WAV format
	if (memcmp(header->riff, "RIFF", 4) != 0 || memcmp(header->wave, "WAVE", 4) != 0) {
		printf("ERROR: Invalid WAV format\n");
		free(wav_buffer);
		return 0;
	}
	
	printf("WAV Header:\n");
	printf("  Sample Rate: %u Hz\n", header->sample_rate);
	printf("  Channels: %u\n", header->channels);
	printf("  Bits per Sample: %u\n", header->bits_per_sample);
	printf("  Data Size: %u bytes\n", header->data_size);
	printf("  Duration: ~%.2f seconds\n", (float)header->data_size / (header->sample_rate * header->channels * (header->bits_per_sample / 8)));
	
	// Save to file for verification
	FILE *f = fopen("test_output.wav", "wb");
	if (f) {
		fwrite(wav_buffer, 1, wav_size, f);
		fclose(f);
		printf("SUCCESS: Saved WAV to test_output.wav\n");
	} else {
		printf("WARNING: Could not save WAV file\n");
	}
	
	free(wav_buffer);
	return 1;
}

int main(int argc, char *argv[]) {
	const char *piper_path = "main/tts/piper";
	const char *model_path = "main/tts/de_DE-thorsten_emotional-medium.onnx";
	const char *config_path = "main/tts/de_DE-thorsten_emotional-medium.onnx.json";
	const char *test_text = "Hallo Welt! Dies ist ein Test der Text-zu-Sprache Funktion.";
	
	printf("========================================\n");
	printf("TTS Functionality Test\n");
	printf("========================================\n\n");
	
	// Allow override via command line
	if (argc > 1) piper_path = argv[1];
	if (argc > 2) model_path = argv[2];
	if (argc > 3) config_path = argv[3];
	if (argc > 4) test_text = argv[4];
	
	int result = test_piper_synthesis(piper_path, model_path, config_path, test_text);
	
	printf("\n========================================\n");
	if (result) {
		printf("TEST PASSED: TTS synthesis working!\n");
		printf("You can play test_output.wav to verify audio quality.\n");
		return 0;
	} else {
		printf("TEST FAILED: TTS synthesis not working.\n");
		printf("Check the errors above and verify:\n");
		printf("  1. Piper binary exists and is executable\n");
		printf("  2. Model file exists\n");
		printf("  3. Config file exists (optional)\n");
		printf("  4. Shared libraries are in the same directory as piper\n");
		return 1;
	}
}

