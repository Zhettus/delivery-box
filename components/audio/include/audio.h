/*
 * audio.h — WM8960 Audio HAT: speaker playback over I2S.
 *
 * The codec setup is the one that worked in motor-audio-check together with
 * the door motor and the INA228 on the same I2C bus:
 *
 *   I2C  (shared with the INA228, 100 kHz)  WM8960 at 0x1A, write-only
 *   I2S  ESP32-S3 = clock master, codec = slave, 16-bit Philips, stereo
 *        GPIO14 BCLK -> P12   GPIO15 WS -> P35 (LRCLK)   GPIO16 DOUT -> P40
 *
 * Order matters (found in motor-audio-check): the codec is configured
 * BEFORE the I2S clocks start (clocks on the wires disturbed the I2C setup),
 * and the class-D speaker amp is only switched on while something plays.
 *
 * Sounds are queued and played by a background task, so every call returns
 * at once. A sound is a WAV clip (16-bit PCM, mono or stereo, any rate the
 * I2S can make) or a tone / melody generated on the fly.
 *
 * The HAT has no microphone. The ICS43434 microphone (GPIO17, I2S data in)
 * is not handled here yet.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_WM8960_ADDRESS  0x1A

typedef struct {
    i2c_master_bus_handle_t bus;       /* shared I2C bus (already created)     */
    uint8_t    address;                /* AUDIO_WM8960_ADDRESS                  */
    uint32_t   i2c_speed_hz;           /* 100 kHz: what the HAT works at        */
    gpio_num_t bclk_gpio;
    gpio_num_t ws_gpio;
    gpio_num_t dout_gpio;
    uint32_t   sample_rate_hz;         /* start rate; a WAV switches to its own */
    uint8_t    volume_percent;         /* 0..100, 100 = +6 dB speaker volume    */
    float      tone_level;             /* 0..1 digital level of generated tones */
    uint32_t   amp_off_after_ms;       /* amp off this long after the last sound */
} audio_config_t;

/* One note of a melody. freq_hz = 0 is a rest (silence). */
typedef struct {
    uint16_t freq_hz;
    uint16_t duration_ms;
} audio_note_t;

/** Configure the codec, then start I2S and the playback task.
 *  ESP_ERR_NOT_FOUND if the HAT does not answer (playback calls then fail). */
esp_err_t audio_init(const audio_config_t *cfg);

/** true once audio_init() succeeded. */
bool audio_available(void);

/** Queue a WAV clip in flash (e.g. from EMBED_FILES). Checked when queued. */
esp_err_t audio_play_wav(const uint8_t *wav_start, const uint8_t *wav_end);

/** Queue a tone. */
esp_err_t audio_play_tone(uint16_t freq_hz, uint16_t duration_ms);

/** Queue a melody. `notes` must stay valid while it plays (static const). */
esp_err_t audio_play_melody(const audio_note_t *notes, size_t count);

/** Drop everything queued and stop the sound that is playing. */
void audio_stop(void);

/** Speaker + headphone volume, 0 = mute, 100 = +6 dB. */
esp_err_t audio_set_volume(uint8_t percent);
uint8_t   audio_get_volume(void);

/** true while a sound plays or is queued. */
bool audio_busy(void);

#ifdef __cplusplus
}
#endif
