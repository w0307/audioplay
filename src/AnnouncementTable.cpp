#include "AnnouncementTable.h"

namespace audioplay_detail {
namespace {
// 修改场景名称、默认文案或优先级，只维护此表。
const SceneRule scenes[] = {
    {Scene::None,          "none",           0, ""},
    {Scene::EmergencyStop, "emergency_stop", 1, "急停已触发，请检查并复位。"},
    {Scene::Fault,         "fault",          2, "车辆故障，请检查。"},
    {Scene::Charging,      "charging",       3, "车辆正在充电。"},
    {Scene::Obstacle,      "obstacle",       4, "前方有障碍物，请注意避让。"},
    {Scene::LowBattery,    "low_battery",    5, "电量低，请及时充电。"},
    {Scene::ManualMode,    "manual_mode",    6, "手动模式已开启。"},
    {Scene::AutomaticMode, "automatic_mode", 6, "自动模式已开启。"},
    {Scene::Running,       "running",        7, "车辆正在运行，请注意安全。"},
    {Scene::LeftTurn,      "turn_left",      7, "车辆左转，请注意安全。"},
    {Scene::RightTurn,     "turn_right",     7, "车辆右转，请注意安全。"},
    {Scene::Rotating,      "rotating",       7, "车辆正在旋转，请注意安全。"},
    {Scene::Sideways,      "sideways",       7, "车辆横移中，请注意安全。"}
};

// 同一场景下的故障类别顺序；数字设备编号/错误码在类别内排序。
struct FaultOrder { FaultKind kind; unsigned priority; };
const FaultOrder fault_order[] = {
    {FaultKind::Touch, 1}, {FaultKind::LimitOrCoupling, 2},
    {FaultKind::Device, 3}, {FaultKind::Parameter, 4},
    {FaultKind::NavigationCommunication, 5}, {FaultKind::UpperCommunication, 6},
    {FaultKind::Ipc, 7}, {FaultKind::Other, 8}
};

struct DeviceRule { std::uint32_t id; const char* name; };
const DeviceRule devices[] = {
    {1, "扩展板"}, {2, "陀螺仪"}, {3, "拉绳"}, {4, "电池"},
    {5, "避障器"}, {6, "预留设备"}, {7, "上装动作"}, {8, "视觉避障器"},
    {9, "驱动器1"}, {10, "驱动器2"}, {11, "驱动器3"}, {12, "驱动器4"},
    {13, "驱动器5"}, {14, "驱动器6"}, {15, "驱动器7"}, {16, "驱动器8"}
};

// 设备专有码用相同的 first_id/last_id；共用码用设备 ID 范围。
// 故障码是原始 16 位数值，不按 bit 拆分。未提供含义的码只播设备名和码值。
struct DeviceCodeRule {
    std::uint32_t first_id, last_id, code;
    const char* text;
};
const DeviceCodeRule device_codes[] = {
    {9, 16, 0xFFFF, "掉线，复位无法清除，需断电重启。"}
};

// type + bit，或 type + 可选 description，映射为稳定资源键和中文文案。
const ErrorRule errors[] = {
    {"COMMON_ERROR", nullptr, 0, FaultKind::Other, 0, "emergency_stop", "急停已触发，请检查并复位。", true},
    {"COMMON_ERROR", nullptr, 1, FaultKind::Touch, 1, "common/1", "触碰保护已触发，请检查。", false},
    {"COMMON_ERROR", nullptr, 2, FaultKind::Ipc, 16, "common/2", "工控机故障，请检查。", false},
    {"COMMON_ERROR", nullptr, 3, FaultKind::LimitOrCoupling, 3, "common/3", "举升限位异常，请检查。", false},
    {"COMMON_ERROR", nullptr, 4, FaultKind::LimitOrCoupling, 4, "common/4", "料车连接异常，请检查。", false},
    {"COMMON_ERROR", nullptr, 5, FaultKind::Parameter, 5, "common/5", "工控机参数匹配异常，请检查。", false},
    {"COMMON_ERROR", nullptr, 6, FaultKind::Other, 106, "common/6", "舵轮差速转向超时，请检查。", false},
    {"COMMON_WARNING", nullptr, 0, FaultKind::NavigationCommunication, 0, "warning/0", "导航通信端口掉线，请检查。", false},
    {"COMMON_WARNING", nullptr, 1, FaultKind::UpperCommunication, 1, "warning/1", "上装通信端口掉线，请检查。", false},
    {"COMMON_WARNING", "PING_TIMEOUT", -1, FaultKind::Other, 2, "network/ping_timeout", "网络连接超时，请检查。", false},
    {"ERROR_RCS_TASK", nullptr, -1, FaultKind::Other, 1, "task/rcs", "调度任务异常，请检查任务状态。", false},
    {"ERROR_QR_LOC_NOT_INIT", nullptr, -1, FaultKind::Ipc, 0, "ipc/ERROR_QR_LOC_NOT_INIT", "二维码定位尚未初始化，请检查。", false},
    {"ERROR_QR_LOC_INITING", nullptr, -1, FaultKind::Ipc, 1, "ipc/ERROR_QR_LOC_INITING", "二维码定位正在初始化，请稍候。", false},
    {"ERROR_QR_LOC_INITED", nullptr, -1, FaultKind::Ipc, 2, "ipc/ERROR_QR_LOC_INITED", "定位状态异常，请检查。", false},
    {"ERROR_QR_LOC_ODOM_ERROR", nullptr, -1, FaultKind::Ipc, 3, "ipc/ERROR_QR_LOC_ODOM_ERROR", "定位里程计异常，请检查。", false},
    {"ERROR_QR_LOC_QRCODE_LOST", nullptr, -1, FaultKind::Ipc, 4, "ipc/ERROR_QR_LOC_QRCODE_LOST", "二维码丢失，请检查定位。", false},
    {"ERROR_QR_LOC_ODOM_NO_DATA", nullptr, -1, FaultKind::Ipc, 5, "ipc/ERROR_QR_LOC_ODOM_NO_DATA", "里程计数据缺失，请检查。", false},
    {"ERROR_NAVI_DERAILMENT", nullptr, -1, FaultKind::Ipc, 6, "ipc/ERROR_NAVI_DERAILMENT", "车辆偏离导航路径，请检查。", false},
    {"ERROR_NAVI_OVER_POINT", nullptr, -1, FaultKind::Ipc, 7, "ipc/ERROR_NAVI_OVER_POINT", "车辆超过目标点，请检查。", false},
    {"ERROR_QR_LOC_NO_DATA", nullptr, -1, FaultKind::Ipc, 8, "ipc/ERROR_QR_LOC_NO_DATA", "二维码定位数据缺失，请检查。", false},
    {"ERROR_RCS_EXIST_ERROR", nullptr, -1, FaultKind::Ipc, 9, "ipc/ERROR_RCS_EXIST_ERROR", "调度系统异常，请检查。", false},
    {"ERROR_QR_LOC_ODOM_WHEEL_PULSE_NOT_UPDATE", nullptr, -1, FaultKind::Ipc, 10, "ipc/ERROR_QR_LOC_ODOM_WHEEL_PULSE_NOT_UPDATE", "编码器脉冲未更新，请检查。", false},
    {"ERROR_QR_LOC_CANNOT_READ_MORE", nullptr, -1, FaultKind::Ipc, 11, "ipc/ERROR_QR_LOC_CANNOT_READ_MORE", "二维码连续读取异常，请检查。", false},
    {"ERROR_QR_LOC_ROT_LOSS_CODE", nullptr, -1, FaultKind::Ipc, 12, "ipc/ERROR_QR_LOC_ROT_LOSS_CODE", "旋转过程中二维码丢失，请检查。", false},
    {"ERROR_NOT_FIND_QR", nullptr, -1, FaultKind::Ipc, 13, "ipc/ERROR_NOT_FIND_QR", "未找到二维码，请检查。", false},
    {"ERROR_NAVI_WHEEL_SLIP", nullptr, -1, FaultKind::Ipc, 14, "ipc/ERROR_NAVI_WHEEL_SLIP", "车辆车轮打滑，请检查。", false},
    {"ERROR_NAVI_ARRIVE_NOT_FINISH", nullptr, -1, FaultKind::Ipc, 15, "ipc/ERROR_NAVI_ARRIVE_NOT_FINISH", "车辆到点任务未完成，请检查。", false}
};
}

const SceneRule& RuleFor(Scene scene) {
    for (const auto& rule : scenes) if (rule.scene == scene) return rule;
    return scenes[0];
}
std::string SceneName(Scene scene) { return RuleFor(scene).key; }
const ErrorRule* FindNamedError(const std::string& type, const std::string& description) {
    for (const auto& rule : errors)
        if (rule.bit < 0 && type == rule.type && (!rule.description || description == rule.description)) return &rule;
    return nullptr;
}
const ErrorRule* FindBitError(const std::string& type, unsigned bit) {
    for (const auto& rule : errors)
        if (rule.bit == static_cast<int>(bit) && type == rule.type) return &rule;
    return nullptr;
}
unsigned FaultPriority(FaultKind kind) {
    for (const auto& item : fault_order) if (item.kind == kind) return item.priority;
    return 8;
}
const char* DeviceName(std::uint32_t id) {
    for (const auto& item : devices) if (item.id == id) return item.name;
    return nullptr;
}
const char* DeviceErrorText(std::uint32_t id, std::uint32_t code) {
    for (const auto& item : device_codes)
        if (id >= item.first_id && id <= item.last_id && code == item.code) return item.text;
    return nullptr;
}
} // namespace audioplay_detail
