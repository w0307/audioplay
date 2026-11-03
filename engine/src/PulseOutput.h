#pragma once
#include "AudioOutput.h"
#include <pulse/pulseaudio.h>

namespace audio_engine {
class PulseOutput final : public AudioOutput {
public:
    ~PulseOutput() override;
    void SetVolume(double volume) override { volume_ = volume; }
    bool MatchesFormat(int rate, int channels) const override { return stream_ && rate == rate_ && channels == channels_; }
    bool Open(int sample_rate, int channels, const CancelToken&, std::string& error) override;
    void Close() override;
    SpeakResult Write(const short*, std::size_t samples, const CancelToken&, std::string& error) override;
    SpeakResult Drain(const CancelToken&, std::string& error) override;
    bool Flush(std::string& error) override;
private:
    struct Completion { bool done = false; bool success = false; };
    static void Completed(pa_stream*, int success, void* data);
    bool Pump(std::string& error);
    bool Healthy(std::string& error) const;
    SpeakResult Wait(pa_operation*, Completion&, const CancelToken&, int timeout_ms, std::string& error);
    pa_mainloop* loop_ = nullptr;
    pa_context* context_ = nullptr;
    pa_stream* stream_ = nullptr;
    int rate_ = 0, channels_ = 1;
    double volume_ = 0.8;
};
}
