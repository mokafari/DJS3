idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES unity display nvs_flash
    PRIV_REQUIRES driver
)

