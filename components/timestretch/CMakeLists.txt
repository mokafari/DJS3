idf_component_register(
    SRCS 
        "src/granular_engine.c"
        "src/beat_sync.c"
        "src/slip_loop.c"
        "src/synth_mode.c"
    INCLUDE_DIRS 
        "include"
    PRIV_REQUIRES 
        driver
        esp_timer
        freertos
)

