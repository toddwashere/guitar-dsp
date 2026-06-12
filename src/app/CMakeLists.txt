juce_add_plugin(guitar_dsp_app
    PRODUCT_NAME "Guitar DSP"
    COMPANY_NAME "GuitarDSP"
    BUNDLE_ID com.guitardsp.app
    PLUGIN_MANUFACTURER_CODE GtDs
    PLUGIN_CODE GtAp
    FORMATS Standalone AU
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
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
    AssetLocator.cpp
    PluginState.cpp
    PluginProcessor.cpp
    PluginEditor.cpp
    DiagnosticPanel.cpp
    DiagToggleBar.cpp
    Oscilloscope.cpp
    SceneIndicator.cpp
    SpectrumAnalyzer.cpp
    VocoderPanel.cpp
    SayPanel.cpp
    MidiDevicePicker.cpp
    WordReadout.cpp
    TtsStatusBar.cpp
)

target_compile_definitions(guitar_dsp_app PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(guitar_dsp_app
    PRIVATE
        guitar_dsp_audio
        guitar_dsp_midi
        guitar_dsp_scenes
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

# Copy assets/ into each built plugin bundle's Resources directory.
# Stamp-file pattern re-runs the copy whenever any file under assets/ changes.
set(GUITAR_DSP_ASSETS_SRC "${CMAKE_SOURCE_DIR}/assets")
file(GLOB_RECURSE GUITAR_DSP_ASSET_FILES CONFIGURE_DEPENDS
    "${GUITAR_DSP_ASSETS_SRC}/*")

# One copy command per format target. Each format's executable lives at
# <bundle>/Contents/MacOS, so ../Resources is the bundle's Resources dir.
function(guitar_dsp_copy_assets_to format_target)
    set(dest "$<TARGET_FILE_DIR:${format_target}>/../Resources/assets")
    set(stamp "${CMAKE_CURRENT_BINARY_DIR}/assets_copied_${format_target}.stamp")
    add_custom_command(
        OUTPUT "${stamp}"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${dest}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${GUITAR_DSP_ASSETS_SRC}" "${dest}"
        COMMAND ${CMAKE_COMMAND} -E touch "${stamp}"
        DEPENDS ${GUITAR_DSP_ASSET_FILES} ${format_target}
        COMMENT "Copying assets/ into ${format_target} bundle Resources/"
        VERBATIM)
    add_custom_target(copy_assets_${format_target} ALL DEPENDS "${stamp}")
endfunction()

guitar_dsp_copy_assets_to(guitar_dsp_app_Standalone)
if(TARGET guitar_dsp_app_AU)
    guitar_dsp_copy_assets_to(guitar_dsp_app_AU)
    # COPY_PLUGIN_AFTER_BUILD installs the .component to the user's Components
    # folder BEFORE the in-tree asset copy runs, so the installed copy would
    # otherwise lack assets. Copy assets into the installed bundle too.
    set(GUITAR_DSP_AU_INSTALLED
        "$ENV{HOME}/Library/Audio/Plug-Ins/Components/Guitar DSP.component/Contents/Resources/assets")
    set(GUITAR_DSP_AU_INSTALLED_STAMP
        "${CMAKE_CURRENT_BINARY_DIR}/assets_copied_AU_installed.stamp")
    add_custom_command(
        OUTPUT "${GUITAR_DSP_AU_INSTALLED_STAMP}"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${GUITAR_DSP_AU_INSTALLED}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${GUITAR_DSP_ASSETS_SRC}" "${GUITAR_DSP_AU_INSTALLED}"
        COMMAND ${CMAKE_COMMAND} -E touch "${GUITAR_DSP_AU_INSTALLED_STAMP}"
        DEPENDS ${GUITAR_DSP_ASSET_FILES} guitar_dsp_app_AU
        COMMENT "Copying assets/ into the installed AU .component"
        VERBATIM)
    add_custom_target(copy_assets_AU_installed ALL
        DEPENDS "${GUITAR_DSP_AU_INSTALLED_STAMP}")
endif()
