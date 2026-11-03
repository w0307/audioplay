#pragma once

#include <audio_engine/PlaybackTypes.h>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// 库内部类型，不安装为业务公共接口。
namespace audioplay_detail {

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using Milliseconds = std::chrono::milliseconds;

enum class OperatingMode { Unknown, Automatic, Manual };

enum class FaultKind
{
    Touch,
    LimitOrCoupling,
    Device,
    Parameter,
    NavigationCommunication,
    UpperCommunication,
    Ipc,
    Other
};

struct Fault
{
    FaultKind kind = FaultKind::Other;
    std::uint32_t device_id = 0;
    std::uint32_t code = 0;
    std::uint32_t order = 0;    // 明确的类别内排序，独立于 errors 输入顺序
    std::string stable_key;      // 规范化标识；不依赖 errors 数组顺序
    std::string fallback_text;   // 解析、映射后的可播中文；不直接朗读原始位图
};

struct VoiceSnapshot
{
    // sequence 是本进程接收序号，由接收层按接受顺序分配。
    // 它用于输入排序；不能作为取消当前音频的场景版本。
    std::uint64_t sequence = 0;
    TimePoint received_at{};
    bool emergency_stop = false;
    std::vector<Fault> faults;
    bool charging = false;
    bool obstacle = false;      // 当前版仅表示 fieldViolation
    bool battery_valid = false;
    double battery_percent = 0.0;
    OperatingMode mode = OperatingMode::Unknown;
    bool driving = false;
    bool paused = false;
    bool motion_valid = false;
    double vx_m_s = 0.0;
    double vy_m_s = 0.0;
    double omega_rad_s = 0.0;
};

enum class Scene
{
    None,
    EmergencyStop,
    Fault,
    Charging,
    Obstacle,
    LowBattery,
    ManualMode,
    AutomaticMode,
    Running,
    LeftTurn,
    RightTurn,
    Rotating,
    Sideways
};

struct SceneKey
{
    Scene scene = Scene::None;
    std::string detail; // 同类故障的设备/错误码标识；普通场景为空
    std::uint64_t event_id = 0; // 模式切换事件；同一模式的新切换不能被旧完成通知清除
};

// 决策与声音资源分开；无声的占位场景也必须保留。
struct VoiceDecision
{
    SceneKey key;
    std::uint32_t rank = 0;       // 内部业务顺序；数值越小优先级越高，None 不参与比较
    bool active = false;         // 当前是否有有效场景
    bool audible = false;        // 此刻是否允许该场景出声
    bool repeat = false;
    Milliseconds repeat_gap{1000}; // 本句正常播完后的间隔；可配置
    std::string default_text;    // 内置默认文案；可含已验证的设备编号
};

struct VoiceResource
{
    std::string file_path;       // 可为空；由资源表解析为绝对路径
    std::string fallback_text;   // 状态场景必须有可用文案；优先配置，缺省用内置文案
};

using audio_engine::CancelToken;
using audio_engine::PlaybackOutcome;
using audio_engine::ErrorStage;

struct PlaybackJob
{
    std::uint64_t task_id = 0;
    std::uint64_t scene_revision = 0; // 只在播报决策实质变化时更新
    SceneKey scene;
    VoiceResource resource;
};

struct PlaybackCompletion
{
    std::uint64_t task_id = 0;
    std::uint64_t scene_revision = 0;
    audio_engine::PlaybackResult result;
};

} // namespace audioplay_detail

