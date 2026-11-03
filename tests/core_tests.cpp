#include "TestSupport.h"
#include "StateParser.h"
#include "AnnouncementTable.h"
#include "PlaybackScheduler.h"
#include <fstream>
#include <algorithm>
#include <limits>
#include <thread>

using namespace audioplay_detail;
using audio_engine::PlaybackRequest;
using audio_engine::PlaybackResult;
using audio_engine::EngineOptions;
using audio_engine::IPlaybackEngine;
namespace {
struct Probe {
    std::mutex mutex; std::condition_variable changed;
    std::vector<PlaybackRequest> starts;
    std::vector<PlaybackOutcome> outcomes;
    EngineOptions options;
    std::size_t active = 0, peak = 0, cancelled = 0;
    std::uint64_t finish_through = 0;
    bool fail = false, hold_cancel = false, init_ok = true;
    bool Wait(std::size_t count) {
        std::unique_lock<std::mutex> lock(mutex);
        return changed.wait_for(lock, std::chrono::seconds(2), [this, count] { return starts.size() >= count; });
    }
    bool WaitFinished(std::size_t count) {
        std::unique_lock<std::mutex> lock(mutex);
        return changed.wait_for(lock, std::chrono::seconds(2), [this, count] { return outcomes.size() >= count; });
    }
    void Finish(std::size_t count) {
        std::lock_guard<std::mutex> lock(mutex); finish_through = count; changed.notify_all();
    }
    std::size_t Count() { std::lock_guard<std::mutex> lock(mutex); return starts.size(); }
    std::string Last() { std::lock_guard<std::mutex> lock(mutex); return starts.back().text; }
};
class FakeBackend final : public IPlaybackEngine {
public:
    explicit FakeBackend(std::shared_ptr<Probe> p) : p_(p) {}
    bool Initialize(const EngineOptions& options, std::string& error) override { p_->options = options; error = "test init failure"; return p_->init_ok; }
    PlaybackResult Play(const PlaybackRequest& job, const CancelToken& token) override {
        std::unique_lock<std::mutex> lock(p_->mutex);
        p_->starts.push_back(job); ++p_->active; p_->peak = std::max(p_->peak, p_->active); p_->changed.notify_all();
        const auto call_number = p_->starts.size();
        while (!p_->fail && call_number > p_->finish_through && (!token->load() || p_->hold_cancel)) p_->changed.wait_for(lock, Milliseconds(2));
        PlaybackResult result;
        result.outcome = token->load() ? PlaybackOutcome::Cancelled : p_->fail ? PlaybackOutcome::Failed : PlaybackOutcome::Completed;
        if (result.outcome == PlaybackOutcome::Cancelled) ++p_->cancelled;
        p_->outcomes.push_back(result.outcome);
        --p_->active; p_->changed.notify_all(); return result;
    }
private: std::shared_ptr<Probe> p_;
};
std::unique_ptr<IPlaybackEngine> Backend(const std::shared_ptr<Probe>& p) { return std::unique_ptr<IPlaybackEngine>(new FakeBackend(p)); }
VoiceSnapshot Running() { VoiceSnapshot s; s.driving = true; s.motion_valid = true; s.vx_m_s = 0.3; return s; }
VoiceDecision At(VoicePolicy& p, VoiceSnapshot s, int ms, bool fresh = true) {
    const auto now = TimePoint() + Milliseconds(ms);
    if (fresh) s.received_at = now;
    return p.Evaluate(s, now);
}
void ConfigTests() {
    TempDirectory temp; const auto path = temp.path + "/audioplay.json";
    const auto load = [&](const std::string& json, RuntimeConfig& c) { std::ofstream f(path.c_str()); f << json; f.close(); std::string error; return LoadJsonConfig(path, c, error); };
    RuntimeConfig c;
    CHECK(load(R"({"audio":{},"policy":{},"voices":{"running":{"file":"../audio/run.wav","text":"运行"}}})", c));
    CHECK(c.voices.at("running").file_path == temp.path + "/../audio/run.wav");
    CHECK(c.input_timeout == Milliseconds(3000));
    CHECK(c.mode_prompt_enabled);
    CHECK(load(R"({"audio":{},"policy":{"mode_prompt_ms":0},"voices":{}})", c)); CHECK(!c.mode_prompt_enabled);
    CHECK(load(R"({"audio":{},"policy":{"mode_prompt_ms":1},"voices":{}})", c)); CHECK(c.mode_prompt_enabled);
    CHECK(load(R"({"audio":{},"policy":{"mode_prompt_ms":0,"mode_prompt_enabled":true},"voices":{}})", c)); CHECK(c.mode_prompt_enabled);
    CHECK(load(R"({"audio":{},"policy":{"mode_prompt_enabled":false},"voices":{}})", c)); CHECK(!c.mode_prompt_enabled);
    for (const char* bad : {
        "{}", "{",
        R"({"audio":{"enabled":"true"},"policy":{},"voices":{}})",
        R"({"audio":{"volume":2},"policy":{},"voices":{}})",
        R"({"audio":{"speed":1.5},"policy":{},"voices":{}})",
        R"({"audio":{},"policy":{"input_timeout_ms":0},"voices":{}})",
        R"({"audio":{},"policy":{"repeat_interval_ms":-1},"voices":{}})",
        R"({"audio":{},"policy":{"mode_prompt_enabled":"true"},"voices":{}})",
        R"({"audio":{},"policy":{"low_battery_trigger_percent":30,"low_battery_recover_percent":20},"voices":{}})",
        R"({"audio":{},"policy":{},"voices":{"running":{"file":3}}})",
    }) CHECK(!load(bad, c));
    CHECK(load(R"({"audio":{},"policy":{},"voices":{"running":{"text":"  "},"fault:device/2/3":{"text":"设备二故障"}}})", c));
    VoiceCatalog catalog(c); VoiceDecision d; d.key.scene = Scene::Running; d.default_text = "内置";
    CHECK(catalog.Resolve(d).fallback_text == "内置"); d.key.scene = Scene::Fault; d.key.detail = "device/2/3";
    CHECK(catalog.Resolve(d).fallback_text == "设备二故障");
    CHECK(load(R"({"audio":{},"policy":{},"voices":{"running":{"text":"first","text":"last"}},"comment":"ignored"})", c));
    CHECK(c.voices.at("running").fallback_text == "last");
    ::unlink(path.c_str()); ::rmdir(temp.path.c_str());
}
AudioError Error(const std::string& type, const std::string& desc, const std::string& level = "FATAL") {
    AudioError e; e.type = type; e.description = desc; e.level = level; return e;
}
void ParseTests() {
    StateParser parser; VoiceSnapshot s; AudioState in;
    in.e_stop = "UNKNOWN"; s = parser.Parse(in); CHECK(!s.emergency_stop);
    in.errors = {Error("COMMON_ERROR", "3"), Error("DEVICE_ERROR", "10 -> 8"), Error("DEVICE_ERROR", "2 -> 9"), Error("COMMON_WARNING", "3"), Error("ERROR_NAVI_DERAILMENT", "")};
    s = parser.Parse(in); CHECK(s.emergency_stop); CHECK(s.faults.front().kind == FaultKind::Touch);
    CHECK(s.faults[1].stable_key == "device/2/9"); CHECK(s.faults[2].stable_key == "device/10/8");
    const auto keys = s.faults;
    std::reverse(in.errors.begin(), in.errors.end()); s = parser.Parse(in);
    for (std::size_t i = 0; i < keys.size(); ++i) CHECK(keys[i].stable_key == s.faults[i].stable_key);
    in.e_stop = "AUTOACK"; in.vx_m_s = std::numeric_limits<double>::quiet_NaN();
    in.errors = {Error("COMMON_ERROR", "bad"), Error("COMMON_WARNING", "PING_TIMEOUT", "WARNING")};
    s = parser.Parse(in); CHECK(s.emergency_stop); CHECK(!s.motion_valid); CHECK(!s.faults.empty());
    in.errors = {Error("COMMON_ERROR", "4294967296")}; s = parser.Parse(in); CHECK(!s.faults.empty());
    in.e_stop = "NONE"; in.errors.clear(); s = parser.Parse(in); CHECK(!s.emergency_stop);
}
void MappingPipelineTests() {
    StateParser parser;
    RuntimeConfig c; c.mode_prompt_enabled = false;
    c.voices["fault:common/1"].file_path = "/recordings/touch.wav";
    c.voices["fault:common/1"].fallback_text = "触碰提示";
    c.voices["emergency_stop"].fallback_text = "急停提示";
    AudioState in; in.e_stop = "NONE"; in.charging = true;
    in.errors = {Error("DEVICE_ERROR", "2 -> 9"), Error("COMMON_ERROR", "10"), Error("ERROR_QR_LOC_QRCODE_LOST", "")};
    auto probe = std::make_shared<Probe>(); PlaybackScheduler scheduler(c, Backend(probe));
    CHECK(scheduler.UpdateState(parser.Parse(in)) == AudioPlay::Result::Accepted); CHECK(probe->Wait(1));
    { std::lock_guard<std::mutex> lock(probe->mutex);
      CHECK(probe->starts.back().file_path == "/recordings/touch.wav"); CHECK(probe->starts.back().text == "触碰提示"); }
    std::reverse(in.errors.begin(), in.errors.end()); scheduler.UpdateState(parser.Parse(in));
    std::this_thread::sleep_for(Milliseconds(25)); CHECK(probe->Count() == 1);
    in.e_stop = "MANUAL"; scheduler.UpdateState(parser.Parse(in)); CHECK(probe->Wait(2));
    CHECK(probe->Last() == "急停提示");
    scheduler.Shutdown(); CHECK(probe->peak == 1);
}
void DeviceErrorTests() {
    StateParser parser; AudioState in; in.e_stop = "NONE";
    // 与主工程的 int8_t / int16_t 序列化完全相同；0xFFFF 实际到达时为 -1。
    const std::int8_t id = 9;
    const std::int16_t raw = -1;
    in.errors = {Error("DEVICE_ERROR", std::to_string(id) + " -> " + std::to_string(raw)),
                 Error("DEVICE_ERROR", "9 -> 65535"), Error("DEVICE_ERROR", "9 -> 0xFFFF")};
    auto s = parser.Parse(in);
    CHECK(s.faults.size() == 1); CHECK(s.faults[0].stable_key == "device/9/65535");
    CHECK(s.faults[0].device_id == 9 && s.faults[0].code == 65535);
    CHECK(s.faults[0].fallback_text == "驱动器1掉线，复位无法清除，需断电重启。");
    in.errors = {Error("DEVICE_ERROR", "16 -> -1"), Error("DEVICE_ERROR", "1 -> -1"),
                 Error("DEVICE_ERROR", "2 -> 9"), Error("DEVICE_ERROR", "0 -> -1")};
    s = parser.Parse(in); CHECK(s.faults.size() == 3);
    CHECK(s.faults[0].fallback_text == "扩展板故障，故障码65535。"); // 不向非驱动器套用掉线码。
    CHECK(s.faults[1].fallback_text == "陀螺仪故障，故障码9。");
    CHECK(s.faults[2].fallback_text == "驱动器8掉线，复位无法清除，需断电重启。");
    in.errors = {Error("DEVICE_ERROR", "0 -> 0")}; CHECK(parser.Parse(in).faults.empty());
    in.errors = {Error("DEVICE_ERROR", "6 -> 4"), Error("DEVICE_ERROR", "42 -> 0"),
                 Error("DEVICE_ERROR", "-1 -> -32768")};
    s = parser.Parse(in); CHECK(s.faults.size() == 3);
    CHECK(s.faults[0].fallback_text == "预留设备故障，故障码4。");
    CHECK(s.faults[1].stable_key == "device/42/0"); // 非零故障设备 ID 下，未知码 0 也不擅自清除。
    CHECK(s.faults[2].stable_key == "device/255/32768");
    for (const char* desc : {"9 -> -32769", "9 -> 65536", "256 -> 1", "-129 -> 1", "9 -> bad", "9 -> 1 -> 2"}) {
        in.errors = {Error("DEVICE_ERROR", desc)}; s = parser.Parse(in);
        CHECK(s.faults.size() == 1 && s.faults[0].stable_key == "device/unknown");
    }
    in.errors = {Error("COMMON_ERROR", "68"), Error("ERROR_QR_LOC_QRCODE_LOST", "")};
    s = parser.Parse(in); CHECK(s.faults.size() == 3);
    CHECK(s.faults[0].stable_key == "ipc/ERROR_QR_LOC_QRCODE_LOST"); // 具体 IPC 错误先于汇总 bit2。
    CHECK(s.faults[1].stable_key == "common/2");
    CHECK(s.faults[2].fallback_text == "舵轮差速转向超时，请检查。");

    // 归一化后的同一资源键查 JSON；不同表示法更新 State 不打断正在播放的录音请求。
    TempDirectory temp; RuntimeConfig c; std::string error;
    const auto path = temp.path + "/audioplay.json";
    { std::ofstream f(path.c_str());
      f << R"({"audio":{},"policy":{"mode_prompt_ms":0},"voices":{"fault:device/9/65535":{"file":"../audio/driver1_offline.wav"}}})"; }
    CHECK(LoadJsonConfig(path, c, error));
    auto probe = std::make_shared<Probe>(); PlaybackScheduler scheduler(c, Backend(probe));
    in.charging = true; in.errors = {Error("DEVICE_ERROR", "9 -> -1")};
    scheduler.UpdateState(parser.Parse(in)); CHECK(probe->Wait(1));
    { std::lock_guard<std::mutex> lock(probe->mutex);
      CHECK(probe->starts.back().file_path == temp.path + "/../audio/driver1_offline.wav");
      CHECK(probe->starts.back().text == "驱动器1掉线，复位无法清除，需断电重启。"); }
    for (const char* desc : {"9 -> 65535", "9 -> 0xffff"}) {
        in.errors = {Error("DEVICE_ERROR", desc)}; scheduler.UpdateState(parser.Parse(in));
        std::this_thread::sleep_for(Milliseconds(25)); CHECK(probe->Count() == 1);
    }
    in.errors.clear(); scheduler.UpdateState(parser.Parse(in)); CHECK(probe->Wait(2));
    CHECK(probe->Last() == "车辆正在充电。");
    scheduler.Shutdown(); CHECK(probe->peak == 1);
    ::unlink(path.c_str()); ::rmdir(temp.path.c_str());
}
void RecordingMappingTests() {
    RuntimeConfig c; std::string error;
    CHECK(LoadJsonConfig(AUDIOPLAY_RECORDINGS_CONFIG, c, error));
    c.mode_prompt_enabled = false;
    StateParser parser; VoiceCatalog catalog(c);
    const auto resolve = [&](const AudioError& input) {
        AudioState state; state.e_stop = "NONE"; state.errors = {input};
        VoicePolicy policy(c);
        return catalog.Resolve(At(policy, parser.Parse(state), 0));
    };
    struct Case { const char* type; const char* description; const char* file; };
    const Case cases[] = {
        {"COMMON_ERROR", "1", "043.mp3"}, {"COMMON_ERROR", "2", "044.mp3"},
        {"COMMON_ERROR", "4", "050.mp3"}, {"COMMON_ERROR", "8", "041.mp3"},
        {"COMMON_ERROR", "16", "041.mp3"}, {"COMMON_ERROR", "32", "100.mp3"},
        {"COMMON_ERROR", "64", ""}, {"COMMON_WARNING", "1", "101.mp3"},
        {"COMMON_WARNING", "2", "102.mp3"}, {"COMMON_WARNING", "PING_TIMEOUT", "042.mp3"},
        {"ERROR_QR_LOC_NOT_INIT", "", "103.mp3"}, {"ERROR_QR_LOC_INITING", "", "104.mp3"},
        {"ERROR_QR_LOC_INITED", "", "055.mp3"}, {"ERROR_QR_LOC_ODOM_ERROR", "", "106.mp3"},
        {"ERROR_QR_LOC_QRCODE_LOST", "", ""}, {"ERROR_QR_LOC_ODOM_NO_DATA", "", "108.mp3"},
        {"ERROR_NAVI_DERAILMENT", "", "109.mp3"}, {"ERROR_NAVI_OVER_POINT", "", ""},
        {"ERROR_QR_LOC_NO_DATA", "", "111.mp3"}, {"ERROR_RCS_EXIST_ERROR", "", ""},
        {"ERROR_QR_LOC_ODOM_WHEEL_PULSE_NOT_UPDATE", "", "113.mp3"},
        {"ERROR_QR_LOC_CANNOT_READ_MORE", "", ""}, {"ERROR_QR_LOC_ROT_LOSS_CODE", "", ""},
        {"ERROR_NOT_FIND_QR", "", ""}, {"ERROR_NAVI_WHEEL_SLIP", "", ""},
        {"ERROR_NAVI_ARRIVE_NOT_FINISH", "", ""}, {"ERROR_RCS_TASK", "", ""},
        {"UNMAPPED_ERROR", "110", ""}, {"DEVICE_ERROR", "9 -> invalid", "051.mp3"}
    };
    for (const auto& item : cases) {
        const auto resource = resolve(Error(item.type, item.description));
        const auto name = resource.file_path.empty() ? "" : resource.file_path.substr(resource.file_path.find_last_of('/') + 1);
        CHECK(name == item.file); CHECK(!resource.fallback_text.empty());
    }
    for (int id = 1; id <= 16; ++id) {
        const auto r = resolve(Error("DEVICE_ERROR", std::to_string(id) + " -> -1"));
        if (id == 6 || id == 15 || id == 16) CHECK(r.file_path.empty());
        else CHECK(r.file_path.find("/0" + std::to_string(id + 60) + ".mp3") != std::string::npos);
        if (id >= 9) CHECK(r.fallback_text.find("掉线") != std::string::npos);
    }
    CHECK(resolve(Error("DEVICE_ERROR", "42 -> 80")).file_path.empty()); // 80不是录音编号映射。
    CHECK(resolve(Error("DEVICE_ERROR", "1 -> 80")).file_path.find("061.mp3") != std::string::npos);

    VoiceDecision d; d.key.scene = Scene::Fault; d.key.detail = "device/9/65535"; d.default_text = "精确故障文案";
    CHECK(catalog.Resolve(d).file_path.find("069.mp3") != std::string::npos);
    CHECK(catalog.Resolve(d).fallback_text == d.default_text);
    c.voices["fault:device/9/65535"].file_path = "/specific/offline.mp3";
    CHECK(VoiceCatalog(c).Resolve(d).file_path == "/specific/offline.mp3");
    c.voices["fault:device/9/65535"].file_path.clear(); // 空精确项表示该码直接TTS。
    CHECK(VoiceCatalog(c).Resolve(d).file_path.empty());
    d.key.detail = "device/9/7";
    CHECK(VoiceCatalog(c).Resolve(d).file_path.find("069.mp3") != std::string::npos);
    d.key.detail = "device/90/7";
    CHECK(VoiceCatalog(c).Resolve(d).file_path.empty()); // 不按字符串前缀误匹配设备9。

    VoicePolicy sideways(c); auto s = Running(); s.vx_m_s = 0; s.vy_m_s = 0.2;
    auto motion = At(sideways, s, 0);
    CHECK(motion.key.scene == Scene::Sideways && motion.rank == 7);
    CHECK(catalog.Resolve(motion).file_path.find("009.mp3") != std::string::npos);
    s.vx_m_s = 0.015; s.vy_m_s = 0.015; CHECK(At(sideways, s, 1).key.scene == Scene::Sideways);
    s.vx_m_s = 0.03; CHECK(At(sideways, s, 2).key.scene == Scene::Running);
    s.vx_m_s = 0; s.vy_m_s = -0.2; CHECK(At(sideways, s, 3).key.scene == Scene::Sideways);
    s.omega_rad_s = 0.2; CHECK(At(sideways, s, 4).key.scene == Scene::LeftTurn);
    s.omega_rad_s = 0; s.paused = true; CHECK(!At(sideways, s, 5).active);
    s.paused = false; s.emergency_stop = true; CHECK(At(sideways, s, 6).key.scene == Scene::EmergencyStop);
}
void PolicyTests() {
    RuntimeConfig c; c.mode_prompt_enabled = false; c.input_timeout = Milliseconds(100000);
    VoicePolicy p(c); auto s = Running();
    s.emergency_stop = true; s.obstacle = true; s.charging = true;
    CHECK(At(p, s, 0).key.scene == Scene::EmergencyStop);
    s.emergency_stop = false; Fault f; f.stable_key = "a"; f.fallback_text = "故障"; s.faults.push_back(f);
    CHECK(At(p, s, 1).key.scene == Scene::Fault);
    s.faults.clear(); CHECK(At(p, s, 2).key.scene == Scene::Charging);
    s.charging = false; CHECK(At(p, s, 3).key.scene == Scene::Obstacle);
    s.obstacle = false; CHECK(At(p, s, 4).key.scene == Scene::Running);
    s.omega_rad_s = 0.11; CHECK(At(p, s, 5).key.scene == Scene::LeftTurn);
    s.omega_rad_s = 0.08; CHECK(At(p, s, 6).key.scene == Scene::LeftTurn);
    s.omega_rad_s = 0.06; CHECK(At(p, s, 7).key.scene == Scene::Running);
    s.omega_rad_s = -0.11; CHECK(At(p, s, 8).key.scene == Scene::RightTurn);
    s.vx_m_s = 0; CHECK(At(p, s, 9).key.scene == Scene::Rotating);
    s.paused = true; CHECK(!At(p, s, 10).active);

    VoicePolicy obstacle(c); s = Running(); s.obstacle = true;
    CHECK(At(obstacle, s, 0).audible);
    s.emergency_stop = true; CHECK(At(obstacle, s, 10000).key.scene == Scene::EmergencyStop);
    CHECK(At(obstacle, s, 30000).key.scene == Scene::EmergencyStop);
    s.emergency_stop = false; CHECK(At(obstacle, s, 40000).audible);
    auto silent = At(obstacle, s, 60000); CHECK(silent.active && !silent.audible && silent.key.scene == Scene::Obstacle);
    s.obstacle = false; CHECK(At(obstacle, s, 61000).key.scene == Scene::Running);
    s.obstacle = true; CHECK(At(obstacle, s, 62000).audible);

    c.mode_prompt_enabled = false; c.input_timeout = Milliseconds(3000); c.low_battery_confirm = Milliseconds(2000);
    VoicePolicy battery(c); s = Running(); s.battery_valid = true; s.battery_percent = 19;
    CHECK(At(battery, s, 0).key.scene == Scene::Running);
    CHECK(At(battery, s, 1000).key.scene == Scene::Running);
    CHECK(At(battery, s, 2000).key.scene == Scene::LowBattery);
    s.battery_percent = 24; CHECK(At(battery, s, 2100).key.scene == Scene::LowBattery);
    s.battery_percent = 25; CHECK(At(battery, s, 2200).key.scene == Scene::Running);
    VoicePolicy stale(c); s = Running(); CHECK(At(stale, s, 0).active); CHECK(!At(stale, s, 3000, false).active);
    s.emergency_stop = true; CHECK(At(stale, s, 4000, false).key.scene == Scene::EmergencyStop);
    c.low_battery_confirm = Milliseconds(4000); VoicePolicy no_confirm(c); s = Running(); s.battery_valid = true; s.battery_percent = 0;
    CHECK(At(no_confirm, s, 0).key.scene == Scene::Running); CHECK(!At(no_confirm, s, 4000, false).active);
}
void ModePolicyTests() {
    RuntimeConfig c; VoicePolicy policy(c); auto s = Running();
    s.mode = OperatingMode::Manual;
    const auto first = At(policy, s, 0);
    CHECK(first.key.scene == Scene::ManualMode && !first.repeat);
    // 超过原3秒窗口且不再收到State，句子仍等待播放完成。
    CHECK(At(policy, s, 4000, false).key.scene == Scene::ManualMode);
    policy.OnPlaybackCompleted(first.key);
    CHECK(At(policy, s, 5000).key.scene == Scene::Running);
    s.mode = OperatingMode::Unknown; CHECK(At(policy, s, 5100).key.scene == Scene::Running);
    s.mode = OperatingMode::Manual; CHECK(At(policy, s, 5200).key.scene == Scene::Running);

    // 接收层观察到往返切换，即使选中场景同为手动，也必须是新的待播事件。
    policy.ObserveMode(OperatingMode::Automatic);
    policy.ObserveMode(OperatingMode::Manual);
    policy.OnPlaybackCompleted(first.key);
    const auto latest = At(policy, s, 6000);
    CHECK(latest.key.scene == Scene::ManualMode && latest.key.event_id != first.key.event_id);
    s.emergency_stop = true; CHECK(At(policy, s, 6100).key.scene == Scene::EmergencyStop);
    s.emergency_stop = false; CHECK(At(policy, s, 6200).key.event_id == latest.key.event_id);
    policy.OnPlaybackCompleted(latest.key); CHECK(At(policy, s, 6300).key.scene == Scene::Running);
    s.mode = OperatingMode::Automatic;
    const auto automatic = At(policy, s, 6400);
    CHECK(automatic.key.scene == Scene::AutomaticMode && !automatic.repeat);
    policy.OnPlaybackCompleted(automatic.key); CHECK(At(policy, s, 6500).key.scene == Scene::Running);
    c.mode_prompt_enabled = false; VoicePolicy disabled(c); CHECK(At(disabled, s, 0).key.scene == Scene::Running);
}
void ModeSchedulerTests() {
    RuntimeConfig c; c.repeat_interval = Milliseconds(20);
    c.voices["manual_mode"].file_path = "/recordings/manual_mode.wav";
    c.voices["automatic_mode"].file_path = "/recordings/automatic_mode.wav";
    {
        StateParser parser; AudioState state; state.operating_mode = "AUTOMATIC";
        auto probe = std::make_shared<Probe>(); PlaybackScheduler scheduler(c, Backend(probe));
        scheduler.UpdateState(parser.Parse(state)); CHECK(probe->Wait(1));
        CHECK(probe->Last() == "自动模式已开启。");
        { std::lock_guard<std::mutex> lock(probe->mutex); CHECK(probe->starts.back().file_path == "/recordings/automatic_mode.wav"); }
        probe->Finish(1); CHECK(probe->WaitFinished(1));
        for (int i = 0; i < 100; ++i) scheduler.UpdateState(parser.Parse(state));
        std::this_thread::sleep_for(Milliseconds(60)); CHECK(probe->Count() == 1);
        state.operating_mode = "MANUAL"; scheduler.UpdateState(parser.Parse(state)); CHECK(probe->Wait(2));
        CHECK(probe->Last() == "手动模式已开启。");
        { std::lock_guard<std::mutex> lock(probe->mutex); CHECK(probe->starts.back().file_path == "/recordings/manual_mode.wav"); }
        // 录音/TTS长于3秒时，原提示窗口及输入超时都不能截断本次句子。
        std::this_thread::sleep_for(Milliseconds(3200));
        { std::lock_guard<std::mutex> lock(probe->mutex); CHECK(probe->active == 1 && probe->cancelled == 0); }
        for (int i = 0; i < 100; ++i) scheduler.UpdateState(parser.Parse(state));
        probe->Finish(2); CHECK(probe->WaitFinished(2));
        std::this_thread::sleep_for(Milliseconds(60)); CHECK(probe->Count() == 2);
        state.operating_mode = "AUTOMATIC"; scheduler.UpdateState(parser.Parse(state)); CHECK(probe->Wait(3));
        CHECK(probe->Last() == "自动模式已开启。");
        probe->Finish(3); CHECK(probe->WaitFinished(3));
        std::this_thread::sleep_for(Milliseconds(60)); CHECK(probe->Count() == 3);
        scheduler.Shutdown(); CHECK(probe->peak == 1 && probe->cancelled == 0);
    }
    {
        auto probe = std::make_shared<Probe>(); PlaybackScheduler scheduler(c, Backend(probe));
        auto s = Running(); s.mode = OperatingMode::Manual;
        scheduler.UpdateState(s); CHECK(probe->Wait(1));
        s.emergency_stop = true; scheduler.UpdateState(s); CHECK(probe->Wait(2));
        CHECK(probe->Last() == "急停已触发，请检查并复位。");
        s.emergency_stop = false; scheduler.UpdateState(s); CHECK(probe->Wait(3));
        CHECK(probe->Last() == "手动模式已开启。");
        probe->Finish(3); CHECK(probe->Wait(4)); CHECK(probe->Last() == "车辆正在运行，请注意安全。");
        // 播完释放模式优先级；之后的急停解除不能再次播报已完成的模式。
        s.emergency_stop = true; scheduler.UpdateState(s); CHECK(probe->Wait(5));
        s.emergency_stop = false; scheduler.UpdateState(s); CHECK(probe->Wait(6));
        CHECK(probe->Last() == "车辆正在运行，请注意安全。");
        scheduler.Shutdown(); CHECK(probe->peak == 1);
    }
    {
        auto probe = std::make_shared<Probe>(); probe->hold_cancel = true;
        PlaybackScheduler scheduler(c, Backend(probe)); VoiceSnapshot s; s.mode = OperatingMode::Manual;
        scheduler.UpdateState(s); CHECK(probe->Wait(1));
        s.mode = OperatingMode::Automatic; scheduler.UpdateState(s);
        s.mode = OperatingMode::Manual; scheduler.UpdateState(s);
        s.mode = OperatingMode::Automatic; scheduler.UpdateState(s);
        std::this_thread::sleep_for(Milliseconds(40)); CHECK(probe->Count() == 1);
        { std::lock_guard<std::mutex> lock(probe->mutex); probe->hold_cancel = false; probe->changed.notify_all(); }
        CHECK(probe->Wait(2)); CHECK(probe->Last() == "自动模式已开启。");
        probe->Finish(2); CHECK(probe->WaitFinished(2));
        scheduler.UpdateState(s); std::this_thread::sleep_for(Milliseconds(60)); CHECK(probe->Count() == 2);
        scheduler.Shutdown(); CHECK(probe->peak == 1 && probe->cancelled == 1);
    }
    {
        auto probe = std::make_shared<Probe>(); probe->fail = true; c.retry_interval = Milliseconds(100);
        PlaybackScheduler scheduler(c, Backend(probe)); VoiceSnapshot s; s.mode = OperatingMode::Manual;
        scheduler.UpdateState(s); CHECK(probe->WaitFinished(1));
        { std::lock_guard<std::mutex> lock(probe->mutex); probe->fail = false; }
        CHECK(probe->Wait(2)); CHECK(probe->Last() == "手动模式已开启。");
        probe->Finish(2); CHECK(probe->WaitFinished(2));
        scheduler.UpdateState(s); std::this_thread::sleep_for(Milliseconds(150)); CHECK(probe->Count() == 2);
        scheduler.Shutdown(); CHECK(probe->peak == 1 && probe->cancelled == 0);
    }
}
void SchedulerTests() {
    RuntimeConfig c; c.mode_prompt_enabled = false; c.input_timeout = Milliseconds(10000); c.repeat_interval = Milliseconds(70);
    c.volume = 0.4; c.speed = 17; c.voice = "pinyin";
    c.voices["running"].file_path = "/recordings/running.wav";
    c.voices["running"].fallback_text = "test-running";
    c.voices["emergency_stop"].fallback_text = "test-emergency";
    c.voices["obstacle"].fallback_text = "test-obstacle";
    {
        auto probe = std::make_shared<Probe>(); PlaybackScheduler scheduler(c, Backend(probe)); auto s = Running();
        CHECK(scheduler.UpdateState(s) == AudioPlay::Result::Accepted); CHECK(probe->Wait(1));
        CHECK(probe->options.volume == 0.4 && probe->options.speed == 17 && probe->options.voice == "pinyin");
        { std::lock_guard<std::mutex> lock(probe->mutex); CHECK(probe->starts.back().file_path == "/recordings/running.wav"); }
        std::vector<std::thread> senders;
        for (int i = 0; i < 4; ++i) senders.emplace_back([&] { for (int k = 0; k < 250; ++k) CHECK(scheduler.UpdateState(s) == AudioPlay::Result::Accepted); });
        for (auto& t : senders) t.join();
        std::this_thread::sleep_for(Milliseconds(25)); CHECK(probe->Count() == 1);
        s.emergency_stop = true; scheduler.UpdateState(s); CHECK(probe->Wait(2)); CHECK(probe->Last() == "test-emergency");
        s.obstacle = true; scheduler.UpdateState(s); std::this_thread::sleep_for(Milliseconds(25)); CHECK(probe->Count() == 2);
        s.emergency_stop = false; scheduler.UpdateState(s); CHECK(probe->Wait(3)); CHECK(probe->Last() == "test-obstacle");
        s.obstacle = false; scheduler.UpdateState(s); CHECK(probe->Wait(4)); CHECK(probe->Last() == "test-running");
        scheduler.Shutdown(); CHECK(probe->peak == 1); CHECK(probe->cancelled == 4);
        CHECK(scheduler.UpdateState(s) == AudioPlay::Result::ShuttingDown);
    }
    {
        auto probe = std::make_shared<Probe>(); PlaybackScheduler scheduler(c, Backend(probe)); auto s = Running();
        scheduler.UpdateState(s); CHECK(probe->Wait(1));
        { std::lock_guard<std::mutex> lock(probe->mutex); probe->hold_cancel = true; }
        s.emergency_stop = true; scheduler.UpdateState(s); std::this_thread::sleep_for(Milliseconds(20));
        s.emergency_stop = false; s.charging = true; scheduler.UpdateState(s);
        s.charging = false; s.obstacle = true; scheduler.UpdateState(s); std::this_thread::sleep_for(Milliseconds(20));
        { std::lock_guard<std::mutex> lock(probe->mutex); probe->hold_cancel = false; probe->changed.notify_all(); }
        CHECK(probe->Wait(2)); CHECK(probe->Last() == "test-obstacle"); CHECK(probe->Count() == 2);
    }
    {
        auto probe = std::make_shared<Probe>(); PlaybackScheduler scheduler(c, Backend(probe)); scheduler.UpdateState(Running()); CHECK(probe->Wait(1));
        { std::lock_guard<std::mutex> lock(probe->mutex); probe->finish_through = 1; probe->changed.notify_all(); }
        std::this_thread::sleep_for(Milliseconds(25)); CHECK(probe->Count() == 1);
        CHECK(probe->Wait(2)); CHECK(probe->Last() == "test-running");
    }
    {
        auto probe = std::make_shared<Probe>(); probe->fail = true;
        c.retry_interval = Milliseconds(100); PlaybackScheduler scheduler(c, Backend(probe)); scheduler.UpdateState(Running()); CHECK(probe->Wait(1));
        std::this_thread::sleep_for(Milliseconds(25)); CHECK(probe->Count() == 1); CHECK(probe->Wait(2));
    }
    {
        auto probe = std::make_shared<Probe>(); c.obstacle_limit = Milliseconds(50); PlaybackScheduler scheduler(c, Backend(probe));
        auto s = Running(); s.obstacle = true; scheduler.UpdateState(s); CHECK(probe->Wait(1));
        { std::unique_lock<std::mutex> lock(probe->mutex); CHECK(probe->changed.wait_for(lock, Milliseconds(1000), [&] { return probe->cancelled == 1; })); }
        scheduler.UpdateState(s); std::this_thread::sleep_for(Milliseconds(25)); CHECK(probe->Count() == 1);
        s.obstacle = false; scheduler.UpdateState(s); CHECK(probe->Wait(2)); CHECK(probe->Last() == "test-running");
    }
    {
        auto probe = std::make_shared<Probe>(); c.input_timeout = Milliseconds(100); PlaybackScheduler scheduler(c, Backend(probe));
        scheduler.UpdateState(Running()); CHECK(probe->Wait(1));
        std::unique_lock<std::mutex> lock(probe->mutex); CHECK(probe->changed.wait_for(lock, Milliseconds(1000), [&] { return probe->cancelled == 1; }));
    }
    {
        auto probe = std::make_shared<Probe>(); probe->init_ok = false; PlaybackScheduler scheduler(c, Backend(probe)); CHECK(scheduler.UpdateState(Running()) == AudioPlay::Result::NotReady);
    }
    {
        auto probe = std::make_shared<Probe>(); c.enabled = false; PlaybackScheduler scheduler(c, Backend(probe)); CHECK(scheduler.UpdateState(Running()) == AudioPlay::Result::Disabled); CHECK(probe->Count() == 0);
    }
}
}
namespace audio_engine { std::unique_ptr<IPlaybackEngine> CreatePlaybackEngine() { return Backend(std::make_shared<Probe>()); } }
int main(int argc, char** argv) {
    try {
        if (argc > 1) {
            AudioState state; state.e_stop = "NONE"; state.battery_percent = 80; state.driving = true; state.vx_m_s = 0.3;
            const auto r = AudioPlay::GetInstance().Play(state);
            if (std::string(argv[1]) == "accepted") CHECK(r == AudioPlay::Result::Accepted);
            else if (std::string(argv[1]) == "disabled") CHECK(r == AudioPlay::Result::Disabled);
            else CHECK(r == AudioPlay::Result::NotReady);
            return 0;
        }
        ConfigTests(); std::cout << "JSON/config cases passed\n";
        ParseTests(); std::cout << "State-to-announcement table cases passed\n";
        MappingPipelineTests(); std::cout << "State -> mapping -> priority -> JSON -> engine request passed\n";
        DeviceErrorTests(); std::cout << "Device IDs/signed 16-bit codes/offline mapping/JSON/dedup passed\n";
        RecordingMappingTests(); std::cout << "Shipped MP3 bindings/device wildcard/exact override/unbound codes/sideways passed\n";
        PolicyTests(); std::cout << "Priority/timing/hysteresis/staleness cases passed\n";
        ModePolicyTests(); std::cout << "Mode events/completion/identity/disabled cases passed\n";
        ModeSchedulerTests(); std::cout << "Mode once/whole sentence/preemption/latest event/retry cases passed\n";
        SchedulerTests(); std::cout << "Preemption/dedup/repeat/concurrency/shutdown cases passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
