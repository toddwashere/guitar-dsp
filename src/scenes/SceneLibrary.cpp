#include "SceneLibrary.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <filesystem>
#include <iostream>

namespace guitar_dsp::scenes {

namespace {

std::optional<std::uint32_t> parseColor(const juce::String& s) {
    if (s.isEmpty() || s[0] != '#' || s.length() != 7) return std::nullopt;
    const auto hex = s.substring(1).getHexValue32();
    return static_cast<std::uint32_t>(hex & 0xFFFFFFu);
}

} // namespace

std::optional<Scene> SceneLibrary::loadOne(const std::string& path) {
    juce::File file(path);
    if (!file.existsAsFile()) {
        std::cerr << "[SceneLibrary] missing file: " << path << '\n';
        return std::nullopt;
    }
    const auto text = file.loadFileAsString();
    auto parsed = juce::JSON::parse(text);
    if (!parsed.isObject()) {
        std::cerr << "[SceneLibrary] not a JSON object: " << path << '\n';
        return std::nullopt;
    }

    auto* obj = parsed.getDynamicObject();
    if (obj == nullptr) {
        std::cerr << "[SceneLibrary] empty object: " << path << '\n';
        return std::nullopt;
    }

    Scene s;
    if (! obj->hasProperty("id") || ! obj->hasProperty("name")) {
        std::cerr << "[SceneLibrary] missing 'id' or 'name': " << path << '\n';
        return std::nullopt;
    }
    s.id = static_cast<int>(obj->getProperty("id"));
    s.name = obj->getProperty("name").toString().toStdString();

    if (auto colorOpt = parseColor(obj->getProperty("color").toString())) {
        s.colorRgb = *colorOpt;
    }

    if (obj->hasProperty("showChat"))
        s.showChat = static_cast<bool>(obj->getProperty("showChat"));
    if (obj->hasProperty("showVocoder"))
        s.showVocoder = static_cast<bool>(obj->getProperty("showVocoder"));
    if (obj->hasProperty("showSay"))
        s.showSay = static_cast<bool>(obj->getProperty("showSay"));
    if (obj->hasProperty("showWordReadout"))
        s.showWordReadout = static_cast<bool>(obj->getProperty("showWordReadout"));

    if (obj->hasProperty("mixer")) {
        if (auto* m = obj->getProperty("mixer").getDynamicObject()) {
            if (m->hasProperty("masterGainDb"))
                s.mixer.masterGainDb = static_cast<float>(static_cast<double>(m->getProperty("masterGainDb")));
            if (m->hasProperty("dryWet"))
                s.mixer.dryWet = static_cast<float>(static_cast<double>(m->getProperty("dryWet")));
            if (m->hasProperty("transitionMs"))
                s.mixer.transitionMs = static_cast<float>(static_cast<double>(m->getProperty("transitionMs")));
        }
    }

    if (obj->hasProperty("tts")) {
        if (auto* t = obj->getProperty("tts").getDynamicObject()) {
            if (t->hasProperty("source"))
                s.tts.source = t->getProperty("source").toString().toStdString();
            if (t->hasProperty("clip"))
                s.tts.clip = t->getProperty("clip").toString().toStdString();
            if (t->hasProperty("text"))
                s.tts.text = t->getProperty("text").toString().toStdString();
            if (t->hasProperty("voice"))
                s.tts.voice = t->getProperty("voice").toString().toStdString();
            if (t->hasProperty("fallback"))
                s.tts.fallback = t->getProperty("fallback").toString().toStdString();
            if (t->hasProperty("trigger"))
                s.tts.trigger = t->getProperty("trigger").toString().toStdString();
            if (t->hasProperty("wordSync"))
                s.tts.wordSync = t->getProperty("wordSync").toString().toStdString();
            if (t->hasProperty("clarity"))
                s.tts.clarity = std::clamp(
                    static_cast<float>((double) t->getProperty("clarity")), 0.0f, 1.0f);
            if (t->hasProperty("bank")) {
                if (auto* arr = t->getProperty("bank").getArray()) {
                    s.tts.bank.clear();
                    s.tts.bank.reserve(static_cast<std::size_t>(arr->size()));
                    for (int i = 0; i < arr->size(); ++i)
                        s.tts.bank.push_back(
                            (*arr)[i].toString().toStdString());
                }
            }
        }
    }

    if (obj->hasProperty("speech")) {
        if (auto* sp = obj->getProperty("speech").getDynamicObject()) {
            if (sp->hasProperty("player")) {
                const auto v = sp->getProperty("player").toString().toStdString();
                s.speech.player = (v == "phonemeStepped")
                    ? Scene::Speech::Player::PhonemeStepped
                    : Scene::Speech::Player::NoteStepped;
            }
            if (sp->hasProperty("maxSustainMs"))
                s.speech.maxSustainMs = static_cast<double>(
                    sp->getProperty("maxSustainMs"));
            if (sp->hasProperty("attackInterruptPolicy")) {
                const auto v = sp->getProperty("attackInterruptPolicy").toString().toStdString();
                s.speech.attackInterrupt = (v == "interrupt")
                    ? Scene::Speech::AttackInterrupt::Interrupt
                    : Scene::Speech::AttackInterrupt::Finish;
            }
        }
    }

    if (obj->hasProperty("gspeakPath"))
        s.gspeakPath = obj->getProperty("gspeakPath").toString().toStdString();
    if (obj->hasProperty("gspeakAutoLoad"))
        s.gspeakAutoLoad = (bool) obj->getProperty("gspeakAutoLoad");

    if (obj->hasProperty("carousel")) {
        if (auto* c = obj->getProperty("carousel").getDynamicObject()) {
            auto& cc = s.carousel;
            auto getF = [](juce::DynamicObject* o, const char* k, float d) {
                return o->hasProperty(k)
                     ? static_cast<float>(static_cast<double>(o->getProperty(k))) : d;
            };
            if (c->hasProperty("enabled"))
                cc.enabled = static_cast<bool>(c->getProperty("enabled"));
            cc.drive        = getF(c, "drive", cc.drive);
            cc.outputTrimDb = getF(c, "outputTrimDb", cc.outputTrimDb);

            if (auto* w = c->hasProperty("waveshaper")
                            ? c->getProperty("waveshaper").getDynamicObject() : nullptr) {
                const auto t = w->getProperty("type").toString();
                if (t == "tanh")      cc.shaper = CarouselConfig::Shaper::Tanh;
                else if (t == "hardclip") cc.shaper = CarouselConfig::Shaper::HardClip;
                else if (t == "foldback") cc.shaper = CarouselConfig::Shaper::Foldback;
                cc.shaperAmount = getF(w, "amount", cc.shaperAmount);
            }
            if (auto* cr = c->hasProperty("crusher")
                             ? c->getProperty("crusher").getDynamicObject() : nullptr) {
                if (cr->hasProperty("bits"))
                    cc.crusherBits = static_cast<int>(cr->getProperty("bits"));
                if (cr->hasProperty("downsample"))
                    cc.crusherDownsample = static_cast<int>(cr->getProperty("downsample"));
            }
            if (auto* f = c->hasProperty("filter")
                            ? c->getProperty("filter").getDynamicObject() : nullptr) {
                const auto mode = f->getProperty("mode").toString();
                if (mode == "lowpass")  cc.filterMode = CarouselConfig::FilterMode::LowPass;
                else if (mode == "bandpass") cc.filterMode = CarouselConfig::FilterMode::BandPass;
                else if (mode == "highpass") cc.filterMode = CarouselConfig::FilterMode::HighPass;
                const auto mod = f->getProperty("mod").toString();
                if (mod == "envelope") cc.filterMod = CarouselConfig::FilterMod::Envelope;
                else if (mod == "lfo") cc.filterMod = CarouselConfig::FilterMod::Lfo;
                cc.filterCutoffHz  = getF(f, "cutoffHz", cc.filterCutoffHz);
                cc.filterResonance = getF(f, "resonance", cc.filterResonance);
                cc.filterEnvAmount = getF(f, "envAmount", cc.filterEnvAmount);
                cc.filterLfoHz     = getF(f, "lfoHz", cc.filterLfoHz);
            }
            if (auto* ch = c->hasProperty("chorus")
                             ? c->getProperty("chorus").getDynamicObject() : nullptr) {
                cc.chorusRateHz = getF(ch, "rateHz", cc.chorusRateHz);
                cc.chorusDepth  = getF(ch, "depth", cc.chorusDepth);
                cc.chorusMix    = getF(ch, "mix", cc.chorusMix);
            }
            if (auto* rv = c->hasProperty("reverb")
                             ? c->getProperty("reverb").getDynamicObject() : nullptr) {
                cc.reverbRoomSize = getF(rv, "roomSize", cc.reverbRoomSize);
                cc.reverbWet      = getF(rv, "wet", cc.reverbWet);
            }
            if (auto* p = c->hasProperty("pitch")
                            ? c->getProperty("pitch").getDynamicObject() : nullptr) {
                cc.pitchSemitones = getF(p, "semitones", cc.pitchSemitones);
                cc.pitchMix       = getF(p, "mix", cc.pitchMix);
                cc.pitchGrainMs   = getF(p, "grainMs", cc.pitchGrainMs);
            }
            if (auto* h = c->hasProperty("harmonizer")
                            ? c->getProperty("harmonizer").getDynamicObject() : nullptr) {
                cc.harmMix = getF(h, "mix", cc.harmMix);
                if (auto* iv = h->getProperty("intervals").getArray()) {
                    const int n = juce::jmin((int) iv->size(),
                                             CarouselConfig::kMaxHarmVoices);
                    cc.harmVoiceCount = n;
                    for (int i = 0; i < n; ++i)
                        cc.harmSemitones[i] = static_cast<int>((*iv)[i]);
                }
                if (auto* dt = h->getProperty("detuneCents").getArray()) {
                    const int n = juce::jmin((int) dt->size(),
                                             CarouselConfig::kMaxHarmVoices);
                    for (int i = 0; i < n; ++i)
                        cc.harmDetuneCents[i] = static_cast<int>((*dt)[i]);
                }
            }
            if (auto* cb = c->hasProperty("comb")
                             ? c->getProperty("comb").getDynamicObject() : nullptr) {
                cc.combFreqHz   = getF(cb, "freqHz", cc.combFreqHz);
                cc.combFeedback = getF(cb, "feedback", cc.combFeedback);
                cc.combMix      = getF(cb, "mix", cc.combMix);
            }
            if (auto* fo = c->hasProperty("formant")
                             ? c->getProperty("formant").getDynamicObject() : nullptr) {
                const auto v = fo->getProperty("vowel").toString();
                if (v == "ah")      cc.formantVowel = CarouselConfig::Vowel::Ah;
                else if (v == "oh") cc.formantVowel = CarouselConfig::Vowel::Oh;
                else if (v == "ee") cc.formantVowel = CarouselConfig::Vowel::Ee;
                cc.formantAmount = getF(fo, "amount", cc.formantAmount);
                if (fo->hasProperty("mode")) {
                    const auto m = fo->getProperty("mode").toString();
                    if (m == "lfo")
                        cc.formantMode = CarouselConfig::FormantMode::Lfo;
                    else if (m == "envelope")
                        cc.formantMode = CarouselConfig::FormantMode::Envelope;
                    else
                        cc.formantMode = CarouselConfig::FormantMode::Static;
                }
                if (fo->hasProperty("breakpoints")) {
                    if (auto* arr = fo->getProperty("breakpoints").getArray()) {
                        cc.formantBreakpoints.clear();
                        cc.formantBreakpoints.reserve(static_cast<std::size_t>(arr->size()));
                        for (int i = 0; i < arr->size(); ++i)
                            cc.formantBreakpoints.push_back(
                                static_cast<float>((double)(*arr)[i]));
                    }
                }
                cc.formantLfoHz       = getF(fo, "lfoHz",       cc.formantLfoHz);
                cc.formantEnvAttackMs = getF(fo, "envAttackMs", cc.formantEnvAttackMs);
            }
        }
    }

    return s;
}

std::vector<Scene> SceneLibrary::loadDirectory(const std::string& directory) {
    std::vector<Scene> out;
    namespace fs = std::filesystem;
    if (! fs::exists(directory) || ! fs::is_directory(directory)) {
        std::cerr << "[SceneLibrary] not a directory: " << directory << '\n';
        return out;
    }

    for (const auto& entry : fs::directory_iterator(directory)) {
        if (! entry.is_regular_file()) continue;
        if (entry.path().extension() != ".json") continue;
        if (auto s = loadOne(entry.path().string())) out.push_back(*s);
    }

    std::sort(out.begin(), out.end(),
              [](const Scene& a, const Scene& b) { return a.id < b.id; });
    return out;
}

} // namespace guitar_dsp::scenes
