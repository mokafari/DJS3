idf_component_register(
    SRCS 
        "main.c" 
        "audio_output.cpp" 
        "usb_host.c" 
        "sd_card.c" 
        "storage.c" 
        "audio_player.cpp" 
        "controls.c" 
        "pitch_control.c"
        "pitch_fader.c" 
        "cue_points.c" 
        "loop_control.c" 
        "display.c" 
        "waveform.c" 
        "track_db.c" 
        "id3_parser.c" 
        "hw_test_harness.c" 
        "dsp_engine.c"
        "dsp_pipeline.c"
        # OpenDeck Metadata System
        "metadata.c"
        "analyzer.c"
        "library_db.c"
        "playlist_manager.c"
        "analysis_task.c"
        "track_history.c"
        "track_prep.c"
        "preferences.c"
        # Slip Mode System
        "slip_mode.c"
        # Jog Wheel Controller
        "jog_wheel.c"
        # Master Output Recorder
        "recorder.c"
        # LED Controller
        "led_controller.c"
        # External Controller System
        "ext_controller.c"
        # Auto-DJ System
        "auto_dj.c"
    INCLUDE_DIRS "include"
    REQUIRES driver nvs_flash esp_psram libhelix-mp3 fatfs usb ui timestretch esp_timer esp-dsp fx sync analysis playlist esp_adc
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

