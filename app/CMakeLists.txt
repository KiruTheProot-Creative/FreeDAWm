juce_add_gui_app(FreeDAWm
    PRODUCT_NAME "FreeDAWm"
    COMPANY_NAME "KiruTheProot-Creative"
    VERSION ${PROJECT_VERSION}
    BUNDLE_ID com.kirutheprootcreative.freedawm
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE)

target_sources(FreeDAWm PRIVATE
    Source/Main.cpp
    Source/AppState.cpp
    Source/PluginHost.cpp
    Source/PluginScanner.cpp
    Source/vst2/Vst2Format.cpp)

target_compile_definitions(FreeDAWm PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:FreeDAWm,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:FreeDAWm,JUCE_VERSION>")

target_link_libraries(FreeDAWm
    PRIVATE
        fdm::core
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# Effects shipped with the DAW: everything in /plugins is copied next to the executable.
add_custom_command(TARGET FreeDAWm POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${PROJECT_SOURCE_DIR}/plugins $<TARGET_FILE_DIR:FreeDAWm>/plugins)

# ---- clean-room VST2 host test: a tiny VST2 plugin + a console app that loads it ----
if(FDM_BUILD_TESTS)
    foreach(variant plain shell crash hang)
        add_library(fdm_test_vst2_${variant} MODULE tests/TestVst2Plugin.cpp)
        set_target_properties(fdm_test_vst2_${variant} PROPERTIES PREFIX "" CXX_VISIBILITY_PRESET hidden)
        target_compile_features(fdm_test_vst2_${variant} PRIVATE cxx_std_17)
        if(APPLE)   # on macOS a VST2 plugin is a .vst bundle
            set_target_properties(fdm_test_vst2_${variant} PROPERTIES BUNDLE TRUE BUNDLE_EXTENSION vst)
        endif()
    endforeach()
    target_compile_definitions(fdm_test_vst2_shell PRIVATE FDM_TEST_SHELL=1)
    target_compile_definitions(fdm_test_vst2_crash PRIVATE FDM_TEST_CRASH=1)
    target_compile_definitions(fdm_test_vst2_hang PRIVATE FDM_TEST_HANG=1)

    juce_add_console_app(fdm_vst2_host_test PRODUCT_NAME "fdm_vst2_host_test")
    target_sources(fdm_vst2_host_test PRIVATE tests/Vst2HostTest.cpp Source/vst2/Vst2Format.cpp Source/PluginScanner.cpp)
    target_compile_definitions(fdm_vst2_host_test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=0)
    target_link_libraries(fdm_vst2_host_test PRIVATE juce::juce_audio_processors juce::juce_audio_utils juce::juce_gui_basics
                          PUBLIC juce::juce_recommended_config_flags)
    add_dependencies(fdm_vst2_host_test fdm_test_vst2_plain fdm_test_vst2_shell fdm_test_vst2_crash fdm_test_vst2_hang)
    if(APPLE)
        add_test(NAME vst2_host COMMAND fdm_vst2_host_test $<TARGET_BUNDLE_DIR:fdm_test_vst2_plain> $<TARGET_BUNDLE_DIR:fdm_test_vst2_shell>
                 $<TARGET_BUNDLE_DIR:fdm_test_vst2_crash> $<TARGET_BUNDLE_DIR:fdm_test_vst2_hang>)
    else()
        add_test(NAME vst2_host COMMAND fdm_vst2_host_test $<TARGET_FILE:fdm_test_vst2_plain> $<TARGET_FILE:fdm_test_vst2_shell>
                 $<TARGET_FILE:fdm_test_vst2_crash> $<TARGET_FILE:fdm_test_vst2_hang>)
    endif()
endif()
