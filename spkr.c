#include <aaudio/AAudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <fcntl.h>

#define REQUESTED_RATE 192000  
#define NUM_RESONATORS 4
#define FEEDBACK_BASE 0.95f      // Base feedback coefficient
#define MIX_DRY 0.2f             
#define MIX_WET 0.8f             

typedef struct {
    float* buffer;
    int32_t max_size;
    int32_t current_size;
    int32_t index;
    float target_pitch;
} Resonator;

Resonator resonators[NUM_RESONATORS];
const float target_pitches[NUM_RESONATORS] = { 55.0f, 65.41f, 82.41f, 110.0f }; // A1, C2, E2, A2

int main() {
    AAudioStreamBuilder* builder;
    AAudioStream* input_stream = NULL;
    AAudioStream* output_stream = NULL;

    // Open entropy source. We use /dev/urandom so the audio thread 
    // never blocks waiting for environmental entropy collections.
    int random_fd = open("/dev/urandom", O_RDONLY | O_NONBLOCK);
    if (random_fd < 0) {
        fprintf(stderr, "Failed to open entropy source.\n");
        return 1;
    }

    // Allocate delay buffers with extra padding for pitch modulation
    for (int i = 0; i < NUM_RESONATORS; i++) {
        resonators[i].target_pitch = target_pitches[i];
        // Base size calculated from sample rate
        int32_t base_size = (int32_t)((float)REQUESTED_RATE / target_pitches[i]);
        // Add 10% padding to allow the size to fluctuate safely without out-of-bounds reads
        resonators[i].max_size = base_size + (base_size / 10);
        resonators[i].current_size = base_size;
        resonators[i].buffer = (float*)calloc(resonators[i].max_size, sizeof(float));
        resonators[i].index = 0;
        
        if (!resonators[i].buffer) {
            return 1;
        }
    }

    // Configure AAudio
    AAudio_createStreamBuilder(&builder);
    AAudioStreamBuilder_setSampleRate(builder, REQUESTED_RATE);
    AAudioStreamBuilder_setChannelCount(builder, 1); 
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT); 
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);

    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_INPUT);
    AAudioStreamBuilder_setInputPreset(builder, AAUDIO_INPUT_PRESET_VOICE_RECOGNITION); 
    AAudioStreamBuilder_openStream(builder, &input_stream);

    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_openStream(builder, &output_stream);

    AAudioStream_requestStart(input_stream);
    AAudioStream_requestStart(output_stream);

    const int32_t frames_per_burst = 128; 
    float* input_buf = (float*)malloc(frames_per_burst * sizeof(float));
    float* output_buf = (float*)malloc(frames_per_burst * sizeof(float));

    printf("Processing active. Entropy stream modulating resonances...\n");

    while (1) {
        int32_t frames_read = AAudioStream_read(input_stream, input_buf, frames_per_burst, 10000000); 
        if (frames_read < 0) break;

        if (frames_read > 0) {
            // Pull a block of raw entropy bytes for this burst processing cycle
            unsigned char entropy_bytes[4];
            if (read(random_fd, entropy_bytes, sizeof(entropy_bytes)) != sizeof(entropy_bytes)) {
                // Fallback to pseudo-random if entropy pool is momentarily dry
                entropy_bytes[0] = rand() % 255;
                entropy_bytes[1] = rand() % 255;
                entropy_bytes[2] = rand() % 255;
                entropy_bytes[3] = rand() % 255;
            }

            for (int i = 0; i < frames_read; i++) {
                float input_sample = input_buf[i];
                float resonance_accumulator = 0.0f;

                // Use the entropy bytes to derive subtle, chaotic perturbations
                // 1. Noise floor injection component (-0.002 to +0.002)
                float noise_injection = (((float)entropy_bytes[0] / 255.0f) - 0.5f) * 0.004f;

                for (int r = 0; r < NUM_RESONATORS; r++) {
                    Resonator* res = &resonators[r];

                    // 2. Continuous drift of delay-line lengths using entropy bytes 1-3
                    int32_t base_size = (int32_t)((float)REQUESTED_RATE / res->target_pitch);
                    int32_t max_drift = base_size / 40; // Max 2.5% deviation
                    int32_t current_drift = (int32_t)((((float)entropy_bytes[r % 4] / 255.0f) - 0.5f) * max_drift);
                    res->current_size = base_size + current_drift;

                    float delayed_sample = res->buffer[res->index];

                    // Inject mic audio, feedback loop, and raw system entropy noise
                    float mixed = input_sample + (delayed_sample * FEEDBACK_BASE) + noise_injection;

                    // Hard limiter to keep the drone stable
                    if (mixed > 1.0f)  mixed = 1.0f;
                    if (mixed < -1.0f) mixed = -1.0f;

                    res->buffer[res->index] = mixed;
                    resonance_accumulator += delayed_sample;

                    // Step through ring buffer using the modulated size limits
                    res->index++;
                    if (res->index >= res->current_size) {
                        res->index = 0;
                    }
                }

                resonance_accumulator /= NUM_RESONATORS;
                output_buf[i] = (input_sample * MIX_DRY) + (resonance_accumulator * MIX_WET);
            }

            int32_t frames_written = AAudioStream_write(output_stream, output_buf, frames_read, 10000000);
            if (frames_written < 0) break;
        }
    }

    // Clean up
    AAudioStream_requestStop(input_stream);
    AAudioStream_requestStop(output_stream);
    AAudioStream_close(input_stream);
    AAudioStream_close(output_stream);
    close(random_fd);
    AAudioStreamBuilder_delete(builder);
    free(input_buf);
    free(output_buf);
    for (int i = 0; i < NUM_RESONATORS; i++) free(resonators[i].buffer);

    return 0;
}
