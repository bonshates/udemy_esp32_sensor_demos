set(req driver log color esp_idf_lib_helpers)

if(IDF_VERSION_MAJOR GREATER 5 OR
   (IDF_VERSION_MAJOR EQUAL 5 AND IDF_VERSION_MINOR GREATER_EQUAL 3))
    list(APPEND req esp_driver_gpio esp_driver_rmt)
endif()

if(IDF_VERSION_MAJOR GREATER_EQUAL 5)
    list(APPEND req esp_rom freertos heap)
endif()

idf_component_register(
    SRCS led_strip.c
    INCLUDE_DIRS .
    REQUIRES ${req}
)
