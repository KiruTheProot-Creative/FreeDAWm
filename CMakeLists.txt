cmake_minimum_required(VERSION 3.22)

project(FreeDAWm VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(FDM_BUILD_TESTS "Build the engine tests and command-line tools" ON)
option(FDM_BUILD_APP   "Build the JUCE desktop application"            ON)
set(FDM_JUCE_PATH "" CACHE PATH
    "Path to a local JUCE checkout. If empty, JUCE is downloaded with FetchContent.")

# The audio engine: plain C++20, no JUCE, no GUI. Everything else builds on it.
add_subdirectory(core)

if(FDM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
    add_subdirectory(tools)
endif()

if(FDM_BUILD_APP)
    if(FDM_JUCE_PATH)
        add_subdirectory(${FDM_JUCE_PATH} ${CMAKE_BINARY_DIR}/juce EXCLUDE_FROM_ALL)
    else()
        include(FetchContent)
        FetchContent_Declare(juce
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG        9.0.4
            GIT_SHALLOW    TRUE)
        FetchContent_MakeAvailable(juce)
    endif()
    add_subdirectory(app)
endif()
