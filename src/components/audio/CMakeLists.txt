idf_component_register(
    SRCS "audio.cpp"
    INCLUDE_DIRS "." "${CMAKE_CURRENT_SOURCE_DIR}/../../../shared"
    REQUIRES driver vfs freertos chmorgan__esp-audio-player
    PRIV_REQUIRES board seq
)
target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++17)
