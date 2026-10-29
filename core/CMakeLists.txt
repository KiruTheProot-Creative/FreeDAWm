add_library(fdm_core STATIC
    src/Clip.cpp
    src/DemoProject.cpp
    src/DrumRack.cpp
    src/Engine.cpp
    src/PolySynth.cpp
    src/Wav.cpp)

add_library(fdm::core ALIAS fdm_core)

target_include_directories(fdm_core PUBLIC include)
target_compile_features(fdm_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(fdm_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(fdm_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
