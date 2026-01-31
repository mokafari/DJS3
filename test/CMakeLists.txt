# Test Component CMakeLists.txt
# Uses ESP-IDF Unity test framework

idf_component_register(
    SRC_DIRS "."
    INCLUDE_DIRS "."
    REQUIRES unity main ui
    PRIV_REQUIRES esp_timer nvs_flash
)

# Register tests with Unity
target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-unused-function")

