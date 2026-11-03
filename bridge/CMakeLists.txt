# The plugin bridge: a small JUCE-free helper process that hosts one VST2 plugin.
# Built as 64-bit with the app (sandbox), and separately as 32-bit for 32-bit plugins:
#   Windows: cmake -S bridge -B build32 -A Win32 && cmake --build build32 --config Release
#   Linux:   cmake -S bridge -B build32 -DCMAKE_CXX_FLAGS=-m32 -DCMAKE_C_FLAGS=-m32 && cmake --build build32
cmake_minimum_required(VERSION 3.22)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(FreeDAWmBridge LANGUAGES C CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
    endif()
    option(FDM_BRIDGE_TEST_PLUGINS "Also build the test VST2 plugins for this architecture" OFF)
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(FDM_BRIDGE_NAME fdm_bridge32)
else()
    set(FDM_BRIDGE_NAME fdm_bridge64)
endif()

add_executable(fdm_bridge BridgeMain.cpp Ipc.cpp)
set_target_properties(fdm_bridge PROPERTIES OUTPUT_NAME ${FDM_BRIDGE_NAME})
target_compile_features(fdm_bridge PRIVATE cxx_std_17)
if(WIN32)
    target_link_libraries(fdm_bridge PRIVATE ole32 user32)
    set_target_properties(fdm_bridge PROPERTIES WIN32_EXECUTABLE TRUE)        # no console window
    if(MINGW)
        target_link_options(fdm_bridge PRIVATE -municode)
    endif()
    target_compile_definitions(fdm_bridge PRIVATE UNICODE _UNICODE)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(fdm_bridge PRIVATE Threads::Threads ${CMAKE_DL_LIBS} rt)
endif()
if(MSVC)
    # main() still works as the entry point of a WIN32 executable
    target_link_options(fdm_bridge PRIVATE /ENTRY:mainCRTStartup)
endif()

if(FDM_BRIDGE_TEST_PLUGINS)
    foreach(variant plain synth crash)
        add_library(fdm_test_vst2_${variant} MODULE ../app/tests/TestVst2Plugin.cpp)
        set_target_properties(fdm_test_vst2_${variant} PROPERTIES PREFIX "" CXX_VISIBILITY_PRESET hidden)
    endforeach()
    target_compile_definitions(fdm_test_vst2_synth PRIVATE FDM_TEST_SYNTH=1)
    target_compile_definitions(fdm_test_vst2_crash PRIVATE FDM_TEST_CRASH=1)
endif()
