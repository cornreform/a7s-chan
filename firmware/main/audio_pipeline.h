#ifndef A7S_AUDIO_PIPELINE_H
#define A7S_AUDIO_PIPELINE_H

#include <cstdint>
#include <driver/i2s_std.h>

#ifdef __cplusplus
extern "C" {
#endif

// Audio configuration
#define AUDIO_SAMPLE_RATE   16000
#define AUDIO_CHANNELS      1
#define AUDIO_BITS_PER_SAMPLE 16
#define AUDIO_BUFFER_SIZE   1024

typedef struct {
    // I2S handles
    i2s_chan_handle_t tx_handle;   // Speaker
    i2s_chan_handle_t rx_handle;   // Microphone
    
    // GPIO pins
    int mic_bck_pin;
    int mic_ws_pin;
    int mic_data_pin;
    int spkr_bck_pin;
    int spkr_ws_pin;
    int spkr_data_pin;
    
    // Amplifier control (if applicable)
    int amp_enable_pin;
    
    bool initialized;
    bool mic_enabled;
    bool spkr_enabled;
} audio_pipeline_t;

// Initialize audio pipeline (I2S mic + speaker)
// Returns 0 on success
int audio_init(audio_pipeline_t* audio,
               int mic_bck_pin, int mic_ws_pin, int mic_data_pin,
               int spkr_bck_pin, int spkr_ws_pin, int spkr_data_pin,
               int amp_enable_pin);

// Read audio samples from microphone (blocking)
// Returns number of bytes read, or -1 on error
int audio_read(audio_pipeline_t* audio, int16_t* buffer, size_t num_samples, uint32_t timeout_ms);

// Write audio samples to speaker (blocking)
// Returns number of bytes written, or -1 on error
int audio_write(audio_pipeline_t* audio, const int16_t* buffer, size_t num_samples, uint32_t timeout_ms);

// Enable/disable microphone
void audio_mic_enable(audio_pipeline_t* audio, bool enable);

// Enable/disable speaker (controls amp if configured)
void audio_spkr_enable(audio_pipeline_t* audio, bool enable);

// Get current state
bool audio_is_mic_enabled(const audio_pipeline_t* audio);
bool audio_is_spkr_enabled(const audio_pipeline_t* audio);

// Deinitialize and free all resources
void audio_deinit(audio_pipeline_t* audio);

#ifdef __cplusplus
}
#endif

#endif // A7S_AUDIO_PIPELINE_H
