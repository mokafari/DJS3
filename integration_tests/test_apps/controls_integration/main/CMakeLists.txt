idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES unity controls pitch_control nvs_flash
    PRIV_REQUIRES driver
)

