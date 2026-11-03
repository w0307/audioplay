#include "TestSupport.h"
#include "PlaybackEngine.h"
#include <sndfile.h>
#include <vector>

using namespace audio_engine;
namespace {
class MockOutput final : public AudioOutput {
public:
    int rate = 0, channels = 0, writes = 0, flushes = 0, cancel_at = 0;
    std::size_t samples = 0;
    bool cancel_drain = false;
    void Reset() { writes = flushes = cancel_at = 0; samples = 0; cancel_drain = false; }
    void SetVolume(double) override {}
    bool MatchesFormat(int r, int c) const override { return r == rate && c == channels; }
    bool Open(int r, int c, const CancelToken&, std::string&) override { rate = r; channels = c; return true; }
    SpeakResult Write(const short*, std::size_t count, const CancelToken& token, std::string&) override {
        ++writes; samples += count;
        if (cancel_at && writes >= cancel_at) token->store(true);
        return token->load() ? SpeakResult::Cancelled : SpeakResult::Completed;
    }
    SpeakResult Drain(const CancelToken& token, std::string&) override { if (cancel_drain) token->store(true); return token->load() ? SpeakResult::Cancelled : SpeakResult::Completed; }
    bool Flush(std::string&) override { ++flushes; return true; }
    void Close() override { rate = channels = 0; }
};
}
namespace audio_engine { std::unique_ptr<AudioOutput> MakeAudioOutput() { return std::unique_ptr<AudioOutput>(new MockOutput); } }
int main() {
    try {
        auto raw = new MockOutput; std::unique_ptr<AudioOutput> output(raw);
        auto speech = MakeSpeechSynthesizer(*output);
        PlaybackEngine engine(std::move(output), std::move(speech));
        EngineOptions c; std::string error; CHECK(engine.Initialize(c, error));
        PlaybackRequest job; job.text = "车辆正在运行，请注意安全。";
        const auto play = [&] { return engine.Play(job, std::make_shared<std::atomic<bool>>(false)); };
        CHECK(play().outcome == PlaybackOutcome::Completed); const auto baseline = raw->samples; CHECK(baseline > 0);
        for (int i = 0; i < 3; ++i) { raw->Reset(); CHECK(play().outcome == PlaybackOutcome::Completed); CHECK(raw->samples == baseline); }
        for (int i = 0; i < 12; ++i) {
            raw->Reset(); raw->cancel_at = 1 + i % 3; CHECK(play().outcome == PlaybackOutcome::Cancelled); CHECK(raw->flushes == 1);
            raw->Reset(); CHECK(play().outcome == PlaybackOutcome::Completed); CHECK(raw->samples == baseline);
        }
        raw->Reset(); raw->cancel_drain = true; CHECK(play().outcome == PlaybackOutcome::Cancelled); CHECK(raw->flushes == 1);
        raw->Reset(); CHECK(engine.Play(job, std::make_shared<std::atomic<bool>>(true)).outcome == PlaybackOutcome::Cancelled); CHECK(raw->writes == 0);
        TempDirectory temp; const auto wav = temp.path + "/stereo.wav";
        SF_INFO info{}; info.samplerate = 22050; info.channels = 2; info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
        SNDFILE* f = sf_open(wav.c_str(), SFM_WRITE, &info); CHECK(f);
        std::vector<short> data(2000, 1000); CHECK(sf_writef_short(f, data.data(), 1000) == 1000); CHECK(sf_close(f) == 0);
        raw->Reset(); job.file_path = wav; CHECK(play().outcome == PlaybackOutcome::Completed); CHECK(raw->channels == 2 && raw->samples == 2000);
        raw->Reset(); job.file_path.clear(); CHECK(play().outcome == PlaybackOutcome::Completed); CHECK(raw->channels == 1 && raw->samples == baseline);
        raw->Reset(); job.file_path = temp.path + "/missing.wav"; const auto fallback = play(); CHECK(fallback.outcome == PlaybackOutcome::Completed && fallback.used_tts_fallback); CHECK(raw->samples == baseline);
        raw->Reset(); job.file_path = std::string(AUDIOPLAY_TEST_AUDIO_DIR) + "/long_stereo_vbr.mp3";
        const auto mp3 = play(); CHECK(mp3.outcome == PlaybackOutcome::Completed && !mp3.used_tts_fallback);
        CHECK(raw->channels == 2 && raw->rate == 44100 && raw->samples == 374850);
        raw->Reset(); raw->cancel_at = 2; CHECK(play().outcome == PlaybackOutcome::Cancelled); CHECK(raw->flushes == 1);
        raw->Reset(); job.file_path = temp.path + "/missing.mp3"; const auto mp3_fallback = play();
        CHECK(mp3_fallback.outcome == PlaybackOutcome::Completed && mp3_fallback.used_tts_fallback);
        CHECK(raw->channels == 1 && raw->samples == baseline);
        ::unlink(wav.c_str()); ::rmdir(temp.path.c_str());
        std::cout << "Real Ekho passed: repeat baseline=" << baseline << " samples, 12 cancellations/recovery, drain/before-start cancellation, WAV/MP3 to TTS, MP3 cancellation and missing-file fallback\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
