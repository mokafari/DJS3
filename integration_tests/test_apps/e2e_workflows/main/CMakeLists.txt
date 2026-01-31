idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES unity audio_output audio_player storage track_db controls pitch_control cue_points loop_control display ui nvs_flash
    PRIV_REQUIRES esp_timer driver
)

