#include "audio_pipeline.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include <cstring>

static const char* TAG = "audio";

int audio_init(audio_pipeline_t* audio,
               int mic_bck_pin, int mic_ws_pin, int mic_data_pin,
               int spkr_bck_pin, int spkr_ws_pin, int spkr_data_pin,
               int amp_enable_pin) {
    
    memset(audio, 0, sizeof(audio_pipeline_t));
    
    audio->mic_bck_pin = mic_bck_pin;
    audio->mic_ws_pin = mic_ws_pin;
    audio->mic_data_pin = mic_data_pin;
    audio->spkr_bck_pin = spkr_bck_pin;
    audio->spkr_ws_pin = spkr_ws_pin;
    audio->spkr_data_pin = spkr_data_pin;
    audio->amp_enable_pin = amp_enable_pin;

    esp_err_t err;

    // ---- Microphone (RX) ----
    if (mic_bck_pin >= 0 && mic_data_pin >= 0) {
        i2s_chan_config_t rx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
        rx_chan_cfg.auto_clear = true;
        
        err = i2s_new_channel(&rx_chan_cfg, NULL, &audio->rx_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "i2s_new_channel RX failed: %s", esp_err_to_name(err));
            audio->rx_handle = NULL;
        } else {
            // Standard I2S RX config
            i2s_std_config_t rx_std_cfg = {
                .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
                .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(
                    I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
                .gpio_cfg = {
                    .mclk = I2S_GPIO_UNUSED,
                    .bclk = (gpio_num_t)mic_bck_pin,
                    .ws = (gpio_num_t)(mic_ws_pin >= 0 ? mic_ws_pin : mic_bck_pin + 1),
                    .dout = I2S_GPIO_UNUSED,
                    .din = (gpio_num_t)mic_data_pin,
                    .invert_flags = {
                        .mclk_inv = false,
                        .bclk_inv = false,
                        .ws_inv = false
                    }
                }
            };

            // If ws pin is explicitly provided, use it
            if (mic_ws_pin >= 0) {
                rx_std_cfg.gpio_cfg.ws = (gpio_num_t)mic_ws_pin;
            }

            err = i2s_channel_init_std_mode(audio->rx_handle, &rx_std_cfg);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "i2s_channel_init_std_mode RX failed: %s", esp_err_to_name(err));
                i2s_del_channel(audio->rx_handle);
                audio->rx_handle = NULL;
            } else {
                audio->mic_enabled = true;
                ESP_LOGI(TAG, "I2S mic initialized (BCK:%d, WS:%d, DATA:%d)",
                         mic_bck_pin, mic_ws_pin, mic_data_pin);
            }
        }
    }

    // ---- Speaker (TX) ----
    if (spkr_bck_pin >= 0 && spkr_data_pin >= 0) {
        i2s_chan_config_t tx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
        tx_chan_cfg.auto_clear = true;
        
        err = i2s_new_channel(&tx_chan_cfg, &audio->tx_handle, NULL);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "i2s_new_channel TX failed: %s", esp_err_to_name(err));
            audio->tx_handle = NULL;
        } else {
            i2s_std_config_t tx_std_cfg = {
                .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
                .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(
                    I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
                .gpio_cfg = {
                    .mclk = I2S_GPIO_UNUSED,
                    .bclk = (gpio_num_t)spkr_bck_pin,
                    .ws = (gpio_num_t)(spkr_ws_pin >= 0 ? spkr_ws_pin : spkr_bck_pin + 1),
                    .dout = (gpio_num_t)spkr_data_pin,
                    .din = I2S_GPIO_UNUSED,
                    .invert_flags = {
                        .mclk_inv = false,
                        .bclk_inv = false,
                        .ws_inv = false
                    }
                }
            };

            if (spkr_ws_pin >= 0) {
                tx_std_cfg.gpio_cfg.ws = (gpio_num_t)spkr_ws_pin;
            }

            err = i2s_channel_init_std_mode(audio->tx_handle, &tx_std_cfg);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "i2s_channel_init_std_mode TX failed: %s", esp_err_to_name(err));
                i2s_del_channel(audio->tx_handle);
                audio->tx_handle = NULL;
            } else {
                audio->spkr_enabled = true;
                ESP_LOGI(TAG, "I2S speaker initialized (BCK:%d, WS:%d, DATA:%d)",
                         spkr_bck_pin, spkr_ws_pin, spkr_data_pin);
            }
        }
    }

    // Amplifier enable pin
    if (amp_enable_pin >= 0) {
        audio->amp_enable_pin = amp_enable_pin;
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << amp_enable_pin),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_ENABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        gpio_config(&io_conf);
        gpio_set_level((gpio_num_t)amp_enable_pin, 0);  // Off by default
    }

    audio->initialized = true;
    ESP_LOGI(TAG, "Audio pipeline initialized");
    return 0;
}

int audio_read(audio_pipeline_t* audio, int16_t* buffer, size_t num_samples, uint32_t timeout_ms) {
    if (!audio->initialized || !audio->mic_enabled || !audio->rx_handle) {
        return -1;
    }

    size_t bytes_read = 0;
    size_t bytes_to_read = num_samples * sizeof(int16_t);
    
    esp_err_t err = i2s_channel_read(audio->rx_handle, buffer, bytes_to_read, 
                                      &bytes_read, pdMS_TO_TICKS(timeout_ms));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "i2s_channel_read failed: %s", esp_err_to_name(err));
        return -1;
    }

    return bytes_read / sizeof(int16_t);
}

int audio_write(audio_pipeline_t* audio, const int16_t* buffer, size_t num_samples, uint32_t timeout_ms) {
    if (!audio->initialized || !audio->spkr_enabled || !audio->tx_handle) {
        return -1;
    }

    size_t bytes_written = 0;
    size_t bytes_to_write = num_samples * sizeof(int16_t);
    
    esp_err_t err = i2s_channel_write(audio->tx_handle, buffer, bytes_to_write,
                                       &bytes_written, pdMS_TO_TICKS(timeout_ms));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "i2s_channel_write failed: %s", esp_err_to_name(err));
        return -1;
    }

    return bytes_written / sizeof(int16_t);
}

void audio_mic_enable(audio_pipeline_t* audio, bool enable) {
    if (!audio->initialized || !audio->rx_handle) return;
    
    if (enable && !audio->mic_enabled) {
        i2s_channel_enable(audio->rx_handle);
        audio->mic_enabled = true;
    } else if (!enable && audio->mic_enabled) {
        i2s_channel_disable(audio->rx_handle);
        audio->mic_enabled = false;
    }
}

void audio_spkr_enable(audio_pipeline_t* audio, bool enable) {
    if (!audio->initialized) return;
    
    if (enable && !audio->spkr_enabled) {
        if (audio->tx_handle) {
            i2s_channel_enable(audio->tx_handle);
        }
        if (audio->amp_enable_pin >= 0) {
            gpio_set_level((gpio_num_t)audio->amp_enable_pin, 1);
        }
        audio->spkr_enabled = true;
    } else if (!enable && audio->spkr_enabled) {
        if (audio->amp_enable_pin >= 0) {
            gpio_set_level((gpio_num_t)audio->amp_enable_pin, 0);
        }
        if (audio->tx_handle) {
            i2s_channel_disable(audio->tx_handle);
        }
        audio->spkr_enabled = false;
    }
}

bool audio_is_mic_enabled(const audio_pipeline_t* audio) {
    return audio->initialized && audio->mic_enabled;
}

bool audio_is_spkr_enabled(const audio_pipeline_t* audio) {
    return audio->initialized && audio->spkr_enabled;
}

void audio_deinit(audio_pipeline_t* audio) {
    if (audio->initialized) {
        if (audio->rx_handle) {
            i2s_channel_disable(audio->rx_handle);
            i2s_del_channel(audio->rx_handle);
        }
        if (audio->tx_handle) {
            i2s_channel_disable(audio->tx_handle);
            i2s_del_channel(audio->tx_handle);
        }
        audio->mic_enabled = false;
        audio->spkr_enabled = false;
        audio->initialized = false;
        ESP_LOGI(TAG, "Audio pipeline deinitialized");
    }
}
