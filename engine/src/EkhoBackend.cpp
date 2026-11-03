#include "SpeechSynthesizer.h"
#include "ekho.h"
#include <atomic>
#include <condition_variable>
#include <mutex>

extern "C" int audioplay_ekho89_patch_level();
namespace audio_engine {
namespace {
class EkhoBackend final : public SpeechSynthesizer {
public:
    explicit EkhoBackend(AudioOutput& output) : output_(output) {}
    ~EkhoBackend() override {
        engine_.reset();
        EkhoBackend* expected = this;
        active_.compare_exchange_strong(expected, nullptr);
    }
    bool Initialize(const EngineOptions& config, std::string& error) override {
        if (audioplay_ekho89_patch_level() != 2) { error = "Ekho v8.9 AudioPlay patch level 2 required"; return false; }
        speed_ = config.speed;
        return true; // 首次需要 TTS 时加载语音数据，文件播放不依赖语音包可用性。
    }
    PlaybackResult Speak(const std::string& text, const CancelToken& cancel) override {
        PlaybackResult failure;
        failure.error_stage = ErrorStage::TtsSource;
        if (cancel->load()) { failure.outcome = PlaybackOutcome::Cancelled; return failure; }
        if (!EnsureEngine(failure.message)) return failure;
        if (cancel->load()) { failure.outcome = PlaybackOutcome::Cancelled; return failure; }
        if (!output_.MatchesFormat(sample_rate_, 1) && !output_.Open(sample_rate_, 1, cancel, failure.message)) {
            failure.error_stage = ErrorStage::Output;
            if (cancel->load()) failure.outcome = PlaybackOutcome::Cancelled;
            return failure;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            finished_ = false; had_pcm_ = false; discarded_ = false;
            result_ = PlaybackResult(); result_.outcome = PlaybackOutcome::Completed;
            cancel_ = cancel;
        }
        engine_->setSpeed(speed_);
        const int rc = registered_ ? engine_->speak(text) : engine_->synth(text.c_str(), &OnPcm);
        if (rc != 0) { failure.message = "Ekho rejected text: " + std::to_string(rc); return failure; }
        registered_ = true;
        std::unique_lock<std::mutex> lock(mutex_);
        // 补丁在合成线程清理缓存后通知，包括取消路径。不能仅凭 stop() 返回复用上下文。
        completed_.wait(lock, [this] { return finished_; });
        if (cancel->load()) result_.outcome = PlaybackOutcome::Cancelled;
        cancel_.reset();
        return result_;
    }
private:
    bool EnsureEngine(std::string& error) {
        if (engine_) return true;
        EkhoBackend* expected = nullptr;
        if (!active_.compare_exchange_strong(expected, this) && expected != this) {
            error = "Another Ekho TTS instance exists"; return false;
        }
        std::unique_ptr<ekho::Ekho> engine(new ekho::Ekho);
        engine->setChannels(1);
        if (engine->setVoice("pinyin") != 0 || engine->getSampleRate() <= 0) {
            error = "Cannot load Mandarin data; check EKHO_DATA_PATH"; return false;
        }
        engine->disableSsml(); engine->setEnglishSpeed(0); engine->setPitch(0); engine->setRate(0);
        engine->setSpeed(speed_); engine->setVolume(0);
        sample_rate_ = engine->getSampleRate(); engine_ = std::move(engine);
        return true;
    }
    void Discard() {
        if (discarded_) return;
        discarded_ = true;
        std::string error;
        if (!output_.Flush(error) && result_.outcome != PlaybackOutcome::Cancelled) {
            result_.message = error; result_.error_stage = ErrorStage::Output; result_.outcome = PlaybackOutcome::Failed;
        }
    }
    static int OnPcm(short* pcm, int samples, int bits, int channels, int rate, int last) {
        EkhoBackend* self = active_.load();
        if (!self) return 1;
        std::lock_guard<std::mutex> lock(self->mutex_);
        try {
            if (self->cancel_ && self->cancel_->load()) self->result_.outcome = PlaybackOutcome::Cancelled;
            if (samples > 0 && self->result_.outcome == PlaybackOutcome::Completed) {
                if (!pcm || bits != 16 || channels != 1 || rate != self->sample_rate_) {
                    self->result_.message = "Unexpected PCM format from Ekho";
                    self->result_.error_stage = ErrorStage::TtsSource;
                    self->result_.outcome = PlaybackOutcome::Failed;
                } else {
                    self->had_pcm_ = true;
                    self->result_.outcome = self->output_.Write(pcm, static_cast<std::size_t>(samples), self->cancel_, self->result_.message);
                    if (self->result_.outcome == PlaybackOutcome::Failed) self->result_.error_stage = ErrorStage::Output;
                }
            }
            if (last == 1 && self->result_.outcome == PlaybackOutcome::Completed) {
                if (!self->had_pcm_) {
                    self->result_.message = "Ekho produced no PCM";
                    self->result_.error_stage = ErrorStage::TtsSource; self->result_.outcome = PlaybackOutcome::Failed;
                } else {
                    self->result_.outcome = self->output_.Drain(self->cancel_, self->result_.message);
                    if (self->result_.outcome == PlaybackOutcome::Failed) self->result_.error_stage = ErrorStage::Output;
                }
            }
            if (self->result_.outcome != PlaybackOutcome::Completed) self->Discard();
        } catch (...) {
            self->result_.message = "Exception handling Ekho PCM";
            self->result_.error_stage = ErrorStage::Output;
            self->result_.outcome = PlaybackOutcome::Failed;
            self->output_.Close();
        }
        if (last == 1) { self->finished_ = true; self->completed_.notify_one(); }
        return self->result_.outcome == PlaybackOutcome::Completed ? 0 : 1;
    }
    static std::atomic<EkhoBackend*> active_;
    AudioOutput& output_;
    std::unique_ptr<ekho::Ekho> engine_;
    int sample_rate_ = 0, speed_ = 0;
    bool registered_ = false, finished_ = false, had_pcm_ = false, discarded_ = false;
    std::mutex mutex_;
    std::condition_variable completed_;
    CancelToken cancel_;
    PlaybackResult result_;
};
std::atomic<EkhoBackend*> EkhoBackend::active_(nullptr);
}
std::unique_ptr<SpeechSynthesizer> MakeSpeechSynthesizer(AudioOutput& output) {
    return std::unique_ptr<SpeechSynthesizer>(new EkhoBackend(output));
}
}
