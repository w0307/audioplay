#pragma once
#include "VoiceTypes.h"

namespace audioplay_detail {

struct SceneRule {
    Scene scene;
    const char* key;
    unsigned priority; // 越小越优先；None 不参与选择
    const char* text;
};
const SceneRule& RuleFor(Scene scene);

struct ErrorRule {
    const char* type;
    const char* description; // nullptr：只按 type 匹配
    int bit;                 // -1：名称匹配；0..31：位图匹配
    FaultKind kind;
    unsigned order;
    const char* key;
    const char* text;
    bool emergency;
};
const ErrorRule* FindNamedError(const std::string& type, const std::string& description);
const ErrorRule* FindBitError(const std::string& type, unsigned bit);
unsigned FaultPriority(FaultKind kind);

// 故障设备 ID 与机构设备 ID 是不同字段。这里只维护 DEVICE_ERROR 的设备。
const char* DeviceName(std::uint32_t id);
// 返回接在设备名称后面的故障文案；未定义的码返回 nullptr。
const char* DeviceErrorText(std::uint32_t id, std::uint32_t code);

} // namespace audioplay_detail
