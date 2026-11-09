if(${IDF_VERSION_MAJOR} STREQUAL 4 AND ${IDF_VERSION_MINOR} STREQUAL 1 AND ${IDF_VERSION_PATCH} STREQUAL 3)
	set(req log esp_idf_lib_helpers driver)
else()
	set(req log esp_idf_lib_helpers driver esp_timer)
endif()

if(IDF_VERSION_MAJOR GREATER 5 OR
   (IDF_VERSION_MAJOR EQUAL 5 AND IDF_VERSION_MINOR GREATER_EQUAL 3))
    list(APPEND req esp_driver_gpio esp_driver_uart)
endif()

idf_component_register(SRCS "mhz19b.c"
                       INCLUDE_DIRS "."
                       REQUIRES ${req})
