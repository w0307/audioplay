#pragma once
#include "PlaybackTypes.h"

namespace audio_engine {

// 引擎不调度任务、不循环、不决定优先级，也不读取业务 JSON。
// Initialize / Play / 析构由调用方串行使用；仅 CancelToken 支持跨线程写入。
class IPlaybackEngine {
public:
    virtual ~IPlaybackEngine() = default;
    virtual bool Initialize(const EngineOptions&, std::string& error) = 0;
    // 同步执行一次：可用文件优先，文件源失败时尝试一次 TTS。
    // 完成、取消或失败后返回。应用可在自己的工作线程调用。
    virtual PlaybackResult Play(const PlaybackRequest&, const CancelToken&) = 0;
};

// 返回 WAV/MP3 + Ekho 8.9 + PulseAudio 的默认实现。
std::unique_ptr<IPlaybackEngine> CreatePlaybackEngine();

} // namespace audio_engine
