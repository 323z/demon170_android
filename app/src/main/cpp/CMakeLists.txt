cmake_minimum_required(VERSION 3.10)
project(demon170 NONE)

# Force C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source files
add_library(demon170 SHARED
    demon170.cpp
)

# Include directories
target_include_directories(demon170 PRIVATE
    ${CMAKE_SOURCE_DIR}
)

# Compiler flags for Android
target_compile_options(demon170 PRIVATE
    -O2
    -fexceptions
    -frtti
    -Wall
    -Wno-unused-parameter
    -ffunction-sections
    -fdata-sections
)

# Link libraries
target_link_libraries(demon170
    EGL
    GLESv2
    log
    android
)
