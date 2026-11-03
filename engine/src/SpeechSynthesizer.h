#pragma once
#include "AudioOutput.h"
#include <audio_engine/PlaybackTypes.h>
namespace audio_engine {
class SpeechSynthesizer {
public:
    virtual ~SpeechSynthesizer() = default;
    virtual bool Initialize(const EngineOptions&, std::string& error) = 0;
    virtual PlaybackResult Speak(const std::string&, const CancelToken&) = 0;
};
std::unique_ptr<SpeechSynthesizer> MakeSpeechSynthesizer(AudioOutput&);
}
