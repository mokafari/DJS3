# Test App Main Component
# Standalone test suite - tests pure logic functions without hardware dependencies

idf_component_register(
    SRCS 
        "test_runner.c"
        "test_duration_calculation.c"
        "test_waveform_view.c"
        "test_audio_player.c"
    INCLUDE_DIRS "."
    REQUIRES unity nvs_flash
)

# Suppress unused function warnings in tests
target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-unused-function")

