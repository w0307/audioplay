#include "StateParser.h"
#include "AnnouncementTable.h"
#include "Config.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <tuple>

namespace audioplay_detail {
namespace {
bool Uint(const std::string& input, std::uint32_t& out, unsigned base = 10) {
    const auto text = Trim(input);
    if (text.empty()) return false;
    std::uint64_t value = 0;
    for (char c : text) {
        unsigned digit;
        if (c >= '0' && c <= '9') digit = static_cast<unsigned>(c - '0');
        else if (base == 16 && c >= 'a' && c <= 'f') digit = static_cast<unsigned>(c - 'a' + 10);
        else if (base == 16 && c >= 'A' && c <= 'F') digit = static_cast<unsigned>(c - 'A' + 10);
        else return false;
        if (digit >= base) return false;
        value = value * base + digit;
        if (value > std::numeric_limits<std::uint32_t>::max()) return false;
    }
    out = static_cast<std::uint32_t>(value); return true;
}
// 主工程以 int8_t / int16_t 转成十进制字符串；恢复原始位模式。
// 例如设备故障码 -1、65535、0xFFFF 都归一到 65535。
bool DeviceRawValue(const std::string& input, unsigned bits, std::uint32_t& out) {
    auto text = Trim(input);
    const bool negative = !text.empty() && text[0] == '-';
    if (negative) text.erase(0, 1);
    unsigned base = 10;
    if (text.size() >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16; text.erase(0, 2);
    }
    std::uint32_t value = 0;
    const std::uint32_t range = std::uint32_t(1) << bits;
    if (!Uint(text, value, base) || value >= range || (negative && value > range / 2)) return false;
    out = (negative ? 0u - value : value) & (range - 1);
    return true;
}
void Add(VoiceSnapshot& state, FaultKind kind, unsigned order, const std::string& key, const std::string& text) {
    Fault fault; fault.kind = kind; fault.order = order;
    fault.stable_key = key; fault.fallback_text = text; state.faults.push_back(std::move(fault));
}
void Apply(VoiceSnapshot& state, const ErrorRule& rule) {
    if (rule.emergency) state.emergency_stop = true;
    else Add(state, rule.kind, rule.order, rule.key, rule.text);
}
void ParseError(const AudioError& error, VoiceSnapshot& state) {
    if (const auto* rule = FindNamedError(error.type, error.description)) { Apply(state, *rule); return; }
    if (error.type == "COMMON_ERROR" || error.type == "COMMON_WARNING") {
        std::uint32_t bits = 0;
        if (!Uint(error.description, bits)) {
            Add(state, FaultKind::Other, 0, "input/" + error.type, "车辆故障信息无法解析，请检查。"); return;
        }
        for (unsigned bit = 0; bit < 32; ++bit) if (bits & (std::uint32_t(1) << bit)) {
            if (const auto* rule = FindBitError(error.type, bit)) Apply(state, *rule);
            else {
                const bool warning = error.type == "COMMON_WARNING";
                Add(state, FaultKind::Other, bit + (warning ? 200 : 100),
                    std::string(warning ? "warning/" : "common/") + std::to_string(bit),
                    warning ? "车辆告警，请检查。" : "车辆通用故障，请检查。");
            }
        }
    } else if (error.type == "DEVICE_ERROR") {
        const auto arrow = error.description.find("->");
        std::uint32_t id = 0, code = 0;
        if (arrow != std::string::npos && DeviceRawValue(error.description.substr(0, arrow), 8, id)) {
            if (id == 0) return; // 协议中 0 表示无故障设备。
            if (!DeviceRawValue(error.description.substr(arrow + 2), 16, code)) {
                Add(state, FaultKind::Device, 0, "device/unknown", "车体设备故障，请检查。"); return;
            }
            const char* name = DeviceName(id);
            const char* mapped = DeviceErrorText(id, code);
            const std::string text = name ?
                std::string(name) + (mapped ? mapped : "故障，故障码" + std::to_string(code) + "。") :
                "车体设备故障，设备编号" + std::to_string(id) + "，故障码" + std::to_string(code) + "。";
            Add(state, FaultKind::Device, 0, "device/" + std::to_string(id) + "/" + std::to_string(code),
                text);
            state.faults.back().device_id = id; state.faults.back().code = code;
        } else Add(state, FaultKind::Device, 0, "device/unknown", "车体设备故障，请检查。");
    } else {
        const bool warning = error.level == "WARNING";
        Add(state, FaultKind::Other, warning ? 400 : 300, "other/" + (error.type.empty() ? "unknown" : error.type),
            warning ? "车辆告警，请检查。" : "车辆故障，请检查。");
    }
}
}

VoiceSnapshot StateParser::Parse(const AudioState& in) const {
    VoiceSnapshot state;
    state.emergency_stop = in.e_stop == "AUTOACK" || in.e_stop == "MANUAL" || in.e_stop == "REMOTE";
    state.obstacle = in.field_violation; state.charging = in.charging;
    state.driving = in.driving; state.paused = in.paused;
    state.battery_valid = std::isfinite(in.battery_percent) && in.battery_percent >= 0 && in.battery_percent <= 100;
    state.battery_percent = in.battery_percent;
    if (in.operating_mode == "AUTOMATIC") state.mode = OperatingMode::Automatic;
    else if (in.operating_mode == "MANUAL") state.mode = OperatingMode::Manual;
    state.motion_valid = std::isfinite(in.vx_m_s) && std::isfinite(in.vy_m_s) && std::isfinite(in.omega_rad_s);
    state.vx_m_s = in.vx_m_s; state.vy_m_s = in.vy_m_s; state.omega_rad_s = in.omega_rad_s;
    for (const auto& error : in.errors) ParseError(error, state);

    std::sort(state.faults.begin(), state.faults.end(), [](const Fault& a, const Fault& b) {
        return std::make_tuple(FaultPriority(a.kind), a.order, a.device_id, a.code, a.stable_key) <
               std::make_tuple(FaultPriority(b.kind), b.order, b.device_id, b.code, b.stable_key);
    });
    std::set<std::string> seen;
    state.faults.erase(std::remove_if(state.faults.begin(), state.faults.end(), [&seen](const Fault& fault) {
        return !seen.insert(fault.stable_key).second;
    }), state.faults.end());
    return state;
}
} // namespace audioplay_detail
