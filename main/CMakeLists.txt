idf_component_register(
    SRCS "main.c" "audio_output.cpp" "usb_host.c" "sd_card.c" "storage.c" "audio_player.cpp" "controls.c" "pitch_control.c" "cue_points.c" "loop_control.c" "display.c" "waveform.c" "track_db.c" "id3_parser.c"
    INCLUDE_DIRS "include"
    REQUIRES driver nvs_flash esp_psram esp8266audio fatfs usb ui timestretch
)

# Set C standard
target_compile_options(${COMPONENT_LIB} PRIVATE 
    -std=gnu11
    -Wall
    -Wextra
    -Wno-unused-parameter
)

# Compile audio_output.cpp as C++
target_compile_options(${COMPONENT_LIB} PRIVATE 
    $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>
)

# ESP32-S3 specific flags
if(CONFIG_IDF_TARGET_ESP32S3)
    target_compile_options(${COMPONENT_LIB} PRIVATE 
        -mfix-esp32-psram-cache-issue
    )
endif()

