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
            if (! clip.bankKey.empty()) {
                po->setProperty("bankKey",       juce::String(clip.bankKey));
                po->setProperty("anchorPitchHz", clip.anchorPitchHz);
                po->setProperty("variantTag",    juce::String(clip.variantTag));
            }
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

Phoneme::Type phonemeTypeFromString(const juce::String& s) {
    if (s == "Vowel")   return Phoneme::Type::Vowel;
    if (s == "Silence") return Phoneme::Type::Silence;
    return Phoneme::Type::Consonant;
}

// Linear resample (matches PrebakedTTSSource.cpp's existing routine).
// Used only when the file's sampleRate differs from the engine's.
std::vector<float> resampleLinear(const std::vector<float>& in,
                                  double fromRate, double toRate) {
    if (std::abs(fromRate - toRate) < 0.5) return in;
    const double ratio = fromRate / toRate;
    const int outLen = (int)((double) in.size() / ratio);
    std::vector<float> out((std::size_t) outLen);
    for (int i = 0; i < outLen; ++i) {
        const double srcIdx = i * ratio;
        const int    i0     = (int) srcIdx;
        const float  frac   = (float)(srcIdx - i0);
        const int    i1     = std::min<int>(i0 + 1, (int) in.size() - 1);
        out[(std::size_t) i] = (1.0f - frac) * in[(std::size_t) i0]
                             + frac          * in[(std::size_t) i1];
    }
    return out;
}

std::size_t rescaleIndex(std::size_t i, double scale) {
    return (std::size_t) std::llround((double) i * scale);
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
GspeakBundle::read(const juce::File& inFile, double engineSampleRate) {
    if (!inFile.existsAsFile()) {
        std::cerr << "[GspeakBundle] file missing: "
                  << inFile.getFullPathName() << '\n';
        return std::nullopt;
    }

    juce::ZipFile zip(inFile);
    const auto* manifestEntry = zip.getEntry("manifest.json");
    const auto* audioEntry    = zip.getEntry("audio.wav");
    if (!manifestEntry || !audioEntry) {
        std::cerr << "[GspeakBundle] missing manifest.json or audio.wav\n";
        return std::nullopt;
    }

    // Parse manifest.
    juce::var manifest;
    {
        auto stream = std::unique_ptr<juce::InputStream>(
            zip.createStreamForEntry(*manifestEntry));
        if (!stream) return std::nullopt;
        manifest = juce::JSON::parse(stream->readEntireStreamAsString());
    }
    auto* mo = manifest.getDynamicObject();
    if (!mo) {
        std::cerr << "[GspeakBundle] manifest is not an object\n";
        return std::nullopt;
    }
    if ((int) mo->getProperty("version") != 1) {
        std::cerr << "[GspeakBundle] unsupported version\n";
        return std::nullopt;
    }
    if (mo->getProperty("kind").toString() != "clip") {
        std::cerr << "[GspeakBundle] unsupported kind\n";
        return std::nullopt;
    }
    const auto clipKind = mo->getProperty("clipKind").toString();
    if (clipKind != "v1" && clipKind != "v2") {
        std::cerr << "[GspeakBundle] unsupported clipKind\n";
        return std::nullopt;
    }
    const bool   isV2        = (clipKind == "v2");
    const double fileRate    = (double) mo->getProperty("sampleRate");
    const auto   declaredLen = (juce::int64) mo->getProperty("lengthSamples");
    const auto   text        = mo->getProperty("text").toString().toStdString();

    // Decode audio.
    std::vector<float> samples;
    double decodedRate = fileRate;
    {
        auto stream = std::unique_ptr<juce::InputStream>(
            zip.createStreamForEntry(*audioEntry));
        if (!stream) return std::nullopt;
        juce::WavAudioFormat fmt;
        auto reader = std::unique_ptr<juce::AudioFormatReader>(
            fmt.createReaderFor(stream.release(), true));
        if (!reader) {
            std::cerr << "[GspeakBundle] cannot decode audio.wav\n";
            return std::nullopt;
        }
        const int len = (int) reader->lengthInSamples;
        decodedRate   = reader->sampleRate;
        juce::AudioBuffer<float> buf(1, len);
        reader->read(&buf, 0, len, 0, true, false);
        samples.assign(buf.getReadPointer(0), buf.getReadPointer(0) + len);
    }

    if ((juce::int64) samples.size() != declaredLen) {
        std::cerr << "[GspeakBundle] length mismatch: declared "
                  << declaredLen << ", actual " << samples.size() << '\n';
        return std::nullopt;
    }

    // Resample if engine rate differs from file rate; compute scale for
    // boundary indices.
    const double scale = engineSampleRate / decodedRate;
    if (std::abs(scale - 1.0) > 1e-6)
        samples = resampleLinear(samples, decodedRate, engineSampleRate);

    auto clip = std::make_shared<TTSClip>();
    clip->name       = inFile.getFileNameWithoutExtension().toStdString();
    clip->sampleRate = engineSampleRate;
    clip->samples    = std::move(samples);
    const std::size_t finalLen = clip->samples.size();

    auto clampIdx = [&](std::size_t i) {
        return std::min(rescaleIndex(i, scale), finalLen);
    };

    if (isV2) {
        const auto* syllables = mo->getProperty("syllables").getArray();
        const auto* phonemes  = mo->getProperty("phonemes").getArray();
        if (!syllables || !phonemes) {
            std::cerr << "[GspeakBundle] v2: missing syllables/phonemes\n";
            return std::nullopt;
        }
        for (int i = 0; i < phonemes->size(); ++i) {
            auto* po = (*phonemes)[i].getDynamicObject();
            if (!po) return std::nullopt;
            Phoneme p;
            p.label       = po->getProperty("label").toString().toStdString();
            p.type        = phonemeTypeFromString(po->getProperty("type").toString());
            p.startSample = clampIdx((std::size_t)(juce::int64) po->getProperty("startSample"));
            p.endSample   = clampIdx((std::size_t)(juce::int64) po->getProperty("endSample"));
            // Optional grain-metadata fields (back-compat: missing → defaults).
            // Read per-phoneme bankKey + anchorPitchHz into the Phoneme struct
            // so splitMasterClipIntoBank_ can round-trip them without heuristics.
            // Also populate clip-level fields from phoneme 0 for back-compat with
            // single-grain bundles that read clip->bankKey / clip->anchorPitchHz.
            if (po->hasProperty("anchorPitchHz")) {
                p.anchorPitchHz = (float)(double) po->getProperty("anchorPitchHz");
                if (i == 0) clip->anchorPitchHz = p.anchorPitchHz;
            }
            if (po->hasProperty("bankKey")) {
                p.bankKey = po->getProperty("bankKey").toString().toStdString();
                if (i == 0) clip->bankKey = p.bankKey;
            }
            if (po->hasProperty("variantTag") && i == 0) {
                clip->variantTag = po->getProperty("variantTag").toString().toStdString();
            }
            clip->phonemes.push_back(p);
        }
        std::size_t prevEnd = 0;
        for (int i = 0; i < syllables->size(); ++i) {
            auto* so = (*syllables)[i].getDynamicObject();
            if (!so) return std::nullopt;
            SyllableSpan s;
            s.startSample        = clampIdx((std::size_t)(juce::int64) so->getProperty("startSample"));
            s.endSample          = clampIdx((std::size_t)(juce::int64) so->getProperty("endSample"));
            s.vowelNucleusSample = clampIdx((std::size_t)(juce::int64) so->getProperty("vowelNucleusSample"));
            s.attackEndSample    = clampIdx((std::size_t)(juce::int64) so->getProperty("attackEndSample"));
            s.codaStartSample    = clampIdx((std::size_t)(juce::int64) so->getProperty("codaStartSample"));
            s.nucleusIsFricative = (bool) so->getProperty("nucleusIsFricative");
            if (auto* idxs = so->getProperty("phonemeIndices").getArray())
                for (int j = 0; j < idxs->size(); ++j)
                    s.phonemeIndices.push_back((int) (*idxs)[j]);
            if (s.startSample < prevEnd || s.endSample <= s.startSample) {
                std::cerr << "[GspeakBundle] v2: syllables out of order or empty\n";
                return std::nullopt;
            }
            prevEnd = s.endSample;
            clip->sylsV2.push_back(s);
        }
        if (clip->sylsV2.empty()) {
            std::cerr << "[GspeakBundle] v2: no syllables\n";
            return std::nullopt;
        }
        // Absorb any resample-rounding by clamping last syllable's endSample.
        clip->sylsV2.back().endSample = clip->samples.size();
    } else {
        // v1 path: read wordsV1 + syllablesV1 into clip->words / clip->syllables.
        auto readSegs = [&](const juce::Array<juce::var>* arr,
                            std::vector<WordSegment>& out) {
            if (!arr) return true;
            std::size_t prevEnd2 = 0;
            for (int i = 0; i < arr->size(); ++i) {
                auto* so = (*arr)[i].getDynamicObject();
                if (!so) return false;
                WordSegment w;
                w.word        = so->getProperty("word").toString().toStdString();
                w.startSample = clampIdx((std::size_t)(juce::int64) so->getProperty("startSample"));
                w.endSample   = clampIdx((std::size_t)(juce::int64) so->getProperty("endSample"));
                if (w.startSample < prevEnd2 || w.endSample <= w.startSample) return false;
                prevEnd2 = w.endSample;
                out.push_back(w);
            }
            return true;
        };
        if (!readSegs(mo->getProperty("wordsV1").getArray(),     clip->words) ||
            !readSegs(mo->getProperty("syllablesV1").getArray(), clip->syllables)) {
            std::cerr << "[GspeakBundle] v1: bad word/syllable arrays\n";
            return std::nullopt;
        }
        if (clip->syllables.empty() && clip->words.empty()) {
            std::cerr << "[GspeakBundle] v1: empty word and syllable arrays\n";
            return std::nullopt;
        }
        if (!clip->syllables.empty())
            clip->syllables.back().endSample = clip->samples.size();
        if (!clip->words.empty())
            clip->words.back().endSample = clip->samples.size();
    }

    Loaded result;
    result.clip = clip;
    result.text = text;
    result.isV2 = isV2;
    return result;
}

} // namespace guitar_dsp::audio
