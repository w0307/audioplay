#pragma once
#include "AudioPlay.h"
#include <audio_engine/PlaybackEngine.h>
#include "VoicePolicy.h"
#include <condition_variable>
#include <mutex>
#include <thread>

namespace audioplay_detail {
class PlaybackScheduler final {
public:
    PlaybackScheduler(const RuntimeConfig&, std::unique_ptr<audio_engine::IPlaybackEngine>);
    ~PlaybackScheduler();
    AudioPlay::Result UpdateState(VoiceSnapshot snapshot);
    void Shutdown();
private:
    void ControlLoop();
    void PlaybackLoop();
    RuntimeConfig config_;
    VoicePolicy policy_;
    VoiceCatalog catalog_;
    std::unique_ptr<audio_engine::IPlaybackEngine> backend_;
    std::mutex mutex_, shutdown_mutex_;
    std::condition_variable changed_;
    std::thread control_, playback_;
    bool ready_ = false, stopping_ = false, has_state_ = false;
    bool busy_ = false, pending_ = false, has_completion_ = false;
    std::uint64_t sequence_ = 0, revision_ = 0, next_task_ = 0;
    VoiceSnapshot latest_;
    VoiceDecision decision_;
    VoiceResource resource_;
    PlaybackJob job_;
    CancelToken cancel_;
    PlaybackCompletion completion_;
    TimePoint due_ = TimePoint::max();
};
}
