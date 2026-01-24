idf_component_register(
    SRCS 
        "src/midi_sync.c"
        "src/analog_sync.c"
        "src/ableton_link.c"
    INCLUDE_DIRS 
        "include"
    PRIV_REQUIRES 
        driver
        esp_timer
        freertos
)

