#include "i2c_diagnostics.h"

#include <stdbool.h>
#include <stdint.h>
#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "sdkconfig.h"

static const char *TAG = "i2c_diagnostics";

static bool scan_bus(bool internal_pullups, bool found[128])
{
    const gpio_num_t sda = CONFIG_EXAMPLE_I2C_MASTER_SDA;
    const gpio_num_t scl = CONFIG_EXAMPLE_I2C_MASTER_SCL;
    const char *phase = internal_pullups ? "internal-assisted" : "external-only";
    const gpio_config_t input_config = {
        .pin_bit_mask = (1ULL << sda) | (1ULL << scl),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = internal_pullups ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&input_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "%s GPIO configuration: %s", phase, esp_err_to_name(err));
        return false;
    }

    unsigned sda_high = 0, scl_high = 0;
    for (unsigned i = 0; i < 10; i++)
    {
        vTaskDelay(pdMS_TO_TICKS(10));
        sda_high += gpio_get_level(sda) != 0;
        scl_high += gpio_get_level(scl) != 0;
    }
    ESP_LOGI(TAG, "%s idle high samples: SDA=%u/10 SCL=%u/10", phase, sda_high, scl_high);
    if (sda_high != 10 || scl_high != 10)
        ESP_LOGW(TAG, "%s bus is not consistently idle high; inspect pull-ups, power and wiring", phase);

    const i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = internal_pullups,
    };
    i2c_master_bus_handle_t bus = NULL;
    err = i2c_new_master_bus(&bus_config, &bus);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "%s bus initialization: %s", phase, esp_err_to_name(err));
        return false;
    }

    unsigned ack = 0, nack = 0, timeout = 0, other = 0;
    // Address phases only: no register or output-latch data is transmitted.
    for (unsigned address = 0x08; address <= 0x77; address++)
    {
        err = i2c_master_probe(bus, address, 30);
        if (err == ESP_OK)
        {
            found[address] = true;
            ack++;
            bool pcf_range = (address >= 0x20 && address <= 0x27) ||
                             (address >= 0x38 && address <= 0x3f);
            ESP_LOGI(TAG, "%s ACK at 7-bit address 0x%02x%s", phase, address,
                     pcf_range ? " (PCF8574-family candidate; not chip identification)" : "");
        }
        else if (err == ESP_ERR_NOT_FOUND)
            nack++;
        else if (err == ESP_ERR_TIMEOUT)
            timeout++;
        else
        {
            other++;
            ESP_LOGE(TAG, "%s probe 0x%02x: %s", phase, address, esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGI(TAG, "%s scan: ACK=%u NACK=%u timeout=%u other=%u",
             phase, ack, nack, timeout, other);
    if (ack > 16)
        ESP_LOGW(TAG, "Unusually many ACKs; a stuck-low SDA can mimic device responses");
    err = i2c_del_master_bus(bus);
    if (err != ESP_OK)
        ESP_LOGE(TAG, "%s bus cleanup: %s", phase, esp_err_to_name(err));
    return err == ESP_OK;
}

void i2c_diagnostics_run(void)
{
    const int sda = CONFIG_EXAMPLE_I2C_MASTER_SDA;
    const int scl = CONFIG_EXAMPLE_I2C_MASTER_SCL;
    const unsigned configured = CONFIG_HARDWARE_TEST_PCF8574_ADDRESS;
    if (!GPIO_IS_VALID_OUTPUT_GPIO(sda) || !GPIO_IS_VALID_OUTPUT_GPIO(scl) ||
        sda == scl || configured >= 128)
    {
        ESP_LOGE(TAG, "Invalid I2C pin/address configuration");
        ESP_LOGI(TAG, "I2C diagnostics finished");
        return;
    }

    ESP_LOGI(TAG, "Address-only diagnostics: SDA=%d SCL=%d configured address=0x%02x", sda, scl, configured);
    ESP_LOGI(TAG, "No LED transmissions or PCF8574 output-data writes will be performed");
    bool external[128] = {false}, assisted[128] = {false};
    bool external_ok = scan_bus(false, external);
    // Do not allocate another bus if the first bus could not be cleaned up.
    bool assisted_ok = external_ok && scan_bus(true, assisted);
    ESP_LOGI(TAG, "Configured 0x%02x ACK: external-only=%s internal-assisted=%s",
             configured, external_ok ? (external[configured] ? "yes" : "no") : "scan failed",
             assisted_ok ? (assisted[configured] ? "yes" : "no") : "scan failed/skipped");
    for (unsigned addr = 0x20; addr <= 0x3f; addr++)
    {
        if (addr >= 0x28 && addr < 0x38)
            continue;
        if (external[addr] || assisted[addr])
            ESP_LOGI(TAG, "PCF-family candidate 0x%02x: external=%s assisted=%s",
                     addr, external[addr] ? "ACK" : "no ACK", assisted[addr] ? "ACK" : "no ACK");
    }
    ESP_LOGW(TAG, "Digital idle levels do not measure resistor values, bus voltage, or rise time");
    ESP_LOGI(TAG, "Use external pull-ups to 3.3V; weak internal pull-ups are diagnostic only");
    // Leave both pins as high-impedance inputs with no internal pull resistors.
    const gpio_config_t release = {
        .pin_bit_mask = (1ULL << sda) | (1ULL << scl),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (external_ok)
    {
        esp_err_t err = gpio_config(&release);
        if (err != ESP_OK)
            ESP_LOGE(TAG, "GPIO release: %s", esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "I2C diagnostics finished");
}
