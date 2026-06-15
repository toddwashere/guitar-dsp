#include "ai/ConversationEngine.h"

namespace guitar_dsp::ai {

std::string ConversationEngine::pickCannedReply(int n) noexcept {
    static const char* pool[] = {
        "Hmm, let me think.",
        "Say that again?",
        "Hard to say.",
        "I'm not sure yet."
    };
    constexpr int kPoolSize = static_cast<int>(sizeof(pool) / sizeof(pool[0]));
    return pool[n % kPoolSize];
}

ConversationEngine::ConversationEngine(ITranscriber& t, ILlmClient& l,
                                       audio::IMicCapture& m,
                                       ConversationBuffer& b, PersonaRegistry& p,
                                       std::function<void(std::string)> say)
    : stt_(&t), llm_(&l), mic_(&m), buf_(&b), personas_(&p), say_(std::move(say)) {
    worker_ = std::thread(&ConversationEngine::workerLoop, this);
}

ConversationEngine::~ConversationEngine() {
    running_.store(false);
    cancel_.cancel();
    qCv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void ConversationEngine::enqueue(Job j) {
    { std::lock_guard lk(qMutex_); queue_.push(j); }
    qCv_.notify_one();
}

void ConversationEngine::startTurn() {
    auto s = state_.load();
    if      (s == State::Idle)      enqueue(Job::StartTurn);
    else if (s == State::Capturing) enqueue(Job::EndTurn);
    // else: ignored (Transcribing/Thinking/Speaking/Error)
}
void ConversationEngine::endTurn()               { enqueue(Job::EndTurn); }
void ConversationEngine::cancelTurn()            { cancel_.cancel(); enqueue(Job::Cancel); }
void ConversationEngine::clearConversation()     { enqueue(Job::Clear); }
void ConversationEngine::onTtsPlaybackFinished() { enqueue(Job::TtsFinished); }

std::string  ConversationEngine::lastError()   const { std::lock_guard lk(errorMutex_); return lastError_; }
StageTimings ConversationEngine::lastTimings() const { std::lock_guard lk(errorMutex_); return lastTimings_; }

void ConversationEngine::setLlmClient(ILlmClient& c) {
    if (state_.load() != State::Idle) return;
    llm_ = &c;
}
void ConversationEngine::setPersona(PersonaId id, std::string custom) {
    personaId_ = id;
    if (!custom.empty()) personas_->setCustomPrompt(id, std::move(custom));
}

void ConversationEngine::workerLoop() {
    while (running_.load()) {
        Job j;
        {
            std::unique_lock lk(qMutex_);
            qCv_.wait(lk, [this]{ return !queue_.empty() || !running_.load(); });
            if (!running_.load()) break;
            j = queue_.front(); queue_.pop();
        }
        switch (j) {
            case Job::StartTurn:
                if (state_.load() == State::Idle) {
                    cancel_.reset();
                    mic_->beginCapture();
                    state_.store(State::Capturing);
                    std::fprintf(stderr, "[ConversationEngine] beginCapture()\n");
                }
                break;
            case Job::EndTurn:
                std::fprintf(stderr, "[ConversationEngine] EndTurn (state=%d)\n",
                             (int) state_.load());
                if (state_.load() == State::Capturing) runEndTurn();
                break;
            case Job::Cancel:
                if (mic_->isCapturing()) (void)mic_->endCapture();
                state_.store(State::Idle);
                break;
            case Job::Clear:
                if (mic_->isCapturing()) (void)mic_->endCapture();
                cancel_.cancel();
                buf_->clear();
                state_.store(State::Idle);
                break;
            case Job::TtsFinished:
                if (state_.load() == State::Speaking) state_.store(State::Idle);
                break;
        }
    }
}

void ConversationEngine::runEndTurn() {
    using clock = std::chrono::steady_clock;
    auto samples = mic_->endCapture();
    std::fprintf(stderr,
        "[ConversationEngine] endCapture: %zu samples (~%.2f s @16k), tooShort=%d\n",
        samples.size(), samples.size() / 16000.0,
        (int) mic_->lastResultWasTooShort());
    if (mic_->lastResultWasTooShort()) {
        { std::lock_guard lk(errorMutex_); lastError_ = "didn't hear anything"; }
        state_.store(State::Error); return;
    }

    state_.store(State::Transcribing);
    auto t0 = clock::now();
    std::fprintf(stderr, "[ConversationEngine] transcribe(%zu samples)...\n", samples.size());
    auto stt = stt_->transcribe(samples, &cancel_);
    auto t1 = clock::now();
    std::fprintf(stderr,
        "[ConversationEngine] transcribe result: text=\"%s\" error=\"%s\"\n",
        stt.text.c_str(), stt.error.c_str());
    if (cancel_.isCancelled()) { state_.store(State::Idle); return; }
    if (!stt.error.empty() || stt.text.empty()) {
        { std::lock_guard lk(errorMutex_); lastError_ = stt.error.empty() ? "couldn't transcribe" : stt.error; }
        state_.store(State::Error); return;
    }
    buf_->append(Message::Role::User, stt.text);

    state_.store(State::Thinking);
    LlmRequest req;
    req.messages = personas_->buildMessages(*buf_, personaId_);
    auto reply = llm_->generate(req, &cancel_);
    auto t2 = clock::now();
    if (cancel_.isCancelled()) { state_.store(State::Idle); return; }
    if (!reply.error.empty()) {
        { std::lock_guard lk(errorMutex_); lastError_ = reply.error; }
        if (cannedFallback_.load()) {
            const auto canned = pickCannedReply(static_cast<int>(buf_->snapshot().size()));
            buf_->append(Message::Role::Assistant, canned);
            state_.store(State::Speaking);
            say_(canned);
            return;
        }
        state_.store(State::Error); return;
    }
    buf_->append(Message::Role::Assistant, reply.text);

    state_.store(State::Speaking);
    auto t3 = clock::now();
    say_(reply.text);
    {
        std::lock_guard lk(errorMutex_);
        lastTimings_.stt = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0);
        lastTimings_.llm = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1);
        lastTimings_.tts = std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2);
        lastError_.clear();
    }
}

} // namespace guitar_dsp::ai
