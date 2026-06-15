#pragma once
#include "ai/ConversationBuffer.h"
#include "ai/PersonaRegistry.h"
#include "ai/ITranscriber.h"
#include "ai/ILlmClient.h"
#include "ai/CancellationToken.h"
#include "audio/IMicCapture.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

namespace guitar_dsp::ai {

struct StageTimings {
    std::chrono::milliseconds stt{};
    std::chrono::milliseconds llm{};
    std::chrono::milliseconds tts{};
};

class ConversationEngine {
public:
    enum class State { Idle, Capturing, Transcribing, Thinking, Speaking, Error };

    ConversationEngine(ITranscriber&,
                       std::shared_ptr<ILlmClient>,
                       audio::IMicCapture&,
                       ConversationBuffer&,
                       PersonaRegistry&,
                       std::function<void(std::string)> sayCallback);
    ~ConversationEngine();

    ConversationEngine(const ConversationEngine&) = delete;
    ConversationEngine& operator=(const ConversationEngine&) = delete;

    // Smart toggle: Idle->start, Capturing->end.
    void startTurn();
    void endTurn();
    void cancelTurn();
    void clearConversation();
    void onTtsPlaybackFinished();

    State        state()       const noexcept { return state_.load(); }
    std::string  lastError()   const;
    StageTimings lastTimings() const;

    // Swap the active LLM client. shared_ptr ownership ensures the old
    // client stays alive until any in-flight worker call returns. Safe
    // to call from any thread, in any engine state.
    void setLlmClient(std::shared_ptr<ILlmClient>);
    void setPersona(PersonaId, std::string customPrompt);

    void setCannedFallbackEnabled(bool enabled) noexcept {
        cannedFallback_.store(enabled);
    }

private:
    enum class Job { StartTurn, EndTurn, Cancel, Clear, TtsFinished };
    void enqueue(Job);
    void workerLoop();
    void runEndTurn();

    ITranscriber*       stt_;
    // Shared ownership: rebuildLlmClient on the message thread can swap
    // this while the worker thread is in runEndTurn. Reads/writes go
    // through a mutex; runEndTurn copies to a local shared_ptr before
    // calling generate() so the old object isn't yanked mid-call.
    std::shared_ptr<ILlmClient> llm_;
    mutable std::mutex          llmMutex_;
    audio::IMicCapture* mic_;
    ConversationBuffer* buf_;
    PersonaRegistry*    personas_;
    std::function<void(std::string)> say_;
    PersonaId           personaId_ {PersonaId::Interviewer};

    std::atomic<State>  state_ {State::Idle};
    mutable std::mutex  errorMutex_;
    std::string         lastError_;
    StageTimings        lastTimings_;
    CancellationToken   cancel_;
    std::atomic<bool>   cannedFallback_ {false};

    static std::string pickCannedReply(int turnCount) noexcept;

    std::thread             worker_;
    std::mutex              qMutex_;
    std::condition_variable qCv_;
    std::queue<Job>         queue_;
    std::atomic<bool>       running_ {true};
};

} // namespace guitar_dsp::ai
