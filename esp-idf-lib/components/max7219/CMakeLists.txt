set(req driver log)

if(IDF_VERSION_MAJOR GREATER 5 OR
   (IDF_VERSION_MAJOR EQUAL 5 AND IDF_VERSION_MINOR GREATER_EQUAL 3))
    list(APPEND req esp_driver_gpio esp_driver_spi)
endif()

idf_component_register(
    SRCS max7219.c
    INCLUDE_DIRS .
    REQUIRES ${req}
)
