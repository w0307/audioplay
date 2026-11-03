#include "AudioPlay.h"
#include "PlaybackScheduler.h"
#include "StateParser.h"

class AudioPlay::Impl {
public:
    Impl() {
        audioplay_detail::RuntimeConfig config;
        std::string error;
        const auto path = audioplay_detail::ResolveConfigPath();
        if (path.empty() || !audioplay_detail::LoadJsonConfig(path, config, error)) {
            audioplay_detail::Log("JSON initialization failed: " + error + " [" + path + "]"); return;
        }
        disabled = !config.enabled;
        if (!disabled) scheduler.reset(new audioplay_detail::PlaybackScheduler(config, audio_engine::CreatePlaybackEngine()));
    }
    bool disabled = false;
    audioplay_detail::StateParser parser;
    std::unique_ptr<audioplay_detail::PlaybackScheduler> scheduler;
};
AudioPlay& AudioPlay::GetInstance() {
    // 日志对象先构造、后销毁，保证单例析构回收工作线程时日志仍有效。
    audioplay_detail::InitializeLogging();
    static AudioPlay instance;
    return instance;
}
AudioPlay::AudioPlay() {
    try { impl_.reset(new Impl); }
    catch (const std::exception& e) { audioplay_detail::Log(std::string("Initialization failed: ") + e.what()); }
}
AudioPlay::~AudioPlay() = default;
AudioPlay::Result AudioPlay::Play(const AudioState& state) {
    if (!impl_) return Result::NotReady;
    if (impl_->disabled) return Result::Disabled;
    if (!impl_->scheduler) return Result::NotReady;
    return impl_->scheduler->UpdateState(impl_->parser.Parse(state));
}
