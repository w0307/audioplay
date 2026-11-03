#pragma once

#include <atomic>
#include <memory>
#include <string>

namespace audio_engine {

// 通用引擎参数；不包含场景、优先级、重复周期或业务阈值。
struct EngineOptions {
    double volume = 0.8;       // 0..1，统一作用于文件与 TTS
    int speed = 0;             // -50..100，仅作用于 Ekho TTS
    std::string voice = "Mandarin"; // 当前后端支持 Mandarin / pinyin
};

struct PlaybackRequest {
    std::string file_path;     // 空值直接 TTS；由调用方解析为实际路径
    std::string text;          // 文件不可用时的 UTF-8 备用文案
};

enum class PlaybackOutcome { Completed, Cancelled, Failed };
enum class ErrorStage { None, FileSource, TtsSource, Output, Request };

struct PlaybackResult {
    PlaybackOutcome outcome = PlaybackOutcome::Failed;
    ErrorStage error_stage = ErrorStage::None;
    bool used_tts_fallback = false;
    std::string message;
};

// 每次 Play 使用独立的非空令牌；调用方可在另一线程将其置为 true。
// 取消后不复位或复用；Play 返回代表该任务的引擎清理已完成。
using CancelToken = std::shared_ptr<std::atomic<bool>>;

} // namespace audio_engine
