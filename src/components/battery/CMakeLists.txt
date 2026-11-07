idf_component_register(
    SRCS "battery.cpp"
    INCLUDE_DIRS "."
    REQUIRES freertos
    PRIV_REQUIRES board esp_adc esp_driver_i2c
)
target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++17)
