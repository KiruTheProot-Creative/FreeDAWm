add_executable(fdm_tests
    main.cpp
    test_engine.cpp
    test_instruments.cpp
    test_clip_wav.cpp)

target_link_libraries(fdm_tests PRIVATE fdm::core)

if(MSVC)
    target_compile_options(fdm_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(fdm_tests PRIVATE -Wall -Wextra)
endif()

add_test(NAME engine COMMAND fdm_tests WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
