#include "audio_pipeline.h"
#include <cstring>
#include <cmath>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char* TAG = "AudioPipeline";

// Ring buffer size for mic data (200ms)
#define MIC_RING_BUFFER_MS   200
#define MIC_RING_SAMPLES     (AUDIO_SAMPLE_RATE * MIC_RING_BUFFER_MS / 1000)

AudioPipeline::AudioPipeline()
    : m_mic_handle(nullptr)
    , m_spk_handle(nullptr)
    , m_mic_started(false)
    , m_spk_started(false)
    , m_playing(false)
    , m_volume(0.5f)
    , m_mic_callback(nullptr)
    , m_mic_callback_arg(nullptr)
    , m_mic_buffer(nullptr)
    , m_mic_buffer_head(0)
    , m_mic_buffer_tail(0)
    , m_mic_buffer_size_samples(MIC_RING_SAMPLES)
    , m_play_task_handle(nullptr)
{
}

AudioPipeline::~AudioPipeline() {
    stop_mic();
    stop_speaker();

    if (m_mic_buffer) {
        heap_caps_free(m_mic_buffer);
        m_mic_buffer = nullptr;
    }

    if (m_mic_handle) {
        i2s_del_channel(m_mic_handle);
    }

    if (m_spk_handle) {
        i2s_del_channel(m_spk_handle);
    }
}

bool AudioPipeline::begin() {
    // Allocate mic ring buffer
    m_mic_buffer = (int16_t*)heap_caps_malloc(
        m_mic_buffer_size_samples * sizeof(int16_t),
        MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL
    );

    if (!m_mic_buffer) {
        ESP_LOGE(TAG, "Failed to allocate mic buffer");
        return false;
    }

    memset(m_mic_buffer, 0, m_mic_buffer_size_samples * sizeof(int16_t));

    bool mic_ok = init_mic();
    bool spk_ok = init_speaker();

    ESP_LOGI(TAG, "Audio pipeline init - Mic: %s, Speaker: %s",
             mic_ok ? "OK" : "FAIL",
             spk_ok ? "OK" : "FAIL");

    return mic_ok || spk_ok;
}

bool AudioPipeline::init_mic() {
    // I2S config for microphone (PDM input)
    i2s_chan_config_t chan_config = {
        .id = I2S_MIC_PORT,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 6,
        .dma_frame_num = AUDIO_BUFFER_SAMPLES,
        .auto_clear = true,
    };

    esp_err_t err = i2s_new_channel(&chan_config, NULL, &m_mic_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create mic channel: %d", err);
        return false;
    }

    // PDM RX (microphone) standard config
    i2s_std_config_t std_config = {
        .clk_cfg = {
            .sample_rate_hz = AUDIO_SAMPLE_RATE,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_MONO,
            .slot_mask = I2S_STD_SLOT_LEFT,
            .ws_width = I2S_SLOT_BIT_WIDTH_16BIT,
            .ws_pol = false,
            .bit_shift = true,
        },
        .gpio_cfg = {
            .bclk = (gpio_num_t)I2S_MIC_BCLK,
            .ws = (gpio_num_t)I2S_MIC_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = (gpio_num_t)I2S_MIC_DIN,
            .invert_flags = {
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    // Use standard I2S mode for the microphone
    err = i2s_channel_init_std_mode(m_mic_handle, &std_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Standard I2S init failed: %d", err);
        i2s_del_channel(m_mic_handle);
        m_mic_handle = nullptr;
        return false;
    }

    return true;
}

bool AudioPipeline::init_speaker() {
    // I2S config for speaker output (I2S DAC, NS4168 amplifier)
    i2s_chan_config_t chan_config = {
        .id = I2S_SPK_PORT,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 6,
        .dma_frame_num = AUDIO_BUFFER_SAMPLES,
        .auto_clear = true,
    };

    esp_err_t err = i2s_new_channel(&chan_config, &m_spk_handle, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create speaker channel: %d", err);
        return false;
    }

    i2s_std_config_t std_config = {
        .clk_cfg = {
            .sample_rate_hz = AUDIO_SAMPLE_RATE,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_MONO,
            .slot_mask = I2S_STD_SLOT_LEFT,
            .ws_width = I2S_SLOT_BIT_WIDTH_16BIT,
            .ws_pol = false,
            .bit_shift = true,
        },
        .gpio_cfg = {
            .bclk = (gpio_num_t)I2S_SPK_BCLK,
            .ws = (gpio_num_t)I2S_SPK_WS,
            .dout = (gpio_num_t)I2S_SPK_DOUT,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    err = i2s_channel_init_std_mode(m_spk_handle, &std_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Speaker I2S init failed: %d", err);
        i2s_del_channel(m_spk_handle);
        m_spk_handle = nullptr;
        return false;
    }

    return true;
}

bool AudioPipeline::start_mic() {
    if (m_mic_started || !m_mic_handle) return false;

    esp_err_t err = i2s_channel_enable(m_mic_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable mic: %d", err);
        return false;
    }

    m_mic_started = true;
    ESP_LOGI(TAG, "Microphone started");
    return true;
}

void AudioPipeline::stop_mic() {
    if (!m_mic_started || !m_mic_handle) return;

    i2s_channel_disable(m_mic_handle);
    m_mic_started = false;
    ESP_LOGI(TAG, "Microphone stopped");
}

bool AudioPipeline::start_speaker() {
    if (m_spk_started || !m_spk_handle) return false;

    esp_err_t err = i2s_channel_enable(m_spk_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable speaker: %d", err);
        return false;
    }

    m_spk_started = true;
    ESP_LOGI(TAG, "Speaker started");
    return true;
}

void AudioPipeline::stop_speaker() {
    if (!m_spk_started || !m_spk_handle) return;

    i2s_channel_disable(m_spk_handle);
    m_spk_started = false;
    m_playing = false;
    ESP_LOGI(TAG, "Speaker stopped");
}

void AudioPipeline::play(const int16_t* data, size_t samples) {
    if (!m_spk_started || !m_spk_handle || !data || samples == 0) return;

    m_playing = true;

    // Apply volume
    size_t bytes_to_write = samples * sizeof(int16_t);
    int16_t* vol_buffer = nullptr;

    if (m_volume < 0.99f) {
        vol_buffer = (int16_t*)heap_caps_malloc(bytes_to_write, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
        if (vol_buffer) {
            for (size_t i = 0; i < samples; i++) {
                vol_buffer[i] = static_cast<int16_t>(data[i] * m_volume);
            }
        }
    }

    size_t bytes_written = 0;
    const int16_t* source = vol_buffer ? vol_buffer : data;

    i2s_channel_write(m_spk_handle, source, bytes_to_write, &bytes_written, portMAX_DELAY);

    if (vol_buffer) {
        heap_caps_free(vol_buffer);
    }

    m_playing = false;
}

void AudioPipeline::play_async(const int16_t* data, size_t samples) {
    // For now, just play synchronously
    play(data, samples);
}

bool AudioPipeline::is_playing() const {
    return m_playing;
}

void AudioPipeline::set_mic_callback(audio_data_callback_t callback, void* user_arg) {
    m_mic_callback = callback;
    m_mic_callback_arg = user_arg;
}

void AudioPipeline::set_volume(float volume) {
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    m_volume = volume;
}

int AudioPipeline::read_mic_data(int16_t* buffer, size_t max_samples, int timeout_ms) {
    if (!m_mic_started || !m_mic_handle || !buffer || max_samples == 0) return 0;

    size_t bytes_read = 0;
    size_t bytes_requested = max_samples * sizeof(int16_t);

    esp_err_t err = i2s_channel_read(m_mic_handle, buffer, bytes_requested,
                                      &bytes_read, pdMS_TO_TICKS(timeout_ms));

    if (err != ESP_OK) {
        return 0;
    }

    return bytes_read / sizeof(int16_t);
}

int AudioPipeline::write_speaker(const int16_t* data, size_t samples, int timeout_ms) {
    if (!m_spk_started || !m_spk_handle || !data || samples == 0) return 0;

    m_playing = true;

    // Apply volume
    size_t bytes_to_write = samples * sizeof(int16_t);
    size_t bytes_written = 0;

    if (m_volume < 0.99f) {
        int16_t* vol_buffer = (int16_t*)heap_caps_malloc(bytes_to_write, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
        if (vol_buffer) {
            for (size_t i = 0; i < samples; i++) {
                vol_buffer[i] = static_cast<int16_t>(data[i] * m_volume);
            }
            i2s_channel_write(m_spk_handle, vol_buffer, bytes_to_write, &bytes_written, pdMS_TO_TICKS(timeout_ms));
            heap_caps_free(vol_buffer);
        }
    } else {
        i2s_channel_write(m_spk_handle, data, bytes_to_write, &bytes_written, pdMS_TO_TICKS(timeout_ms));
    }

    m_playing = false;
    return bytes_written / sizeof(int16_t);
}
