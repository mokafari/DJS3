idf_component_register(
    SRCS 
        "src/audio_fx.c"
        "src/filter.c"
        "src/delay.c"
        "src/reverb.c"
        "src/echo.c"
        "src/eq.c"
    INCLUDE_DIRS 
        "include"
    PRIV_REQUIRES 
        driver
        freertos
)

