#pragma once

#include "AudioState.h"
#include <vda5050_msgs/State.h>
#include <utility>

// 归属 AGV 主工程，不编进 AudioPlay 库。
// 这里只做字段复制，优先级、故障解析、去重、循环和资源选择仍在库内。
// 本头文件是接入示例，尚未在 ROS 工程中编译。
namespace agv_audio_integration {

inline AudioState ToAudioState(const vda5050_msgs::State& source)
{
    AudioState target;

    target.e_stop = source.safetyState.eStop;
    target.field_violation = source.safetyState.fieldViolation;
    target.charging = source.batteryState.charging;
    target.battery_percent = source.batteryState.batteryCharge;
    target.operating_mode = source.operatingMode;
    target.driving = source.driving;
    target.paused = source.paused;
    target.vx_m_s = source.velocity.vx;
    target.vy_m_s = source.velocity.vy;
    target.omega_rad_s = source.velocity.omega;

    target.errors.reserve(source.errors.size());
    for (const auto& item : source.errors)
    {
        AudioError error;
        error.type = item.errorType;
        error.description = item.errorDescription;
        error.level = item.errorLevel;
        target.errors.push_back(std::move(error));
    }

    return target;
}

} // namespace agv_audio_integration

