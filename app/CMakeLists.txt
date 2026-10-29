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
    Source/PluginHost.cpp)

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
