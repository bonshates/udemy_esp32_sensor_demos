if(${IDF_TARGET} STREQUAL esp8266)
    set(req esp8266 log color esp_idf_lib_helpers)
else()
    set(req driver log color esp_idf_lib_helpers)
endif()

if(IDF_VERSION_MAJOR GREATER 5 OR
   (IDF_VERSION_MAJOR EQUAL 5 AND IDF_VERSION_MINOR GREATER_EQUAL 3))
    list(APPEND req esp_driver_gpio esp_driver_spi)
endif()

idf_component_register(
    SRCS led_strip_spi.c led_strip_spi_sk9822.c
    INCLUDE_DIRS .
    REQUIRES ${req}
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Werror=override-init)
