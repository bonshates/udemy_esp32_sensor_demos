set(req i2cdev log)

if(IDF_VERSION_MAJOR GREATER 5 OR
   (IDF_VERSION_MAJOR EQUAL 5 AND IDF_VERSION_MINOR GREATER_EQUAL 3))
    list(APPEND req esp_driver_gpio)
endif()

idf_component_register(
    SRCS ht16k33.c
    INCLUDE_DIRS .
    REQUIRES ${req}
)
