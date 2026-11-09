"""Run with python3 test_host.py; requires a host C compiler, not ESP32 hardware."""

from pathlib import Path
import os
import shlex
import subprocess
import tempfile
import unittest


COMPONENT = Path(__file__).resolve().parents[1]

# Only the SDK interfaces are mocked. The production LED/color code is compiled.
SDK = r"""
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#define ESP_IDF_VERSION_VAL(a,b,c) (((a) << 16) | ((b) << 8) | (c))
#define ESP_IDF_VERSION ESP_IDF_VERSION_VAL(6,1,0)
#define HELPER_TARGET_IS_ESP8266 0
#define CONFIG_LED_STRIP_FLUSH_TIMEOUT 1000
#define CONFIG_LED_STRIP_PAUSE_LENGTH 50
#define SOC_RMT_MEM_WORDS_PER_CHANNEL 64
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#define configTICK_RATE_HZ 100
#define portMAX_DELAY UINT32_MAX
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NO_MEM 3
#define ESP_ERR_TIMEOUT 4
#define RMT_CLK_SRC_DEFAULT 0
typedef int esp_err_t;
typedef int gpio_num_t;
typedef uint32_t TickType_t;
typedef void *rmt_channel_handle_t;
typedef void *rmt_encoder_handle_t;
typedef struct {
    unsigned level0, duration0, level1, duration1;
} rmt_symbol_word_t;
typedef struct {
    rmt_symbol_word_t bit0, bit1;
    struct { unsigned msb_first; } flags;
} rmt_bytes_encoder_config_t;
typedef struct {
    gpio_num_t gpio_num;
    int clk_src;
    uint32_t resolution_hz;
    size_t mem_block_symbols, trans_queue_depth;
} rmt_tx_channel_config_t;
typedef struct {
    int loop_count;
    struct { unsigned eot_level; } flags;
} rmt_transmit_config_t;
esp_err_t rmt_new_tx_channel(const rmt_tx_channel_config_t *, rmt_channel_handle_t *);
esp_err_t rmt_new_bytes_encoder(const rmt_bytes_encoder_config_t *, rmt_encoder_handle_t *);
esp_err_t rmt_enable(rmt_channel_handle_t);
esp_err_t rmt_disable(rmt_channel_handle_t);
esp_err_t rmt_del_channel(rmt_channel_handle_t);
esp_err_t rmt_del_encoder(rmt_encoder_handle_t);
esp_err_t rmt_tx_wait_all_done(rmt_channel_handle_t, int);
esp_err_t rmt_transmit(rmt_channel_handle_t, rmt_encoder_handle_t, const void *, size_t,
                       const rmt_transmit_config_t *);
void esp_rom_delay_us(uint32_t);
void *heap_caps_calloc(size_t, size_t, unsigned);
int64_t esp_timer_get_time(void);
"""

HARNESS = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "led_strip.c"

static int fail_step, step, channels, encoders, wait_result, last_timeout, delay;
static const void *payload;
static size_t payload_size;
static rmt_bytes_encoder_config_t timing;

void *heap_caps_calloc(size_t n, size_t size, unsigned caps)
{
    assert(caps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (++step == fail_step) return NULL;
    return calloc(n, size);
}
esp_err_t rmt_new_tx_channel(const rmt_tx_channel_config_t *c, rmt_channel_handle_t *out)
{
    assert(c->resolution_hz == 40000000 && c->trans_queue_depth == 1);
    assert(c->mem_block_symbols == 64);
    if (++step == fail_step) return ESP_ERR_NO_MEM;
    *out = (void *)1; channels++; return ESP_OK;
}
esp_err_t rmt_new_bytes_encoder(const rmt_bytes_encoder_config_t *c, rmt_encoder_handle_t *out)
{
    if (++step == fail_step) return ESP_ERR_NO_MEM;
    timing = *c; *out = (void *)2; encoders++; return ESP_OK;
}
esp_err_t rmt_enable(rmt_channel_handle_t c)
{
    assert(c); return ++step == fail_step ? ESP_ERR_INVALID_STATE : ESP_OK;
}
esp_err_t rmt_disable(rmt_channel_handle_t c) { assert(c); return ESP_OK; }
esp_err_t rmt_del_channel(rmt_channel_handle_t c) { assert(c); channels--; return ESP_OK; }
esp_err_t rmt_del_encoder(rmt_encoder_handle_t e) { assert(e); encoders--; return ESP_OK; }
esp_err_t rmt_tx_wait_all_done(rmt_channel_handle_t c, int ms)
{
    assert(c); last_timeout = ms; return wait_result;
}
esp_err_t rmt_transmit(rmt_channel_handle_t c, rmt_encoder_handle_t e, const void *p,
                       size_t size, const rmt_transmit_config_t *config)
{
    assert(c && e && config->loop_count == 0 && config->flags.eot_level == 0);
    payload = p; payload_size = size; return ESP_OK;
}
void esp_rom_delay_us(uint32_t us) { delay = us; }

int main(void)
{
    led_strip_install();
    assert(led_strip_init(NULL) == ESP_ERR_INVALID_ARG);
    led_strip_t invalid = { .length = SIZE_MAX };
    assert(led_strip_init(&invalid) == ESP_ERR_INVALID_ARG);
    // Allocation, channel creation, encoder creation, and enable failures.
    for (int failure = 1; failure <= 4; failure++)
    {
        led_strip_t s = { .length = 1, .brightness = 255 };
        fail_step = failure; step = 0;
        assert(led_strip_init(&s) != ESP_OK);
        assert(!s.buf && !s.tx_buf && !s.tx_channel && !s.encoder && !s.rmt_enabled);
        assert(channels == 0 && encoders == 0);
    }
    fail_step = 0;
    const unsigned ticks[][4] = { {16,40,40,16}, {12,36,24,24},
                                  {14,54,54,14}, {12,36,54,14} };
    for (int type = 0; type < LED_STRIP_TYPE_MAX; type++)
    {
        led_strip_t s = { .type = type, .length = 2, .brightness = 255, .is_rgbw = true };
        assert(led_strip_init(&s) == ESP_OK);
        assert(led_strip_init(&s) == ESP_ERR_INVALID_STATE);
        assert(timing.flags.msb_first && timing.bit0.level0 && !timing.bit0.level1);
        assert(timing.bit1.level0 && !timing.bit1.level1);
        assert(timing.bit0.duration0 == ticks[type][0] && timing.bit0.duration1 == ticks[type][1]);
        assert(timing.bit1.duration0 == ticks[type][2] && timing.bit1.duration1 == ticks[type][3]);
        rgb_t color = { .r = 32, .g = 64, .b = 128 };
        assert(led_strip_fill(&s, 0, 2, color) == ESP_OK);
        assert(s.buf[0] == (type >= LED_STRIP_APA106 ? 32 : 64));
        assert(s.buf[3] == rgb_luma(color));
        assert(led_strip_set_pixel(&s, 2, color) == ESP_ERR_INVALID_ARG);
        assert(led_strip_fill(&s, SIZE_MAX, 2, color) == ESP_ERR_INVALID_ARG);
        assert(led_strip_set_pixels(&s, 0, 1, NULL) == ESP_ERR_INVALID_ARG);
        assert(led_strip_flush(&s) == ESP_OK && delay == 50);
        assert(payload == s.tx_buf && payload != s.buf && payload_size == 8);
        assert(memcmp(payload, s.buf, 8) == 0);
        uint8_t saved = s.tx_buf[0];
        assert(led_strip_set_pixel(&s, 0, (rgb_t){0}) == ESP_OK);
        assert(s.tx_buf[0] == saved);
        wait_result = ESP_ERR_TIMEOUT;
        assert(led_strip_busy(&s) && last_timeout == 0);
        assert(led_strip_flush(&s) == ESP_ERR_TIMEOUT && s.tx_buf[0] == saved);
        assert(led_strip_free(&s) == ESP_ERR_TIMEOUT && s.buf && channels == 1);
        wait_result = ESP_OK;
        assert(led_strip_wait(&s, 2) == ESP_OK && last_timeout == 20);
        assert(led_strip_wait(&s, portMAX_DELAY) == ESP_OK && last_timeout == -1);
        assert(led_strip_wait(&s, portMAX_DELAY - 1) == ESP_OK && last_timeout == INT_MAX);
        assert(!led_strip_busy(&s));
        s.brightness = 128;
        assert(led_strip_flush(&s) == ESP_OK);
        for (size_t i = 0; i < payload_size; i++)
            assert(s.tx_buf[i] == scale8_video(s.buf[i], 128));
        s.brightness = 0;
        assert(led_strip_flush(&s) == ESP_OK);
        for (size_t i = 0; i < payload_size; i++) assert(s.tx_buf[i] == 0);
        assert(led_strip_free(&s) == ESP_OK);
        assert(!s.buf && !s.tx_buf && !s.tx_channel && !s.encoder);
        assert(channels == 0 && encoders == 0);
        assert(led_strip_init(&s) == ESP_OK);
        assert(led_strip_free(&s) == ESP_OK);
    }
    puts("LED strip host checks passed: timing, RGBW, brightness, snapshots, timeouts, cleanup, bounds");
    return 0;
}
"""


class ModernRmtTest(unittest.TestCase):
    def test_driver_contract(self):
        with tempfile.TemporaryDirectory(prefix="led-strip-test-") as tmp:
            root = Path(tmp)
            (root / "sdk.h").write_text(SDK)
            for header in (
                "driver/gpio.h", "driver/rmt_tx.h", "esp_err.h",
                "esp_idf_version.h", "freertos/FreeRTOS.h", "esp_log.h",
                "esp_attr.h", "esp_rom_sys.h", "esp_heap_caps.h",
                "soc/soc_caps.h", "esp_idf_lib_helpers.h", "esp_timer.h",
            ):
                stub = root / header
                stub.parent.mkdir(parents=True, exist_ok=True)
                stub.write_text('#include "sdk.h"\n')
            source = root / "test.c"
            source.write_text(HARNESS)
            output = root / "test"
            compiler = shlex.split(os.environ.get("CC", "cc"))
            subprocess.run(
                compiler + [
                    "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                    "-I", str(root), "-I", str(COMPONENT),
                    "-I", str(COMPONENT.parent / "color"),
                    "-I", str(COMPONENT.parent / "lib8tion"),
                    str(source), "-o", str(output),
                ],
                check=True,
            )
            subprocess.run([str(output)], check=True)


if __name__ == "__main__":
    unittest.main()
