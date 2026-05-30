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
