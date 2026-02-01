idf_component_register(
    SRCS "main.c"
         "../../../../main/dsp_engine.c"
         "../../../../main/pitch_control.c"
    INCLUDE_DIRS "."
                 "../../../../main/include"
    REQUIRES unity nvs_flash fx
    PRIV_REQUIRES esp_timer driver
)
