# See the build system documentation in IDF programming guide
# for more information about component CMakeLists.txt files.

if(CONFIG_HARDWARE_TEST_ENABLED)
    set(app_sources main.c hardware_test.c i2c_diagnostics.c)
else()
    set(app_sources main.c bme680_sensor.c)
endif()

idf_component_register(SRCS ${app_sources}
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES led_strip pcf8574 bme680 i2cdev esp_driver_gpio esp_driver_i2c freertos log)
