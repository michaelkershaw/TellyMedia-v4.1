cmake_minimum_required(VERSION 3.15)
project(VDJShaderConverter)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add executable
add_executable(VDJShaderConverter
    src/main.cpp
    src/Converter.cpp
    src/IniHandler.cpp
    src/Logger.cpp
)

# Windows-specific settings
if(WIN32)
    target_link_libraries(VDJShaderConverter PRIVATE comctl32 shell32)
    set_target_properties(VDJShaderConverter PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()
