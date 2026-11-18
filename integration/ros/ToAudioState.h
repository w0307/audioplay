#pragma once

#include "AudioState.h"
#include <vda5050_msgs/State.h>
#include <cstdint>
#include <utility>

// 归属 AGV 主工程，不编进 AudioPlay 库。
// 这里只做字段复制和 HMI 位图拆分；播报策略和播放取消仍在库内。
// 本头文件是接入示例，尚未在 ROS 工程中编译。
namespace agv_audio_integration {

// disabled_functions 来自 enable_mcu_sensor_：bit0 屏蔽避障提示，bit2 屏蔽全部语音。
inline AudioState ToAudioState(const vda5050_msgs::State& source, std::uint8_t disabled_functions = 0)
{
    AudioState target;
    target.obstacle_disabled = (disabled_functions & 0x01u) != 0;
    target.voice_disabled = (disabled_functions & 0x04u) != 0;

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

