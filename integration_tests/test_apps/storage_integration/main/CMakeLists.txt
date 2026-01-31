idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES unity storage track_db nvs_flash
    PRIV_REQUIRES fatfs
)

