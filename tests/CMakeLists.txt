find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Synthetic test bundle for sung-vowels tests. Re-built whenever the
# builder script changes.
set(SUNG_VOWELS_TEST_BUNDLE
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/sung_vowels_test.gspeak)
add_custom_command(
    OUTPUT ${SUNG_VOWELS_TEST_BUNDLE}
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/build_sung_vowels_test_bundle.py
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/build_sung_vowels_test_bundle.py
    COMMENT "Building sung_vowels_test.gspeak fixture"
    VERBATIM)
add_custom_target(sung_vowels_test_bundle ALL DEPENDS ${SUNG_VOWELS_TEST_BUNDLE})

add_executable(guitar_dsp_tests
    unit/test_smoke.cpp
    unit/test_realtime_sentinel.cpp
    unit/test_synthetic_guitar.cpp
    unit/test_golden_file.cpp
    unit/audio/test_input_stage.cpp
    unit/audio/test_mixer.cpp
    unit/audio/test_audio_graph.cpp
    unit/audio/test_ivocoder_compiles.cpp
    unit/audio/test_channel_vocoder.cpp
    unit/audio/test_tts_clip_player.cpp
    unit/audio/test_prebaked_tts_source.cpp
    unit/audio/test_apple_tts_source.cpp
    unit/audio/test_piper_tts_source.cpp
    unit/audio/test_tts_prewarmer.cpp
    unit/audio/test_word_segment.cpp
    unit/audio/test_crusher.cpp
    unit/audio/test_carousel_mod.cpp
    unit/audio/test_carousel.cpp
    unit/audio/test_pitch_shifter.cpp
    unit/audio/test_pitch_tracked_carrier.cpp
    unit/audio/test_harmonizer.cpp
    unit/audio/test_comb.cpp
    unit/audio/test_formant.cpp
    unit/audio/test_formant_modulator.cpp
    unit/audio/test_onset_detector.cpp
    unit/audio/test_word_aligner.cpp
    unit/audio/test_audio_file_decoder.cpp
    unit/audio/test_note_stepped_player.cpp
    unit/audio/test_v1_golden.cpp
    unit/audio/test_clip_bank_player.cpp
    unit/audio/test_mic_capture.cpp
    unit/audio/test_mic_scope_buffer.cpp
    unit/audio/test_mic_shaper.cpp
    unit/audio/test_gspeak_bundle.cpp
    unit/audio/test_phoneme_compiles.cpp
    unit/audio/test_phoneme_extractor.cpp
    unit/audio/test_syllabifier.cpp
    unit/audio/test_phoneme_aligned_clip_builder.cpp
    unit/audio/test_v1_boundary_edits.cpp
    unit/audio/test_vowel_grain_loop.cpp
    unit/audio/test_phoneme_stepped_player.cpp
    unit/audio/test_formant_shifter.cpp
    unit/audio/test_sung_direct_path.cpp
    unit/scenes/test_scene_compiles.cpp
    unit/scenes/test_scene_library.cpp
    unit/scenes/test_scene_library_tts.cpp
    unit/scenes/test_scene_library_tts_text.cpp
    unit/scenes/test_scene_library_tts_bank.cpp
    unit/scenes/test_scene_library_tts_fallback.cpp
    unit/scenes/test_scene_library_tts_trigger.cpp
    unit/scenes/test_scene_library_sung_vowels.cpp
    unit/scenes/test_scene_library_sung_direct.cpp
    unit/scenes/test_scene_library_carousel.cpp
    unit/scenes/test_scene_library_carousel_pitch.cpp
    unit/scenes/test_scene_library_carousel_formant.cpp
    unit/scenes/test_scene_library_speech_v2.cpp
    unit/scenes/test_scene_library_developers.cpp
    unit/scenes/test_scene_resolved_gspeak_path.cpp
    unit/scenes/test_scene_engine.cpp
    unit/scenes/test_scene_engine_carousel.cpp
    unit/midi/test_fcb1010_mapping.cpp
    unit/midi/test_fcb1010_mapping_ai.cpp
    unit/midi/test_host_midi_scene_router.cpp
    unit/midi/test_midi_router_selection.cpp
    unit/ai/test_conversation_buffer.cpp
    unit/ai/test_persona_registry.cpp
    unit/ai/test_knowledge_doc.cpp
    unit/ai/test_app_preferences.cpp
    unit/ai/test_http_transport_smoke.cpp
    unit/ai/test_cancellation_token.cpp
    unit/ai/test_juce_http_transport.cpp
    unit/ai/test_anthropic_client.cpp
    unit/ai/test_ollama_client.cpp
    unit/ai/test_conversation_engine.cpp
    unit/app/test_plugin_state.cpp
    unit/app/test_state_pill.cpp
    unit/app/test_conversation_panel.cpp
    unit/app/test_word_readout.cpp
    unit/app/test_asset_locator.cpp
    unit/app/test_ai_settings_panel.cpp
    unit/app/test_song_store.cpp
    ${CMAKE_SOURCE_DIR}/src/app/PluginState.cpp
    ${CMAKE_SOURCE_DIR}/src/app/StatePill.cpp
    ${CMAKE_SOURCE_DIR}/src/app/ConversationPanel.cpp
    ${CMAKE_SOURCE_DIR}/src/app/AiSettingsPanel.cpp
    ${CMAKE_SOURCE_DIR}/src/app/SongStore.cpp
    integration/test_passthrough_golden.cpp
    integration/test_realtime_safety.cpp
    integration/test_scene_switch.cpp
    integration/test_speaking_scene.cpp
    integration/test_apple_speaking_scene.cpp
    integration/test_scene_hot_reload.cpp
    integration/test_fallback_chain.cpp
    integration/test_carousel_scene.cpp
    integration/test_note_triggered_speech.cpp
    integration/test_vocal_guitar_clip_bank_scene.cpp
    integration/test_mic_talkbox_scene.cpp
    integration/test_auto_vocal_formant_scene.cpp
    integration/test_whisper_transcriber.cpp
    integration/test_sidechain_routing.cpp
    integration/test_realtime_safety_ai.cpp
    integration/test_au_state_persist_ai.cpp
    integration/test_ai_pedal_routing.cpp
    integration/test_headless_safety_ai.cpp
    integration/test_speak_v2_scene.cpp
    integration/test_gspeak_autoload.cpp
    integration/test_gspeak_save_then_load.cpp
    integration/test_mp3_import_flow.cpp
    integration/test_voice_pack_swap.cpp
    integration/test_sung_vowels_scene.cpp
    ${CMAKE_SOURCE_DIR}/src/app/AssetLocator.cpp
    harness/RealtimeSentinel.cpp
    harness/SyntheticGuitar.cpp
    harness/GoldenFile.cpp
)

add_dependencies(guitar_dsp_tests sung_vowels_test_bundle)

target_link_libraries(guitar_dsp_tests PRIVATE
    guitar_dsp_audio
    guitar_dsp_scenes
    guitar_dsp_midi
    guitar_dsp_ai
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_gui_basics
    Catch2::Catch2WithMain
)

target_include_directories(guitar_dsp_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

include(${CMAKE_SOURCE_DIR}/external/Catch2/extras/Catch.cmake)
catch_discover_tests(guitar_dsp_tests)
