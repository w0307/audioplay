#pragma once
#include <audio_engine/PlaybackTypes.h>
namespace audio_engine {
using SpeakResult = PlaybackOutcome;
class AudioOutput {
public:
    virtual ~AudioOutput() = default;
    virtual void SetVolume(double volume) = 0;
    virtual bool MatchesFormat(int rate, int channels) const = 0;
    virtual bool Open(int rate, int channels, const CancelToken&, std::string& error) = 0;
    virtual SpeakResult Write(const short* samples, std::size_t count, const CancelToken&, std::string& error) = 0;
    virtual SpeakResult Drain(const CancelToken&, std::string& error) = 0;
    virtual bool Flush(std::string& error) = 0;
    virtual void Close() = 0;
};
std::unique_ptr<AudioOutput> MakeAudioOutput();
}
