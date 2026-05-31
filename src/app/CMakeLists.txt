juce_add_plugin(guitar_dsp_app
    PRODUCT_NAME "Guitar DSP"
    COMPANY_NAME "GuitarDSP"
    BUNDLE_ID com.guitardsp.app
    PLUGIN_MANUFACTURER_CODE GtDs
    PLUGIN_CODE GtAp
    FORMATS Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    # macOS TCC privacy declarations. Required since macOS 14+ even though
    # this app never enumerates Bluetooth devices — JUCE's audio_devices
    # module links against the relevant frameworks, and TCC crashes any
    # app on launch that links them without a usage description.
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Guitar DSP needs audio input access to process your guitar signal."
    BLUETOOTH_PERMISSION_ENABLED TRUE
    BLUETOOTH_PERMISSION_TEXT "Guitar DSP needs Bluetooth access only if you want to connect a Bluetooth MIDI foot controller."
)

juce_generate_juce_header(guitar_dsp_app)

target_sources(guitar_dsp_app PRIVATE
    PluginProcessor.cpp
    PluginEditor.cpp
    DiagnosticPanel.cpp
    Oscilloscope.cpp
    SpectrumAnalyzer.cpp
)

target_compile_definitions(guitar_dsp_app PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(guitar_dsp_app
    PRIVATE
        guitar_dsp_audio
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Copy assets/ into the Standalone .app's Resources directory after build.
# Path is JUCE-specific: <target>_artefacts/<BUILD_TYPE>/Standalone/<PRODUCT_NAME>.app
# Find assets/ directory at configure-time. Resolve to absolute path.
set(_GUITAR_DSP_ASSET_CANDIDATES
    "${CMAKE_BINARY_DIR}/../.claude/worktrees/phase-2-scenes-and-midi/assets"
    "${PROJECT_SOURCE_DIR}/assets"
    "${CMAKE_BINARY_DIR}/../assets"
)

foreach(_CANDIDATE ${_GUITAR_DSP_ASSET_CANDIDATES})
    if(EXISTS "${_CANDIDATE}")
        get_filename_component(GUITAR_DSP_ASSETS_SRC "${_CANDIDATE}" ABSOLUTE)
        break()
    endif()
endforeach()

if(NOT GUITAR_DSP_ASSETS_SRC)
    message(FATAL_ERROR "Could not find assets/ directory")
endif()

set(GUITAR_DSP_APP_RESOURCES
    "$<TARGET_FILE_DIR:guitar_dsp_app_Standalone>/../Resources/assets")

add_custom_command(TARGET guitar_dsp_app_Standalone POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${GUITAR_DSP_APP_RESOURCES}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${GUITAR_DSP_ASSETS_SRC}" "${GUITAR_DSP_APP_RESOURCES}"
    COMMENT "Copying assets/ into Guitar DSP.app/Contents/Resources/assets/"
)
