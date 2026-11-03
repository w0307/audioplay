#pragma once
#include "AudioState.h"
#include "VoiceTypes.h"

namespace audioplay_detail {
class StateParser final {
public:
    VoiceSnapshot Parse(const AudioState& state) const;
};
}
