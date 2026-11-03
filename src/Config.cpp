#include "Config.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <unistd.h>

namespace audioplay_detail {
using Json = nlohmann::json;
std::string Trim(const std::string& s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    return first == std::string::npos ? "" : s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}

namespace {
struct LoggingState {
    std::mutex output_mutex, throttle_mutex;
    std::map<std::string, TimePoint> times;
};
LoggingState& Logs() { static LoggingState state; return state; }
}
void InitializeLogging() { (void)Logs(); }
void Log(const std::string& message) {
    std::lock_guard<std::mutex> lock(Logs().output_mutex);
    std::cerr << "[AudioPlay] " << message << '\n';
}
void LogThrottled(const std::string& category, const std::string& message) {
    auto& state = Logs();
    std::lock_guard<std::mutex> lock(state.throttle_mutex);
    auto& times = state.times;
    const auto now = Clock::now();
    auto it = times.find(category);
    if (it == times.end() || now - it->second >= std::chrono::seconds(5)) {
        times[category] = now;
        Log(message);
    }
}
std::string ResolveConfigPath() {
    char buffer[4096];
    const ssize_t n = ::readlink("/proc/self/exe", buffer, sizeof(buffer));
    if (n <= 0 || n >= static_cast<ssize_t>(sizeof(buffer))) return "";
    const std::string exe(buffer, static_cast<std::size_t>(n));
    return exe.substr(0, exe.find_last_of('/')) + "/config/audioplay.json";
}
namespace {
void Require(bool valid, const std::string& message) { if (!valid) throw std::runtime_error(message); }
double Number(const Json& obj, const char* key, double fallback, double lo, double hi) {
    if (!obj.count(key)) return fallback;
    const double n = obj.at(key).get<double>();
    Require(std::isfinite(n) && n >= lo && n <= hi, std::string(key) + " out of range");
    return n;
}
long Integer(const Json& obj, const char* key, long fallback, long lo, long hi) {
    if (!obj.count(key)) return fallback;
    Require(obj.at(key).is_number_integer(), std::string(key) + " must be an integer");
    return static_cast<long>(Number(obj, key, static_cast<double>(fallback), static_cast<double>(lo), static_cast<double>(hi)));
}
std::string String(const Json& obj, const char* key) {
    return obj.value(key, std::string());
}
}
bool LoadJsonConfig(const std::string& path, RuntimeConfig& out, std::string& error) {
    try {
        std::ifstream stream(path.c_str(), std::ios::binary);
        Require(stream.good(), "Cannot read config: " + path);
        // 使用 JSON 库自身的语法/类型处理，配置只在启动时读取一次。
        const Json doc = Json::parse(stream);
        Require(doc.is_object(), "JSON root must be an object");
        for (const char* key : {"audio", "policy", "voices"})
            Require(doc.count(key) && doc.at(key).is_object(), std::string(key) + " must be an object");
        RuntimeConfig c;
        const Json& audio = doc.at("audio");
        c.enabled = audio.value("enabled", c.enabled);
        c.volume = Number(audio, "volume", c.volume, 0, 1);
        c.speed = static_cast<int>(Integer(audio, "speed", c.speed, -50, 100));
        if (audio.count("voice")) c.voice = String(audio, "voice");
        Require(c.voice == "Mandarin" || c.voice == "pinyin", "voice must be Mandarin or pinyin");
        const Json& p = doc.at("policy");
        c.repeat_interval = Milliseconds(Integer(p, "repeat_interval_ms", 1000, 0, 3600000));
        c.retry_interval = Milliseconds(Integer(p, "retry_interval_ms", 1000, 100, 3600000));
        // 兼容旧配置：0 仍关闭提示，正值只表示启用，不再限制句子时长。
        c.mode_prompt_enabled = p.count("mode_prompt_enabled") ?
            p.at("mode_prompt_enabled").get<bool>() : Integer(p, "mode_prompt_ms", 3000, 0, 3600000) > 0;
        c.obstacle_limit = Milliseconds(Integer(p, "obstacle_limit_ms", 30000, 0, 3600000));
        c.low_battery_confirm = Milliseconds(Integer(p, "low_battery_confirm_ms", 3000, 0, 3600000));
        c.input_timeout = Milliseconds(Integer(p, "input_timeout_ms", 3000, 100, 3600000));
        c.low_battery_trigger = Number(p, "low_battery_trigger_percent", 20, 0, 100);
        c.low_battery_recover = Number(p, "low_battery_recover_percent", 25, 0, 100);
        Require(c.low_battery_recover > c.low_battery_trigger, "Battery recovery must exceed trigger");
        c.linear_enter = Number(p, "linear_enter_m_s", 0.02, 0.000001, 100);
        c.linear_exit = Number(p, "linear_exit_m_s", 0.01, 0, c.linear_enter);
        c.angular_enter = Number(p, "angular_enter_rad_s", 0.10, 0.000001, 100);
        c.angular_exit = Number(p, "angular_exit_rad_s", 0.07, 0, c.angular_enter);
        Require(c.linear_exit < c.linear_enter && c.angular_exit < c.angular_enter, "Motion hysteresis requires exit < enter");
        const auto slash = path.find_last_of('/');
        const std::string base = slash == std::string::npos ? "." : path.substr(0, slash);
        for (auto it = doc.at("voices").begin(); it != doc.at("voices").end(); ++it) {
            Require(it.value().is_object(), "Voice entry must be an object: " + it.key());
            VoiceResource resource;
            resource.file_path = String(it.value(), "file");
            resource.fallback_text = Trim(String(it.value(), "text"));
            if (!resource.file_path.empty() && resource.file_path[0] != '/') resource.file_path = base + "/" + resource.file_path;
            c.voices.emplace(it.key(), resource);
        }
        out = std::move(c);
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}
}
