#pragma once
#include "Config.h"
namespace audioplay_detail {
class VoicePolicy final {
public:
    explicit VoicePolicy(const RuntimeConfig& config) : config_(config) {}
    void ObserveMode(OperatingMode mode);
    void OnPlaybackCompleted(const SceneKey& scene);
    VoiceDecision Evaluate(const VoiceSnapshot& state, TimePoint now);
    TimePoint NextDeadline() const { return deadline_; }
private:
    VoiceDecision Make(Scene scene) const;
    RuntimeConfig config_;
    bool started_ = false;
    TimePoint last_{};
    TimePoint deadline_ = TimePoint::max();
    bool obstacle_selected_ = false;
    Milliseconds obstacle_elapsed_{0};
    OperatingMode mode_ = OperatingMode::Unknown;
    bool mode_pending_ = false;
    std::uint64_t mode_event_ = 0;
    bool battery_low_ = false, battery_pending_ = false;
    TimePoint battery_since_{};
    bool translating_ = false;
    bool sideways_ = false;
    int turn_ = 0;
};
class VoiceCatalog final {
public:
    explicit VoiceCatalog(const RuntimeConfig& config) : resources_(config.voices) {}
    VoiceResource Resolve(const VoiceDecision& decision) const;
private:
    std::map<std::string, VoiceResource> resources_;
};
}
