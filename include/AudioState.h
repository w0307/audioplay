#pragma once

#include <limits>
#include <string>
#include <vector>

// 普通 C++ 数据类型，不依赖 ROS 消息头文件。
// 保留业务数据，不携带优先级、播报文案、音频文件路径等策略信息。
struct AudioError
{
    std::string type;         // COMMON_ERROR、DEVICE_ERROR、IPC 错误类型等
    std::string description;  // 原始位图/设备码/描述；DEVICE_ERROR 为 ID -> code，保留有符号原值
    std::string level;        // FATAL、WARNING 或其他值
};

struct AudioState
{
    // 空字符串表示未知/未提供，不能按“不等于 NONE”直接认定急停。
    // 合法输入包括 NONE、AUTOACK、MANUAL、REMOTE。
    std::string e_stop;

    bool field_violation = false; // 已上报的障碍物触发
    bool charging = false;
    bool driving = false;
    bool paused = false;          // 抑制普通运行提示，不是全局静音

    // 单位 %，合法范围 0..100。NaN 默认值表示调用方没有提供。
    // 范围合法不等于电池数据新鲜；现有 State 无独立 BMS 时效字段。
    double battery_percent = std::numeric_limits<double>::quiet_NaN();

    // 当前工程主要使用 AUTOMATIC、MANUAL；空值/不支持值不触发模式提示。
    std::string operating_mode;

    double vx_m_s = 0.0;
    double vy_m_s = 0.0;
    double omega_rad_s = 0.0;

    std::vector<AudioError> errors;
};

