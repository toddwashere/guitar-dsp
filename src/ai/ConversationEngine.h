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
                       ILlmClient&,
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

    // Idle-only; ignored when busy.
    void setLlmClient(ILlmClient&);
    void setPersona(PersonaId, std::string customPrompt);

private:
    enum class Job { StartTurn, EndTurn, Cancel, Clear, TtsFinished };
    void enqueue(Job);
    void workerLoop();
    void runEndTurn();

    ITranscriber*       stt_;
    ILlmClient*         llm_;
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

    std::thread             worker_;
    std::mutex              qMutex_;
    std::condition_variable qCv_;
    std::queue<Job>         queue_;
    std::atomic<bool>       running_ {true};
};

} // namespace guitar_dsp::ai
