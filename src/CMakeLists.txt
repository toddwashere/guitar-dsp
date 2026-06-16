# Audio module library — pure C++, no JUCE dependency.
# Modules added here as they are implemented.
add_library(guitar_dsp_audio STATIC
    audio/InputStage.cpp
    audio/Mixer.cpp
    audio/AudioGraph.cpp
    audio/ChannelVocoder.cpp
    audio/TTSClipPlayer.cpp
    audio/PrebakedTTSSource.cpp
    audio/AppleTTSSource.mm
    audio/PiperTTSSource.cpp
    audio/TTSPrewarmer.cpp
    audio/TTSSynthChain.cpp
    audio/Crusher.cpp
    audio/CarouselMod.cpp
    audio/ClipBankPlayer.cpp
    audio/Carousel.cpp
    audio/PitchShifter.cpp
    audio/PitchTrackedCarrier.cpp
    audio/Harmonizer.cpp
    audio/Comb.cpp
    audio/Formant.cpp
    audio/FormantModulator.cpp
    audio/OnsetDetector.cpp
    audio/WordAligner.cpp
    audio/NoteSteppedTTSPlayer.cpp
    audio/MicCapture.cpp
    audio/MicShaper.cpp
    audio/Phoneme.cpp
    audio/PhonemeExtractor.cpp
    audio/Syllabifier.cpp
    audio/PhonemeAlignedClipBuilder.cpp
    audio/VowelGrainLoop.cpp
)

target_include_directories(guitar_dsp_audio PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(guitar_dsp_audio PUBLIC cxx_std_20)

target_link_libraries(guitar_dsp_audio
    PUBLIC
        juce::juce_audio_formats
        juce::juce_dsp
    PRIVATE
        "-framework AVFoundation"
        "-framework Foundation"
)

add_library(guitar_dsp_scenes STATIC
    scenes/SceneLibrary.cpp
    scenes/SceneEngine.cpp
)

target_include_directories(guitar_dsp_scenes PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(guitar_dsp_scenes PUBLIC cxx_std_20)

target_link_libraries(guitar_dsp_scenes PUBLIC juce::juce_core)

# AI module — STT, LLM clients, conversation orchestration. Pure C++ + JUCE.
add_library(guitar_dsp_ai STATIC
    ai/ConversationBuffer.cpp
    ai/PersonaRegistry.cpp
    ai/AppPreferences.cpp
    ai/JuceHttpTransport.cpp
    ai/AnthropicClient.cpp
    ai/OllamaClient.cpp
    ai/WhisperTranscriber.cpp
    ai/ConversationEngine.cpp
)

target_include_directories(guitar_dsp_ai PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_features(guitar_dsp_ai PUBLIC cxx_std_20)
target_link_libraries(guitar_dsp_ai
    PUBLIC
        juce::juce_core
        juce::juce_events
    PRIVATE
        whisper
)

add_library(guitar_dsp_midi STATIC
    midi/FCB1010Mapping.cpp
    midi/HostMidiSceneRouter.cpp
    midi/MidiRouter.cpp
)

target_include_directories(guitar_dsp_midi PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(guitar_dsp_midi PUBLIC cxx_std_20)

target_link_libraries(guitar_dsp_midi
    PUBLIC
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_core
        juce::juce_events
)

# Standalone application (declared in Task 5)
# The app target is added by app/CMakeLists.txt when it exists.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/app/CMakeLists.txt")
    add_subdirectory(app)
endif()
