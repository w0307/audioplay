#include "TestSupport.h"
#include "PlaybackEngine.h"
#include <sndfile.h>
#include <vector>
#include <fstream>
#include <functional>
#include <limits>
#include <cmath>
#include <nlohmann/json.hpp>

using namespace audio_engine;
namespace {
class MockOutput final : public AudioOutput {
public:
    int rate = 0, channels = 0, opens = 0, writes = 0, flushes = 0, drains = 0;
    std::size_t samples = 0;
    bool has_signal = false;
    double volume = -1;
    bool open_fail = false, write_fail = false, drain_fail = false, flush_fail = false;
    bool write_throw = false;
    std::function<void()> on_flush, on_write, on_drain;
    void ResetCounts() { writes = drains = 0; samples = 0; has_signal = false; }
    void SetVolume(double v) override { volume = v; }
    bool MatchesFormat(int r, int c) const override { return rate == r && channels == c; }
    bool Open(int r, int c, const CancelToken&, std::string& error) override {
        ++opens; if (open_fail) { error = "output open failure"; return false; } rate = r; channels = c; return true;
    }
    SpeakResult Write(const short* pcm, std::size_t n, const CancelToken& token, std::string& error) override {
        CHECK(n % static_cast<std::size_t>(channels) == 0); ++writes;
        samples += n;
        for (std::size_t i = 0; i < n; ++i) if (pcm[i]) { has_signal = true; break; }
        if (on_write) on_write();
        if (write_throw) throw std::runtime_error("output write exception");
        if (write_fail) { error = "output write failure"; return SpeakResult::Failed; }
        return token->load() ? SpeakResult::Cancelled : SpeakResult::Completed;
    }
    SpeakResult Drain(const CancelToken& token, std::string& error) override {
        ++drains; if (on_drain) on_drain();
        if (drain_fail) { error = "output drain failure"; return SpeakResult::Failed; }
        return token->load() ? SpeakResult::Cancelled : SpeakResult::Completed;
    }
    bool Flush(std::string& error) override { ++flushes; if (on_flush) on_flush(); if (flush_fail) error = "flush failed"; return !flush_fail; }
    void Close() override { rate = channels = 0; }
};
class MockSpeech final : public SpeechSynthesizer {
public:
    int calls = 0; std::string text; bool fail = false;
    bool Initialize(const EngineOptions&, std::string&) override { return true; }
    PlaybackResult Speak(const std::string& t, const CancelToken& token) override {
        ++calls; text = t; PlaybackResult r;
        r.outcome = token->load() ? PlaybackOutcome::Cancelled : fail ? PlaybackOutcome::Failed : PlaybackOutcome::Completed;
        if (fail) { r.error_stage = ErrorStage::TtsSource; r.message = "TTS failed"; }
        return r;
    }
};
void Wav(const std::string& path, int channels) {
    SF_INFO info{}; info.samplerate = 16000; info.channels = channels; info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
    SNDFILE* file = sf_open(path.c_str(), SFM_WRITE, &info); CHECK(file);
    std::vector<short> data(8000 * static_cast<std::size_t>(channels), 1000);
    CHECK(sf_writef_short(file, data.data(), 8000) == 8000); CHECK(sf_close(file) == 0);
}
void ProvidedRecordingTests() {
    const std::string root = AUDIOPLAY_RECORDINGS_ROOT;
    std::ifstream manifest((root + "/config/audio_manifest.json").c_str());
    const auto doc = nlohmann::json::parse(manifest);
    CHECK(doc.at("recordings").size() == 74);
    auto out = new MockOutput; auto speech = new MockSpeech;
    PlaybackEngine engine{std::unique_ptr<AudioOutput>(out), std::unique_ptr<SpeechSynthesizer>(speech)};
    EngineOptions options; std::string error; CHECK(engine.Initialize(options, error));
    std::size_t total_samples = 0;
    for (const auto& record : doc.at("recordings")) {
        PlaybackRequest job;
        job.file_path = root + "/audio/" + record.at("id").get<std::string>() + ".mp3";
        job.text = "文件不应回退到这条TTS。";
        out->ResetCounts();
        const auto result = engine.Play(job, std::make_shared<std::atomic<bool>>(false));
        CHECK(result.outcome == PlaybackOutcome::Completed && !result.used_tts_fallback);
        CHECK(out->has_signal && out->drains == 1 && speech->calls == 0);
        CHECK(out->rate == record.at("sample_rate").get<int>());
        CHECK(out->channels == record.at("channels").get<int>());
        const double decoded_seconds = static_cast<double>(out->samples) / out->rate / out->channels;
        CHECK(std::abs(decoded_seconds - record.at("duration_seconds").get<double>()) < 0.15);
        total_samples += out->samples;
    }
    std::cout << "All 74 provided MP3s decoded/drained without TTS; samples=" << total_samples << '\n';
}
void Mp3Tests() {
    auto out = new MockOutput; auto speech = new MockSpeech;
    PlaybackEngine engine{std::unique_ptr<AudioOutput>(out), std::unique_ptr<SpeechSynthesizer>(speech)};
    EngineOptions config; std::string error; CHECK(engine.Initialize(config, error));
    const std::string root = AUDIOPLAY_TEST_AUDIO_DIR;
    struct Fixture { const char* file; int rate, channels; std::size_t frames; };
    const Fixture files[] = {
        {"short_mono_cbr.mp3", 16000, 1, 4000},
        {"long_stereo_vbr.mp3", 44100, 2, 187425},
        {"short_stereo_cbr.mp3", 48000, 2, 28800}
    };
    PlaybackRequest job; auto token = std::make_shared<std::atomic<bool>>(false);
    for (const auto& f : files) {
        out->ResetCounts(); job.file_path = root + "/" + f.file; job.text.clear();
        const auto result = engine.Play(job, token);
        CHECK(result.outcome == PlaybackOutcome::Completed && !result.used_tts_fallback);
        CHECK(out->rate == f.rate && out->channels == f.channels);
        CHECK(out->samples == f.frames * static_cast<std::size_t>(f.channels));
        CHECK(out->has_signal && out->drains == 1 && speech->calls == 0);
    }
    // 内容决定解码格式，MP3 改扩展名后仍然能播放。
    TempDirectory temp; const auto renamed = temp.path + "/renamed.wav", bad = temp.path + "/bad.mp3";
    { std::ifstream source((root + "/short_mono_cbr.mp3").c_str(), std::ios::binary);
      std::ofstream dest(renamed.c_str(), std::ios::binary); dest << source.rdbuf(); }
    job.file_path = renamed; out->ResetCounts();
    CHECK(engine.Play(job, token).outcome == PlaybackOutcome::Completed); CHECK(out->samples == 4000);
    job.file_path = root + "/long_stereo_vbr.mp3"; job.text = "录音不可用时的备用文案。";
    out->ResetCounts(); out->on_write = [&] { if (out->writes == 2) token->store(true); };
    auto result = engine.Play(job, token);
    CHECK(result.outcome == PlaybackOutcome::Cancelled && !result.used_tts_fallback);
    CHECK(speech->calls == 0 && out->flushes == 1 && out->samples < 374850);
    out->on_write = {}; token = std::make_shared<std::atomic<bool>>(false); out->ResetCounts();
    CHECK(engine.Play(job, token).outcome == PlaybackOutcome::Completed); CHECK(out->samples == 374850);

    // 文件读完不等于扬声器播完；Drain 阶段也须支持取消且不回退 TTS。
    out->on_drain = [&] { token->store(true); };
    CHECK(engine.Play(job, token).outcome == PlaybackOutcome::Cancelled); CHECK(speech->calls == 0);
    out->on_drain = {}; token = std::make_shared<std::atomic<bool>>(false);
    out->Close(); out->open_fail = true; result = engine.Play(job, token);
    CHECK(result.error_stage == ErrorStage::Output && !result.used_tts_fallback); CHECK(speech->calls == 0); out->open_fail = false;
    out->write_fail = true; result = engine.Play(job, token);
    CHECK(result.error_stage == ErrorStage::Output && !result.used_tts_fallback); CHECK(speech->calls == 0); out->write_fail = false;
    out->drain_fail = true; result = engine.Play(job, token);
    CHECK(result.error_stage == ErrorStage::Output && !result.used_tts_fallback); CHECK(speech->calls == 0); out->drain_fail = false;
    job.file_path = temp.path + "/missing.mp3"; result = engine.Play(job, token);
    CHECK(result.outcome == PlaybackOutcome::Completed && result.used_tts_fallback); CHECK(speech->calls == 1);
    { std::ofstream f(bad.c_str(), std::ios::binary); f << "not an MP3"; }
    job.file_path = bad; result = engine.Play(job, token);
    CHECK(result.outcome == PlaybackOutcome::Completed && result.used_tts_fallback); CHECK(speech->calls == 2);
    ::unlink(renamed.c_str()); ::unlink(bad.c_str()); ::rmdir(temp.path.c_str());
}
}
namespace audio_engine {
std::unique_ptr<AudioOutput> MakeAudioOutput() { return std::unique_ptr<AudioOutput>(new MockOutput); }
std::unique_ptr<SpeechSynthesizer> MakeSpeechSynthesizer(AudioOutput&) { return std::unique_ptr<SpeechSynthesizer>(new MockSpeech); }
}
int main() {
    try {
        TempDirectory temp; const auto mono = temp.path + "/mono.wav", stereo = temp.path + "/stereo.wav", corrupt = temp.path + "/bad.wav";
        Wav(mono, 1); Wav(stereo, 2); { std::ofstream f(corrupt.c_str()); f << "invalid audio"; }
        auto out = new MockOutput; auto speech = new MockSpeech;
        PlaybackEngine engine{std::unique_ptr<AudioOutput>(out), std::unique_ptr<SpeechSynthesizer>(speech)};
        EngineOptions c; std::string error;
        PlaybackRequest empty;
        CHECK(engine.Play(empty, std::make_shared<std::atomic<bool>>(false)).error_stage == ErrorStage::Request);
        c.volume = std::numeric_limits<double>::quiet_NaN(); CHECK(!engine.Initialize(c, error));
        c.volume = 0.8; c.speed = 101; CHECK(!engine.Initialize(c, error));
        c.speed = 0; c.voice = "unsupported"; CHECK(!engine.Initialize(c, error));
        c.voice = "Mandarin"; CHECK(engine.Initialize(c, error)); CHECK(out->volume == c.volume);
        CHECK(engine.Play(empty, CancelToken()).error_stage == ErrorStage::Request);
        empty.file_path = std::string("file\0.wav", 9);
        CHECK(engine.Play(empty, std::make_shared<std::atomic<bool>>(false)).error_stage == ErrorStage::Request);
        PlaybackRequest job; job.file_path = mono; job.text = "车辆正在运行。";
        auto token = std::make_shared<std::atomic<bool>>(false);
        const auto text = job.text; job.text.clear();
        CHECK(engine.Play(job, token).outcome == PlaybackOutcome::Completed); CHECK(speech->calls == 0);
        job.text = text;
        auto r = engine.Play(job, token); CHECK(r.outcome == PlaybackOutcome::Completed); CHECK(speech->calls == 0); CHECK(out->channels == 1);
        job.file_path = stereo; r = engine.Play(job, token); CHECK(r.outcome == PlaybackOutcome::Completed); CHECK(out->channels == 2); CHECK(speech->calls == 0);
        job.file_path = temp.path + "/missing.wav"; r = engine.Play(job, token); CHECK(r.used_tts_fallback); CHECK(r.outcome == PlaybackOutcome::Completed); CHECK(speech->calls == 1);
        job.file_path = corrupt; r = engine.Play(job, token); CHECK(r.used_tts_fallback); CHECK(speech->calls == 2);
        job.file_path = temp.path; r = engine.Play(job, token); CHECK(r.used_tts_fallback); CHECK(speech->calls == 3);
        job.file_path.clear(); r = engine.Play(job, token); CHECK(r.outcome == PlaybackOutcome::Completed); CHECK(!r.used_tts_fallback); CHECK(speech->calls == 4);
        token->store(true); job.file_path = mono; r = engine.Play(job, token); CHECK(r.outcome == PlaybackOutcome::Cancelled); CHECK(speech->calls == 4);
        token = std::make_shared<std::atomic<bool>>(false);
        job.file_path = corrupt; out->on_flush = [&] { token->store(true); };
        r = engine.Play(job, token); CHECK(r.outcome == PlaybackOutcome::Cancelled); CHECK(speech->calls == 4); out->on_flush = {};
        token = std::make_shared<std::atomic<bool>>(false); job.file_path = mono;
        out->Close(); out->open_fail = true; r = engine.Play(job, token); CHECK(r.error_stage == ErrorStage::Output); CHECK(speech->calls == 4); out->open_fail = false;
        out->write_fail = true; r = engine.Play(job, token); CHECK(r.error_stage == ErrorStage::Output); CHECK(speech->calls == 4); out->write_fail = false;
        out->drain_fail = true; r = engine.Play(job, token); CHECK(r.error_stage == ErrorStage::Output); CHECK(speech->calls == 4); out->drain_fail = false;
        out->write_throw = true; r = engine.Play(job, token); CHECK(r.outcome == PlaybackOutcome::Failed && r.error_stage == ErrorStage::Output); CHECK(out->rate == 0); CHECK(speech->calls == 4); out->write_throw = false;
        job.file_path = corrupt; out->flush_fail = true; r = engine.Play(job, token); CHECK(r.error_stage == ErrorStage::Output); CHECK(speech->calls == 4); out->flush_fail = false;
        speech->fail = true; r = engine.Play(job, token); CHECK(r.outcome == PlaybackOutcome::Failed && r.error_stage == ErrorStage::TtsSource); CHECK(speech->calls == 5);
        speech->fail = false; job.text.clear(); r = engine.Play(job, token); CHECK(r.error_stage == ErrorStage::TtsSource); CHECK(speech->calls == 5);
        ::unlink(mono.c_str()); ::unlink(stereo.c_str()); ::unlink(corrupt.c_str()); ::rmdir(temp.path.c_str());
        std::cout << "WAV priority, mono/stereo, missing/corrupt source fallback, cancellation and output-error cases passed\n";
        Mp3Tests();
        std::cout << "MP3 short/long CBR/VBR/ID3/gapless/tail/drain/cancel/recovery/output-error/fallback cases passed\n";
        ProvidedRecordingTests();
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
