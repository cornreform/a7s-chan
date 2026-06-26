#pragma once

#include <cstdint>
#include <cstddef>
#include "driver/i2s_std.h"

// AudioPipeline - I2S microphone input + I2S speaker output
// CoreS3:
//   Microphone: PDM mic via I2S (typically on I2S Port 0)
//   Speaker: I2S DAC/amplifier (typically on I2S Port 1, NS4168 I2S amp)

// Audio configuration
#define AUDIO_SAMPLE_RATE    16000
#define AUDIO_BITS_PER_SAMPLE 16
#define AUDIO_CHANNELS       1
#define AUDIO_BUFFER_MS      50              // 50ms buffer
#define AUDIO_BUFFER_SAMPLES (AUDIO_SAMPLE_RATE * AUDIO_BUFFER_MS / 1000)
#define AUDIO_BUFFER_BYTES   (AUDIO_BUFFER_SAMPLES * (AUDIO_BITS_PER_SAMPLE / 8) * AUDIO_CHANNELS)

// CoreS3 I2S pin definitions
// Microphone (PDM, I2S Port 0)
#define I2S_MIC_PORT         I2S_NUM_0
#define I2S_MIC_BCLK         4    // I2S clock
#define I2S_MIC_WS           5    // Word select / LR clock
#define I2S_MIC_DIN          15   // Data in

// Speaker (I2S DAC, I2S Port 1)
#define I2S_SPK_PORT         I2S_NUM_1
#define I2S_SPK_BCLK         46   // I2S bit clock
#define I2S_SPK_WS           3    // I2S word select
#define I2S_SPK_DOUT         16   // Data out

// Audio pipeline callback
typedef void (*audio_data_callback_t)(const int16_t* data, size_t samples, void* user_arg);

class AudioPipeline {
public:
    AudioPipeline();
    ~AudioPipeline();

    // Initialize I2S microphone and speaker
    bool begin();

    // Start streaming microphone input
    bool start_mic();

    // Stop microphone
    void stop_mic();

    // Start speaker output
    bool start_speaker();

    // Stop speaker
    void stop_speaker();

    // Play audio data through speaker (blocking)
    void play(const int16_t* data, size_t samples);

    // Play audio data asynchronously (non-blocking, queued)
    void play_async(const int16_t* data, size_t samples);

    // Check if speaker is currently playing
    bool is_playing() const;

    // Set microphone data callback (called from mic task)
    void set_mic_callback(audio_data_callback_t callback, void* user_arg);

    // Set volume (0.0 - 1.0)
    void set_volume(float volume);

    // Get current microphone buffer (for streaming)
    int read_mic_data(int16_t* buffer, size_t max_samples, int timeout_ms = 100);

    // Get volume
    float get_volume() const { return m_volume; }

    // Write data to speaker
    int write_speaker(const int16_t* data, size_t samples, int timeout_ms = 100);

private:
    i2s_chan_handle_t m_mic_handle;
    i2s_chan_handle_t m_spk_handle;

    bool m_mic_started;
    bool m_spk_started;
    bool m_playing;

    float m_volume;

    // Mic callback
    audio_data_callback_t m_mic_callback;
    void* m_mic_callback_arg;

    // Mic data buffer (ring buffer for reading)
    int16_t* m_mic_buffer;
    volatile size_t m_mic_buffer_head;
    volatile size_t m_mic_buffer_tail;
    size_t m_mic_buffer_size_samples;

    // Speaker play task handle
    void* m_play_task_handle;

    // Internal methods
    bool init_mic();
    bool init_speaker();
};
