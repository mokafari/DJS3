# Register the component
file(GLOB_RECURSE SOURCES 
    "src/libhelix/*.c"
    "src/libhelix/real/*.c"
)

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS 
        "src/libhelix/pub"
        "src/libhelix/real"
)

# Suppress warnings in the library code
target_compile_options(${COMPONENT_LIB} PRIVATE 
    -Wno-unused-but-set-variable
    -Wno-unused-variable
    -Wno-sign-compare
)