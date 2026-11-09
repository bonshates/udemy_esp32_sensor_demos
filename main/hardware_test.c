#include "hardware_test.h"
#include "i2c_diagnostics.h"

#include <stdbool.h>
#include <stdint.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <i2cdev.h>
#include <led_strip.h>
#include <pcf8574.h>
#include "sdkconfig.h"

static const char *TAG = "hardware_test";

static bool check_result(const char *operation, esp_err_t err)
{
    if (err == ESP_OK)
        return true;
    ESP_LOGE(TAG, "%s: %s (0x%x)", operation, esp_err_to_name(err), (unsigned)err);
    return false;
}

void hardware_test_run(void)
{
#if CONFIG_HARDWARE_TEST_I2C_DIAGNOSTICS_ONLY
    i2c_diagnostics_run();
    return;
#endif
    const int address = CONFIG_HARDWARE_TEST_PCF8574_ADDRESS;
    if (!((address >= 0x20 && address <= 0x27) || (address >= 0x38 && address <= 0x3f)) ||
        !GPIO_IS_VALID_OUTPUT_GPIO(CONFIG_HARDWARE_TEST_LED_GPIO) ||
        !GPIO_IS_VALID_OUTPUT_GPIO(CONFIG_EXAMPLE_I2C_MASTER_SDA) ||
        !GPIO_IS_VALID_OUTPUT_GPIO(CONFIG_EXAMPLE_I2C_MASTER_SCL) ||
        CONFIG_HARDWARE_TEST_LED_GPIO == CONFIG_EXAMPLE_I2C_MASTER_SDA ||
        CONFIG_HARDWARE_TEST_LED_GPIO == CONFIG_EXAMPLE_I2C_MASTER_SCL ||
        CONFIG_EXAMPLE_I2C_MASTER_SDA == CONFIG_EXAMPLE_I2C_MASTER_SCL)
    {
        ESP_LOGE(TAG, "Invalid test address, GPIO, or overlapping peripheral pins");
        return;
    }

    led_strip_t strip = {
        .type = CONFIG_HARDWARE_TEST_LED_TYPE,
        .length = CONFIG_HARDWARE_TEST_LED_COUNT,
        .gpio = CONFIG_HARDWARE_TEST_LED_GPIO,
        .brightness = CONFIG_HARDWARE_TEST_LED_BRIGHTNESS,
#if CONFIG_HARDWARE_TEST_LED_RGBW
        .is_rgbw = true,
#endif
    };
    i2c_dev_t expander = {0};
    const rgb_t colors[] = {
        { .r = 255 }, { .g = 255 }, { .b = 255 },
        { .r = 255, .g = 255, .b = 255 }, { .r = 0, .g = 0, .b = 0 },
    };
    const char *color_names[] = {"red", "green", "blue", "white", "off"};
    const size_t color_count = sizeof(colors) / sizeof(colors[0]);
    unsigned led_ok = 0, pcf_ok = 0;

    ESP_LOGI(TAG, "LED: GPIO%d, type=%d, count=%d, brightness=%d",
             CONFIG_HARDWARE_TEST_LED_GPIO, CONFIG_HARDWARE_TEST_LED_TYPE,
             CONFIG_HARDWARE_TEST_LED_COUNT, CONFIG_HARDWARE_TEST_LED_BRIGHTNESS);
    ESP_LOGI(TAG, "PCF8574: address=0x%02x, SDA=%d, SCL=%d; external 3.3V pull-ups required",
             address, CONFIG_EXAMPLE_I2C_MASTER_SDA, CONFIG_EXAMPLE_I2C_MASTER_SCL);
    ESP_LOGI(TAG, "P0 drives an active-low test LED; P1 is an optional button to GND");
    ESP_LOGI(TAG, "Only P0 will be driven low. All other PCF8574 pins remain released");

    led_strip_install();
    bool led_ready = check_result("LED initialization", led_strip_init(&strip));
    bool i2c_ready = check_result("I2C initialization", i2cdev_init());
    bool pcf_ready = false;
    if (i2c_ready)
        pcf_ready = check_result("PCF8574 descriptor initialization",
            pcf8574_init_desc(&expander, address, I2C_NUM_0,
                             CONFIG_EXAMPLE_I2C_MASTER_SDA, CONFIG_EXAMPLE_I2C_MASTER_SCL));

    // Run independently so an absent I2C peripheral does not suppress the LED test.
    for (unsigned step = 0; step < CONFIG_HARDWARE_TEST_STEPS; step++)
    {
        size_t color = step % color_count;
        ESP_LOGI(TAG, "Step %u/%d: strip=%s, PCF P0 LED=%s", step + 1,
                 CONFIG_HARDWARE_TEST_STEPS, color_names[color], step % 2 ? "off" : "on");
        if (led_ready &&
            check_result("LED fill", led_strip_fill(&strip, 0, strip.length, colors[color])) &&
            check_result("LED flush", led_strip_flush(&strip)) &&
            check_result("LED TX completion",
                         led_strip_wait(&strip, pdMS_TO_TICKS(CONFIG_LED_STRIP_FLUSH_TIMEOUT))))
            led_ok++;

        if (pcf_ready)
        {
            // Ones release quasi-bidirectional inputs; never drive the button pin low.
            uint8_t output = step % 2 ? 0xff : 0xfe;
            if (check_result("PCF8574 write", pcf8574_port_write(&expander, output)))
            {
                // Allow the output and the weak pull-up to settle before readback.
                vTaskDelay(pdMS_TO_TICKS(10));
                uint8_t input = 0;
                if (check_result("PCF8574 read", pcf8574_port_read(&expander, &input)))
                {
                    ESP_LOGI(TAG, "PCF8574 port=0x%02x, P1 button=%s",
                             input, input & 0x02 ? "released" : "pressed");
                    if ((input & 0x01) == (output & 0x01))
                        pcf_ok++;
                    else
                        ESP_LOGE(TAG, "PCF8574 P0 readback mismatch: expected=%u actual=%u",
                                 (unsigned)(output & 1), (unsigned)(input & 1));
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(CONFIG_HARDWARE_TEST_STEP_MS));
    }

    // Leave the strip dark and release the expander pins, including P0.
    bool led_cleanup = false, pcf_cleanup = false;
    if (led_ready)
    {
        led_cleanup = check_result("LED off fill",
            led_strip_fill(&strip, 0, strip.length, (rgb_t){ .r = 0, .g = 0, .b = 0 }));
        if (led_cleanup)
            led_cleanup = check_result("LED off flush", led_strip_flush(&strip));
        bool freed = check_result("LED shutdown", led_strip_free(&strip));
        led_cleanup = led_cleanup && freed;
    }
    if (pcf_ready)
    {
        pcf_cleanup = check_result("PCF8574 release all pins", pcf8574_port_write(&expander, 0xff));
        bool freed = check_result("PCF8574 shutdown", pcf8574_free_desc(&expander));
        pcf_cleanup = pcf_cleanup && freed;
    }
    if (i2c_ready)
    {
        bool stopped = check_result("I2C shutdown", i2cdev_done());
        pcf_cleanup = pcf_cleanup && stopped;
    }

    ESP_LOGI(TAG, "LED RMT API checks: %s (%u/%d steps)",
             led_ok == CONFIG_HARDWARE_TEST_STEPS && led_cleanup ? "PASS" : "FAIL",
             led_ok, CONFIG_HARDWARE_TEST_STEPS);
    ESP_LOGI(TAG, "PCF8574 bus/P0 readback: %s (%u/%d steps)",
             pcf_ok == CONFIG_HARDWARE_TEST_STEPS && pcf_cleanup ? "PASS" : "FAIL",
             pcf_ok, CONFIG_HARDWARE_TEST_STEPS);
    ESP_LOGW(TAG, "RMT completion does not detect an attached strip: verify colors visually");
    ESP_LOGI(TAG, "Verify P0 LED toggles and P1 button readings change when pressed");
    ESP_LOGI(TAG, "Test finished; no automatic repeat");
}
