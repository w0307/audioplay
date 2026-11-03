#pragma once
#include "VoiceTypes.h"
#include <map>

namespace audioplay_detail {
struct RuntimeConfig {
    bool enabled = true;
    double volume = 0.8;
    std::string voice = "Mandarin";
    int speed = 0;
    Milliseconds repeat_interval{1000};
    Milliseconds retry_interval{1000};
    bool mode_prompt_enabled = true;
    Milliseconds obstacle_limit{30000};
    Milliseconds low_battery_confirm{3000};
    Milliseconds input_timeout{3000};
    double low_battery_trigger = 20.0;
    double low_battery_recover = 25.0;
    double linear_enter = 0.02;
    double linear_exit = 0.01;
    double angular_enter = 0.10;
    double angular_exit = 0.07;
    std::map<std::string, VoiceResource> voices;
};
bool LoadJsonConfig(const std::string& path, RuntimeConfig& out, std::string& error);
std::string ResolveConfigPath();
std::string SceneName(Scene scene);
std::string Trim(const std::string& value);
void Log(const std::string& message);
void InitializeLogging();
void LogThrottled(const std::string& category, const std::string& message);
}
