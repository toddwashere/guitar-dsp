#include "GspeakBundle.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cstdint>
#include <iostream>
#include <memory>

namespace guitar_dsp::audio {

namespace {

juce::var phonemeTypeToString(Phoneme::Type t) {
    switch (t) {
        case Phoneme::Type::Vowel:     return juce::var("Vowel");
        case Phoneme::Type::Consonant: return juce::var("Consonant");
        case Phoneme::Type::Silence:   return juce::var("Silence");
    }
    return juce::var("Consonant");
}

juce::var buildManifest(const TTSClip& clip, const std::string& text,
                        bool isV2) {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("version",       1);
    obj->setProperty("kind",          "clip");
    obj->setProperty("savedBy",       "guitar-dsp gspeak/1");
    obj->setProperty("text",          juce::String(text));
    obj->setProperty("sampleRate",    clip.sampleRate);
    obj->setProperty("lengthSamples", (juce::int64) clip.samples.size());
    obj->setProperty("clipKind",      isV2 ? "v2" : "v1");

    if (isV2) {
        juce::Array<juce::var> syllables;
        for (const auto& s : clip.sylsV2) {
            auto* so = new juce::DynamicObject();
            so->setProperty("startSample",         (juce::int64) s.startSample);
            so->setProperty("endSample",           (juce::int64) s.endSample);
            so->setProperty("vowelNucleusSample",  (juce::int64) s.vowelNucleusSample);
            so->setProperty("attackEndSample",     (juce::int64) s.attackEndSample);
            so->setProperty("codaStartSample",     (juce::int64) s.codaStartSample);
            so->setProperty("nucleusIsFricative",  s.nucleusIsFricative);
            juce::Array<juce::var> idxs;
            for (int i : s.phonemeIndices) idxs.add(i);
            so->setProperty("phonemeIndices", idxs);
            syllables.add(juce::var(so));
        }
        obj->setProperty("syllables", syllables);

        juce::Array<juce::var> phonemes;
        for (const auto& p : clip.phonemes) {
            auto* po = new juce::DynamicObject();
            po->setProperty("label",       juce::String(p.label));
            po->setProperty("type",        phonemeTypeToString(p.type));
            po->setProperty("startSample", (juce::int64) p.startSample);
            po->setProperty("endSample",   (juce::int64) p.endSample);
            phonemes.add(juce::var(po));
        }
        obj->setProperty("phonemes", phonemes);
    } else {
        juce::Array<juce::var> wordsV1;
        for (const auto& w : clip.words) {
            auto* wo = new juce::DynamicObject();
            wo->setProperty("word",        juce::String(w.word));
            wo->setProperty("startSample", (juce::int64) w.startSample);
            wo->setProperty("endSample",   (juce::int64) w.endSample);
            wordsV1.add(juce::var(wo));
        }
        obj->setProperty("wordsV1", wordsV1);

        juce::Array<juce::var> syllablesV1;
        for (const auto& w : clip.syllables) {
            auto* so = new juce::DynamicObject();
            so->setProperty("word",        juce::String(w.word));
            so->setProperty("startSample", (juce::int64) w.startSample);
            so->setProperty("endSample",   (juce::int64) w.endSample);
            syllablesV1.add(juce::var(so));
        }
        obj->setProperty("syllablesV1", syllablesV1);
    }

    return juce::var(obj);
}

juce::MemoryBlock writeWavMono16(const TTSClip& clip) {
    juce::MemoryBlock out;
    {
        // The WavAudioFormat writer takes ownership of the stream it
        // writes to. Wrap MemoryOutputStream in a unique_ptr that the
        // writer can own.
        auto stream = std::make_unique<juce::MemoryOutputStream>(out, false);
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(stream.get(),
                                   clip.sampleRate,
                                   1,         // mono
                                   16,        // 16-bit PCM
                                   {},
                                   0));
        if (!writer) return {};
        stream.release();  // writer now owns it
        juce::AudioBuffer<float> buf(1, (int) clip.samples.size());
        std::copy(clip.samples.begin(), clip.samples.end(),
                  buf.getWritePointer(0));
        writer->writeFromAudioSampleBuffer(buf, 0, (int) clip.samples.size());
    }
    return out;
}

} // namespace

bool GspeakBundle::write(const juce::File& outFile,
                         const TTSClip& clip,
                         const std::string& text) {
    if (clip.samples.empty()) {
        std::cerr << "[GspeakBundle] cannot write empty clip\n";
        return false;
    }
    const bool isV2 = !clip.sylsV2.empty();

    const auto manifest     = buildManifest(clip, text, isV2);
    const auto manifestText = juce::JSON::toString(manifest, true);
    juce::MemoryBlock manifestBytes(manifestText.toRawUTF8(),
                                    manifestText.getNumBytesAsUTF8());

    auto wavBytes = writeWavMono16(clip);
    if (wavBytes.getSize() == 0) {
        std::cerr << "[GspeakBundle] failed to encode audio\n";
        return false;
    }

    juce::ZipFile::Builder builder;
    builder.addEntry(new juce::MemoryInputStream(manifestBytes, false),
                     9, "manifest.json", juce::Time::getCurrentTime());
    builder.addEntry(new juce::MemoryInputStream(wavBytes, false),
                     0, "audio.wav", juce::Time::getCurrentTime());

    outFile.deleteFile();
    auto outStream = outFile.createOutputStream();
    if (outStream == nullptr) {
        std::cerr << "[GspeakBundle] cannot open output: "
                  << outFile.getFullPathName() << '\n';
        return false;
    }
    if (!builder.writeToStream(*outStream, nullptr)) {
        std::cerr << "[GspeakBundle] zip write failed\n";
        return false;
    }
    return true;
}

std::optional<GspeakBundle::Loaded>
GspeakBundle::read(const juce::File&, double) {
    return std::nullopt;  // implemented in Task 3
}

} // namespace guitar_dsp::audio
