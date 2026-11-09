if(${IDF_TARGET} STREQUAL esp8266)
    set(req esp8266 log esp_idf_lib_helpers esp_timer)
elseif(${IDF_VERSION_MAJOR} STREQUAL 4 AND ${IDF_VERSION_MINOR} STREQUAL 1 AND ${IDF_VERSION_PATCH} STREQUAL 3)
    set(req driver log esp_idf_lib_helpers)
else()
    set(req driver log esp_idf_lib_helpers esp_timer)
endif()

if(IDF_VERSION_MAJOR GREATER 5 OR
   (IDF_VERSION_MAJOR EQUAL 5 AND IDF_VERSION_MINOR GREATER_EQUAL 3))
    list(APPEND req esp_driver_gpio)
endif()

idf_component_register(
    SRCS wiegand.c
    INCLUDE_DIRS .
    REQUIRES ${req}
)
