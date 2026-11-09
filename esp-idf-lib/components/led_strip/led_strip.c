/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2020 Ruslan V. Uss <unclerus@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @file led_strip.c
 *
 * RMT-based ESP-IDF driver for WS2812B/SK6812/APA106/SM16703 LED strips
 *
 * Copyright (c) 2020 Ruslan V. Uss <unclerus@gmail.com>
 *
 * MIT Licensed as described in the file LICENSE
 */
#include "led_strip.h"
#include <esp_log.h>
#include <esp_attr.h>
#include <stdlib.h>
#include <limits.h>
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#include <esp_rom_sys.h>
#include <esp_heap_caps.h>
#include <soc/soc_caps.h>
#else
#include <ets_sys.h>
#endif
#include <esp_idf_lib_helpers.h>

#if HELPER_TARGET_IS_ESP8266
#error led_strip is not supported on ESP8266
#endif

#ifndef RMT_DEFAULT_CONFIG_TX
#define RMT_DEFAULT_CONFIG_TX(gpio, channel_id)      \
    {                                                \
        .rmt_mode = RMT_MODE_TX,                     \
        .channel = channel_id,                       \
        .gpio_num = gpio,                            \
        .clk_div = 80,                               \
        .mem_block_num = 1,                          \
        .tx_config = {                               \
            .carrier_freq_hz = 38000,                \
            .carrier_level = RMT_CARRIER_LEVEL_HIGH, \
            .idle_level = RMT_IDLE_LEVEL_LOW,        \
            .carrier_duty_percent = 33,              \
            .carrier_en = false,                     \
            .loop_en = false,                        \
            .idle_output_en = true,                  \
        }                                            \
    }
#endif

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
static const char *TAG = "led_strip";
#endif

#define LED_STRIP_RMT_CLK_DIV 2

#define CHECK(x) do { esp_err_t __; if ((__ = x) != ESP_OK) return __; } while (0)
#define CHECK_ARG(VAL) do { if (!(VAL)) return ESP_ERR_INVALID_ARG; } while (0)

#define COLOR_SIZE(strip) (3 + ((strip)->is_rgbw != 0))

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
static void IRAM_ATTR _rmt_adapter(const void *src, rmt_item32_t *dest, size_t src_size,
                                   size_t wanted_num, size_t *translated_size, size_t *item_num,
                                   const rmt_item32_t *bit0, const rmt_item32_t *bit1)
{
    if (!src || !dest)
    {
        *translated_size = 0;
        *item_num = 0;
        return;
    }
    size_t size = 0;
    size_t num = 0;
    uint8_t *psrc = (uint8_t *)src;
    rmt_item32_t *pdest = dest;
#ifdef LED_STRIP_BRIGHTNESS
    led_strip_t *strip;
    esp_err_t r = rmt_translator_get_context(item_num, (void **)&strip);
    uint8_t brightness = r == ESP_OK ? strip->brightness : 255;
#endif
    while (size < src_size && num < wanted_num)
    {
#ifdef LED_STRIP_BRIGHTNESS
        uint8_t b = brightness != 255 ? scale8_video(*psrc, brightness) : *psrc;
#else
        uint8_t b = *psrc;
#endif
        for (int i = 0; i < 8; i++)
        {
            // MSB first
            pdest->val = b & (1 << (7 - i)) ? bit1->val : bit0->val;
            num++;
            pdest++;
        }
        size++;
        psrc++;
    }
    *translated_size = size;
    *item_num = num;
}

typedef struct {
    rmt_item32_t bit0, bit1;
} led_rmt_t;

static led_rmt_t rmt_items[LED_STRIP_TYPE_MAX] = { 0 };

static void IRAM_ATTR ws2812_rmt_adapter(const void *src, rmt_item32_t *dest, size_t src_size,
        size_t wanted_num, size_t *translated_size, size_t *item_num)
{
    _rmt_adapter(src, dest, src_size, wanted_num, translated_size, item_num, &rmt_items[LED_STRIP_WS2812].bit0, &rmt_items[LED_STRIP_WS2812].bit1);
}

static void IRAM_ATTR sk6812_rmt_adapter(const void *src, rmt_item32_t *dest, size_t src_size,
        size_t wanted_num, size_t *translated_size, size_t *item_num)
{
    _rmt_adapter(src, dest, src_size, wanted_num, translated_size, item_num, &rmt_items[LED_STRIP_SK6812].bit0, &rmt_items[LED_STRIP_SK6812].bit1);
}

static void IRAM_ATTR apa106_rmt_adapter(const void *src, rmt_item32_t *dest, size_t src_size,
        size_t wanted_num, size_t *translated_size, size_t *item_num)
{
    _rmt_adapter(src, dest, src_size, wanted_num, translated_size, item_num, &rmt_items[LED_STRIP_APA106].bit0, &rmt_items[LED_STRIP_APA106].bit1);
}

static void IRAM_ATTR sm16703_rmt_adapter(const void *src, rmt_item32_t *dest, size_t src_size,
                                         size_t wanted_num, size_t *translated_size, size_t *item_num)
{
    _rmt_adapter(src, dest, src_size, wanted_num, translated_size, item_num, &rmt_items[LED_STRIP_SM16703].bit0, &rmt_items[LED_STRIP_SM16703].bit1);
}

#define LED_ADAPTER(name) , .adapter = name
#else
#define LED_ADAPTER(name)
#endif

typedef enum {
    ORDER_GRB,
    ORDER_RGB,
} color_order_t;

typedef struct {
    uint32_t t0h, t0l, t1h, t1l;
    color_order_t order;
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
    sample_to_rmt_t adapter;
#endif
} led_params_t;

static const led_params_t led_params[] = {
    [LED_STRIP_WS2812]  = { .t0h = 400, .t0l = 1000, .t1h = 1000, .t1l = 400, .order = ORDER_GRB LED_ADAPTER(ws2812_rmt_adapter) },
    [LED_STRIP_SK6812]  = { .t0h = 300, .t0l = 900,  .t1h = 600,  .t1l = 600, .order = ORDER_GRB LED_ADAPTER(sk6812_rmt_adapter) },
    [LED_STRIP_APA106]  = { .t0h = 350, .t0l = 1360, .t1h = 1360, .t1l = 350, .order = ORDER_RGB LED_ADAPTER(apa106_rmt_adapter) },
    [LED_STRIP_SM16703] = { .t0h = 300, .t0l = 900,  .t1h = 1360, .t1l = 350, .order = ORDER_RGB LED_ADAPTER(sm16703_rmt_adapter) },
};

///////////////////////////////////////////////////////////////////////////////

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
void led_strip_install()
{
    float ratio = (float)APB_CLK_FREQ / LED_STRIP_RMT_CLK_DIV / 1e09f;

    for (size_t i = 0; i < LED_STRIP_TYPE_MAX; i++)
    {
        // 0 bit
        rmt_items[i].bit0.duration0 = (uint32_t)(ratio * led_params[i].t0h);
        rmt_items[i].bit0.level0 = 1;
        rmt_items[i].bit0.duration1 = (uint32_t)(ratio * led_params[i].t0l);
        rmt_items[i].bit0.level1 = 0;
        // 1 bit
        rmt_items[i].bit1.duration0 = (uint32_t)(ratio * led_params[i].t1h);
        rmt_items[i].bit1.level0 = 1;
        rmt_items[i].bit1.duration1 = (uint32_t)(ratio * led_params[i].t1l);
        rmt_items[i].bit1.level1 = 0;
    }
}

esp_err_t led_strip_init(led_strip_t *strip)
{
    CHECK_ARG(strip && strip->length > 0 && strip->type < LED_STRIP_TYPE_MAX);

    strip->buf = calloc(strip->length, COLOR_SIZE(strip));
    if (!strip->buf)
    {
        ESP_LOGE(TAG, "Not enough memory");
        return ESP_ERR_NO_MEM;
    }

    rmt_config_t config = RMT_DEFAULT_CONFIG_TX(strip->gpio, strip->channel);
    config.clk_div = LED_STRIP_RMT_CLK_DIV;

    CHECK(rmt_config(&config));
    CHECK(rmt_driver_install(config.channel, 0, 0));

    CHECK(rmt_translator_init(config.channel, led_params[strip->type].adapter));
#ifdef LED_STRIP_BRIGHTNESS
    // No support for translator context prior to ESP-IDF 4.3
    CHECK(rmt_translator_set_context(config.channel, strip));
#endif

    return ESP_OK;
}

esp_err_t led_strip_free(led_strip_t *strip)
{
    CHECK_ARG(strip && strip->buf);
    free(strip->buf);

    CHECK(rmt_driver_uninstall(strip->channel));

    return ESP_OK;
}

esp_err_t led_strip_flush(led_strip_t *strip)
{
    CHECK_ARG(strip && strip->buf);

    CHECK(rmt_wait_tx_done(strip->channel, pdMS_TO_TICKS(CONFIG_LED_STRIP_FLUSH_TIMEOUT)));
    ets_delay_us(CONFIG_LED_STRIP_PAUSE_LENGTH);
    return rmt_write_sample(strip->channel, strip->buf,
                            strip->length * COLOR_SIZE(strip), false);
}

bool led_strip_busy(led_strip_t *strip)
{
    if (!strip) return false;
    return rmt_wait_tx_done(strip->channel, 0) == ESP_ERR_TIMEOUT;
}

esp_err_t led_strip_wait(led_strip_t *strip, TickType_t timeout)
{
    CHECK_ARG(strip);

    return rmt_wait_tx_done(strip->channel, timeout);
}

#else

// 40 MHz preserves the legacy driver's 25 ns timing granularity.
#define LED_STRIP_RMT_RESOLUTION_HZ 40000000
#define NS_TO_TICKS(ns) ((uint32_t)((uint64_t)(ns) * LED_STRIP_RMT_RESOLUTION_HZ / 1000000000))

void led_strip_install(void)
{
    // Modern encoders are configured per strip during initialization.
}

esp_err_t led_strip_init(led_strip_t *strip)
{
    CHECK_ARG(strip && strip->length > 0 &&
              strip->type >= LED_STRIP_WS2812 && strip->type < LED_STRIP_TYPE_MAX &&
              strip->length <= SIZE_MAX / COLOR_SIZE(strip));
    if (strip->buf || strip->tx_buf || strip->tx_channel || strip->encoder)
        return ESP_ERR_INVALID_STATE;

    size_t size = strip->length * COLOR_SIZE(strip);
    strip->buf = calloc(1, size);
    // The encoder reads this snapshot from an ISR, potentially with flash cache disabled.
    strip->tx_buf = heap_caps_calloc(1, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    esp_err_t err = ESP_ERR_NO_MEM;
    if (!strip->buf || !strip->tx_buf)
        goto fail;

    rmt_tx_channel_config_t channel_config = {
        .gpio_num = strip->gpio,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = LED_STRIP_RMT_RESOLUTION_HZ,
        .mem_block_symbols = SOC_RMT_MEM_WORDS_PER_CHANNEL,
        .trans_queue_depth = 1,
    };
    err = rmt_new_tx_channel(&channel_config, &strip->tx_channel);
    if (err != ESP_OK)
        goto fail;

    const led_params_t *params = &led_params[strip->type];
    rmt_bytes_encoder_config_t encoder_config = {
        .bit0 = {
            .level0 = 1, .duration0 = NS_TO_TICKS(params->t0h),
            .level1 = 0, .duration1 = NS_TO_TICKS(params->t0l),
        },
        .bit1 = {
            .level0 = 1, .duration0 = NS_TO_TICKS(params->t1h),
            .level1 = 0, .duration1 = NS_TO_TICKS(params->t1l),
        },
        .flags.msb_first = 1,
    };
    err = rmt_new_bytes_encoder(&encoder_config, &strip->encoder);
    if (err != ESP_OK)
        goto fail;
    err = rmt_enable(strip->tx_channel);
    if (err != ESP_OK)
        goto fail;
    strip->rmt_enabled = true;
    return ESP_OK;

fail:
    if (strip->encoder)
        rmt_del_encoder(strip->encoder);
    if (strip->tx_channel)
        rmt_del_channel(strip->tx_channel);
    free(strip->tx_buf);
    free(strip->buf);
    strip->encoder = NULL;
    strip->tx_channel = NULL;
    strip->tx_buf = NULL;
    strip->buf = NULL;
    strip->rmt_enabled = false;
    return err;
}

esp_err_t led_strip_free(led_strip_t *strip)
{
    CHECK_ARG(strip && strip->buf);
    // Never release a payload or encoder still referenced by the TX interrupt.
    if (strip->rmt_enabled)
    {
        CHECK(rmt_tx_wait_all_done(strip->tx_channel, CONFIG_LED_STRIP_FLUSH_TIMEOUT));
        CHECK(rmt_disable(strip->tx_channel));
        strip->rmt_enabled = false;
    }
    if (strip->tx_channel)
    {
        CHECK(rmt_del_channel(strip->tx_channel));
        strip->tx_channel = NULL;
    }
    if (strip->encoder)
    {
        CHECK(rmt_del_encoder(strip->encoder));
        strip->encoder = NULL;
    }
    free(strip->tx_buf);
    free(strip->buf);
    strip->tx_buf = NULL;
    strip->buf = NULL;
    return ESP_OK;
}

esp_err_t led_strip_flush(led_strip_t *strip)
{
    CHECK_ARG(strip && strip->buf && strip->tx_buf && strip->encoder && strip->rmt_enabled);
    CHECK(rmt_tx_wait_all_done(strip->tx_channel, CONFIG_LED_STRIP_FLUSH_TIMEOUT));
    esp_rom_delay_us(CONFIG_LED_STRIP_PAUSE_LENGTH);

    size_t size = strip->length * COLOR_SIZE(strip);
    for (size_t i = 0; i < size; i++)
        strip->tx_buf[i] = strip->brightness == 255 ? strip->buf[i] :
                          scale8_video(strip->buf[i], strip->brightness);

    const rmt_transmit_config_t config = {
        .loop_count = 0,
        .flags.eot_level = 0,
    };
    return rmt_transmit(strip->tx_channel, strip->encoder, strip->tx_buf, size, &config);
}

bool led_strip_busy(led_strip_t *strip)
{
    return strip && strip->rmt_enabled &&
           rmt_tx_wait_all_done(strip->tx_channel, 0) == ESP_ERR_TIMEOUT;
}

esp_err_t led_strip_wait(led_strip_t *strip, TickType_t timeout)
{
    CHECK_ARG(strip && strip->tx_channel && strip->rmt_enabled);
    if (timeout == portMAX_DELAY)
        return rmt_tx_wait_all_done(strip->tx_channel, -1);
    // Round up and saturate rather than turning a large finite timeout into -1.
    uint64_t ms = ((uint64_t)timeout * 1000 + configTICK_RATE_HZ - 1) / configTICK_RATE_HZ;
    return rmt_tx_wait_all_done(strip->tx_channel, ms > INT_MAX ? INT_MAX : (int)ms);
}
#endif

esp_err_t led_strip_set_pixel(led_strip_t *strip, size_t num, rgb_t color)
{
    CHECK_ARG(strip && strip->buf && num < strip->length);
    size_t idx = num * COLOR_SIZE(strip);
    switch (led_params[strip->type].order)
    {
        case ORDER_GRB:
            strip->buf[idx] = color.g;
            strip->buf[idx + 1] = color.r;
            strip->buf[idx + 2] = color.b;
            if (strip->is_rgbw)
                strip->buf[idx + 3] = rgb_luma(color);
            break;
        case ORDER_RGB:
            strip->buf[idx] = color.r;
            strip->buf[idx + 1] = color.g;
            strip->buf[idx + 2] = color.b;
            if (strip->is_rgbw)
                strip->buf[idx + 3] = rgb_luma(color);
            break;
    }
    return ESP_OK;
}

esp_err_t led_strip_set_pixels(led_strip_t *strip, size_t start, size_t len, rgb_t *data)
{
    CHECK_ARG(strip && strip->buf && data && len &&
              start < strip->length && len <= strip->length - start);
    for (size_t i = 0; i < len; i++)
        CHECK(led_strip_set_pixel(strip, i + start, data[i]));
    return ESP_OK;
}

esp_err_t led_strip_fill(led_strip_t *strip, size_t start, size_t len, rgb_t color)
{
    CHECK_ARG(strip && strip->buf && len &&
              start < strip->length && len <= strip->length - start);

    for (size_t i = start; i < start + len; i++)
        CHECK(led_strip_set_pixel(strip, i, color));
    return ESP_OK;
}
