if(${IDF_TARGET} STREQUAL esp8266)
    set(req esp8266 freertos log esp_idf_lib_helpers)
else()
    set(req driver freertos log esp_idf_lib_helpers)
endif()

if(IDF_VERSION_MAJOR GREATER 5 OR
   (IDF_VERSION_MAJOR EQUAL 5 AND IDF_VERSION_MINOR GREATER_EQUAL 3))
    list(APPEND req esp_driver_gpio)
endif()

idf_component_register(
    SRCS dht.c
    INCLUDE_DIRS .
    REQUIRES ${req}
)
