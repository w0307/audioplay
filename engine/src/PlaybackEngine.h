#pragma once
#include <audio_engine/PlaybackEngine.h>
#include "SpeechSynthesizer.h"
namespace audio_engine {
class PlaybackEngine final : public IPlaybackEngine {
public:
    PlaybackEngine(std::unique_ptr<AudioOutput>, std::unique_ptr<SpeechSynthesizer>);
    bool Initialize(const EngineOptions&, std::string& error) override;
    PlaybackResult Play(const PlaybackRequest&, const CancelToken&) override;
private:
    PlaybackResult RunOnce(const PlaybackRequest&, const CancelToken&);
    PlaybackResult DecodeAndPlayFile(const std::string&, const CancelToken&);
    PlaybackResult DecodeAndPlayWav(int fd, const std::string&, const CancelToken&);
    PlaybackResult DecodeAndPlayMp3(int fd, const std::string&, const CancelToken&);
    std::unique_ptr<AudioOutput> output_; // 比 speech_ 后析构
    std::unique_ptr<SpeechSynthesizer> speech_;
    bool initialized_ = false;
};
}
