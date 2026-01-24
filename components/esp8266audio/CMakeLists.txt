# Collect libmad source files
file(GLOB LIBMAD_SOURCES "src/libmad/*.c")

idf_component_register(
    SRCS 
        "src/AudioLogger.cpp"
        "src/AudioOutputI2S.cpp"
        "src/AudioFileSourceFS.cpp"
        "src/AudioGeneratorMP3.cpp"
        "src/stack_helpers.c"
        ${LIBMAD_SOURCES}
    INCLUDE_DIRS 
        "include"
        "include/libmad"
    REQUIRES 
        driver
        fatfs
)

# Set C++ standard
target_compile_options(${COMPONENT_LIB} PRIVATE 
    -std=gnu++17
    -Wall
    -Wextra
    -Wno-unused-parameter
)

# ESP32-S3 specific flags
if(CONFIG_IDF_TARGET_ESP32S3)
    target_compile_options(${COMPONENT_LIB} PRIVATE 
        -mfix-esp32-psram-cache-issue
    )
endif()

