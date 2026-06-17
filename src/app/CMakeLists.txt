# Product (display) name — used for the bundle name AND the installed-component
# asset path below, so they can never desync.
set(GUITAR_DSP_PRODUCT_NAME "Guitar Speak")

juce_add_plugin(guitar_dsp_app
    PRODUCT_NAME "${GUITAR_DSP_PRODUCT_NAME}"
    COMPANY_NAME "Todd B Fisher"
    BUNDLE_ID com.toddbfisher.guitarspeak
    PLUGIN_MANUFACTURER_CODE TdBF
    PLUGIN_CODE GtSp
    FORMATS Standalone AU
    IS_SYNTH FALSE
    # NEEDS_MIDI_INPUT FALSE so JUCE sets AU_MAIN_TYPE to 'aufx' (Effect),
    # which is the type Logic exposes a Side Chain dropdown for. With
    # NEEDS_MIDI_INPUT TRUE we got 'aumf' (Music Effect) — no sidechain UI
    # in Logic, breaking the Scene 4 mic capture path. The FCB1010 still
    # works in both Standalone and AU via the direct CoreMIDI route in
    # MidiRouter (host MIDI delivery via AU was an extra path, not the
    # only path), so dropping the AU MIDI bus costs us nothing operationally.
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    AU_MAIN_TYPE "kAudioUnitType_Effect"
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
    # macOS TCC privacy declarations. Required since macOS 14+ even though
    # this app never enumerates Bluetooth devices — JUCE's audio_devices
    # module links against the relevant frameworks, and TCC crashes any
    # app on launch that links them without a usage description.
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Guitar Speak listens to your voice to power the conversational-AI feature."
    BLUETOOTH_PERMISSION_ENABLED TRUE
    BLUETOOTH_PERMISSION_TEXT "Guitar Speak needs Bluetooth access only if you want to connect a Bluetooth MIDI foot controller."
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
    WaveformView.cpp
    SceneIndicator.cpp
    SpectrumAnalyzer.cpp
    VocoderPanel.cpp
    SayPanel.cpp
    MidiDevicePicker.cpp
    NoteReadout.cpp
    WordSyncSelector.cpp
    WordReadout.cpp
    TtsStatusBar.cpp
    StatePill.cpp
    ConversationPanel.cpp
    AiSettingsPanel.cpp
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
        guitar_dsp_ai
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

# Copy runtime assets into each built bundle's Resources directory.
#
# History: previously this used `add_custom_target(... ALL DEPENDS stamp)` —
# which only fired on a default build. `cmake --build build --target
# guitar_dsp_app_Standalone` (or _AU) would skip the copy and ship an empty
# bundle, breaking scene loading and STT. Switching to POST_BUILD on the format
# target itself makes the copy run whenever the target rebuilds, regardless of
# how the build was invoked.
#
# We wipe destination before copy so an asset removed/renamed at source
# also disappears from the bundle. `copy_directory` alone is additive and
# left stale carousel/old-scene JSON in the bundle for the live demo,
# causing SceneLibrary to load both sets and corrupt the presentation
# scene layout. Cost: copies all assets every build (<1 MB, negligible).
set(GUITAR_DSP_ASSETS_SRC  "${CMAKE_SOURCE_DIR}/assets")
set(GUITAR_DSP_WHISPER_SRC "${CMAKE_SOURCE_DIR}/Resources/whisper")

function(guitar_dsp_post_build_copy format_target src_dir dest_subdir)
    set(dest "$<TARGET_FILE_DIR:${format_target}>/../Resources/${dest_subdir}")
    add_custom_command(TARGET ${format_target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${dest}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${src_dir}" "${dest}"
        COMMENT "Copying ${dest_subdir}/ into ${format_target} bundle (clean)"
        VERBATIM)
endfunction()

guitar_dsp_post_build_copy(guitar_dsp_app_Standalone "${GUITAR_DSP_ASSETS_SRC}"  assets)
guitar_dsp_post_build_copy(guitar_dsp_app_Standalone "${GUITAR_DSP_WHISPER_SRC}" whisper)

if(TARGET guitar_dsp_app_AU)
    guitar_dsp_post_build_copy(guitar_dsp_app_AU "${GUITAR_DSP_ASSETS_SRC}"  assets)
    guitar_dsp_post_build_copy(guitar_dsp_app_AU "${GUITAR_DSP_WHISPER_SRC}" whisper)

    # COPY_PLUGIN_AFTER_BUILD installs the .component into ~/Library/Audio/
    # Plug-Ins/Components/ as a separate POST_BUILD step inside juce_add_plugin.
    # Our POST_BUILD commands are declared AFTER that block, so they run after
    # the installed bundle exists — copy assets + whisper into it too.
    set(GUITAR_DSP_AU_INSTALLED_ROOT
        "$ENV{HOME}/Library/Audio/Plug-Ins/Components/${GUITAR_DSP_PRODUCT_NAME}.component/Contents/Resources")
    add_custom_command(TARGET guitar_dsp_app_AU POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${GUITAR_DSP_AU_INSTALLED_ROOT}/assets"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${GUITAR_DSP_AU_INSTALLED_ROOT}/whisper"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${GUITAR_DSP_ASSETS_SRC}" "${GUITAR_DSP_AU_INSTALLED_ROOT}/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${GUITAR_DSP_WHISPER_SRC}" "${GUITAR_DSP_AU_INSTALLED_ROOT}/whisper"
        COMMENT "Copying assets/ + whisper/ into the installed AU .component (clean)"
        VERBATIM)
endif()
