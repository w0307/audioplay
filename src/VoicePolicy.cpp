#include "VoicePolicy.h"
#include "AnnouncementTable.h"
#include <algorithm>
#include <cmath>

namespace audioplay_detail {
void VoicePolicy::ObserveMode(OperatingMode mode) {
    // 只记住最后一个有效模式；缺失字段不能制造一次新的模式切换。
    if (mode == OperatingMode::Unknown || mode == mode_) return;
    mode_ = mode;
    ++mode_event_;
    mode_pending_ = config_.mode_prompt_enabled;
}
void VoicePolicy::OnPlaybackCompleted(const SceneKey& scene) {
    if ((scene.scene == Scene::ManualMode || scene.scene == Scene::AutomaticMode) &&
        scene.event_id == mode_event_) mode_pending_ = false;
}
VoiceDecision VoicePolicy::Make(Scene scene) const {
    const auto& rule = RuleFor(scene);
    VoiceDecision d; d.key.scene = scene; d.rank = rule.priority;
    d.active = scene != Scene::None; d.audible = d.active;
    d.repeat = d.active; d.repeat_gap = config_.repeat_interval; d.default_text = rule.text;
    return d;
}
VoiceDecision VoicePolicy::Evaluate(const VoiceSnapshot& s, TimePoint now) {
    if (!started_) { started_ = true; last_ = now; }
    if (now < last_) now = last_;
    const auto elapsed = std::chrono::duration_cast<Milliseconds>(now - last_);
    last_ += elapsed;
    if (obstacle_selected_) obstacle_elapsed_ = std::min(config_.obstacle_limit, obstacle_elapsed_ + elapsed);
    obstacle_selected_ = false;
    deadline_ = TimePoint::max();
    const auto expiry = s.received_at + config_.input_timeout;
    const bool stale = now >= expiry;
    if (!stale) deadline_ = expiry;
    if (!s.obstacle) obstacle_elapsed_ = Milliseconds(0);
    ObserveMode(s.mode);

    // 未收到新的有效电量不能被当作确认；已确认低电不凭超时自动解除。
    if (!stale && s.battery_valid) {
        if (s.battery_percent >= config_.low_battery_recover) { battery_low_ = false; battery_pending_ = false; }
        else if (!battery_low_ && s.battery_percent <= config_.low_battery_trigger) {
            if (!battery_pending_) { battery_pending_ = true; battery_since_ = now; }
            if (now >= battery_since_ + config_.low_battery_confirm) { battery_low_ = true; battery_pending_ = false; }
            else deadline_ = std::min(deadline_, battery_since_ + config_.low_battery_confirm);
        } else battery_pending_ = false;
    } else battery_pending_ = false;

    // 先生成候选，再按表中 priority 选择；不依靠 if/else 的排列顺序。
    // 模式是一次性事件，等待整句完成，不使用持续状态的超时来截断句子。
    const Scene mode = mode_pending_ && s.mode != OperatingMode::Unknown ?
        (mode_ == OperatingMode::Manual ? Scene::ManualMode : Scene::AutomaticMode) : Scene::None;
    Scene motion = Scene::None;
    bool translating = translating_, sideways = sideways_; int turn = turn_;
    if (stale || !s.motion_valid || !s.driving || s.paused) { translating = sideways = false; turn = 0; }
    else {
        const double linear = std::hypot(s.vx_m_s, s.vy_m_s);
        if (linear >= config_.linear_enter) translating = true;
        else if (linear <= config_.linear_exit) translating = false;
        // 只用既有速度识别纯横移；纵向/横向都有明显速度时仍使用运行提示。
        if (std::abs(s.vy_m_s) >= config_.linear_enter && std::abs(s.vx_m_s) <= config_.linear_exit) sideways = true;
        else if (std::abs(s.vy_m_s) <= config_.linear_exit || std::abs(s.vx_m_s) >= config_.linear_enter) sideways = false;
        if (s.omega_rad_s >= config_.angular_enter) turn = 1;
        else if (s.omega_rad_s <= -config_.angular_enter) turn = -1;
        else if (std::abs(s.omega_rad_s) <= config_.angular_exit) turn = 0;
        if (!translating && turn) motion = Scene::Rotating;
        else if (translating) motion = turn > 0 ? Scene::LeftTurn : turn < 0 ? Scene::RightTurn : sideways ? Scene::Sideways : Scene::Running;
    }
    const Scene candidates[] = {
        motion, mode,
        battery_low_ ? Scene::LowBattery : Scene::None,
        s.obstacle ? Scene::Obstacle : Scene::None,
        !stale && s.charging ? Scene::Charging : Scene::None,
        !s.faults.empty() ? Scene::Fault : Scene::None,
        s.emergency_stop ? Scene::EmergencyStop : Scene::None
    };
    const SceneRule* selected = nullptr;
    for (const auto scene : candidates) {
        if (scene == Scene::None) continue;
        const auto& rule = RuleFor(scene);
        if (!selected || rule.priority < selected->priority) selected = &rule;
    }
    if (!selected || selected->scene == motion) { translating_ = translating; sideways_ = sideways; turn_ = turn; }
    if (!selected) return VoiceDecision();
    auto decision = Make(selected->scene);
    if (selected->scene == Scene::Fault) {
        decision.key.detail = s.faults.front().stable_key;
        decision.default_text = s.faults.front().fallback_text;
    } else if (selected->scene == Scene::Obstacle) {
        decision.audible = obstacle_elapsed_ < config_.obstacle_limit;
        if (decision.audible) {
            obstacle_selected_ = true;
            deadline_ = std::min(deadline_, now + config_.obstacle_limit - obstacle_elapsed_);
        }
    } else if (selected->scene == Scene::ManualMode || selected->scene == Scene::AutomaticMode) {
        decision.repeat = false;
        decision.key.event_id = mode_event_;
    }
    return decision;
}

VoiceResource VoiceCatalog::Resolve(const VoiceDecision& d) const {
    VoiceResource r;
    auto it = resources_.end();
    if (d.key.scene == Scene::Fault && !d.key.detail.empty()) it = resources_.find("fault:" + d.key.detail);
    // 精确设备故障码 > 设备通用录音 > 通用故障。通配只作用于资源，
    // 不合并 stable_key，不改变优先级/去重，也不把录音编号当作故障码。
    if (it == resources_.end() && d.key.scene == Scene::Fault && d.key.detail.compare(0, 7, "device/") == 0) {
        const auto slash = d.key.detail.find('/', 7);
        if (slash != std::string::npos) it = resources_.find("fault:" + d.key.detail.substr(0, slash) + "/*");
    }
    if (it == resources_.end()) it = resources_.find(SceneName(d.key.scene));
    if (it != resources_.end()) r = it->second;
    if (r.fallback_text.empty()) r.fallback_text = d.default_text;
    return r;
}
}
