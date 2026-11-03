#pragma once

#include <memory>
#include "AudioState.h"

// C++11 状态播报库。后台选择场景并播放录音或 Ekho 合成语音。
// 对外只提供实例获取与状态提交；音源选择是库内部行为。
class AudioPlay final
{
public:
    enum class Result
    {
        Accepted,       // 输入已接收，不代表已出声或播放成功
        Disabled,       // 配置关闭
        NotReady,       // 初始化失败或播放服务不可用
        InvalidState,   // 保留枚举值以兼容旧调用方；3.2 不再整体拒绝状态
        ShuttingDown    // 对象正在退出
    };

    // 进程内唯一实例。首次调用自动读取 config/audioplay.json 并初始化。
    // 配置路径相对于主程序可执行文件所在目录，与工作目录无关。
    // 建议在 task_manager 初始化阶段调用，避免首次状态更新承担初始化耗时。
    static AudioPlay& GetInstance();

    // 异步更新完整播报所需状态，只保留最新快照；入参为普通 C++ 结构体。
    // 调用中提取并复制需要的字段，返回后不持有 state 的引用或指针。
    // 相同播报场景的重复更新不入队、不重启，不受 timestamp/位姿变化影响。
    // 内部先按规则选择场景，再查 JSON 资源映射：可用文件优先，文案合成兜底。
    // 本方法不解析 JSON、不读音频文件、不等待播放结束。
    Result Play(const AudioState& state);

    AudioPlay(const AudioPlay&) = delete;
    AudioPlay& operator=(const AudioPlay&) = delete;
    AudioPlay(AudioPlay&&) = delete;
    AudioPlay& operator=(AudioPlay&&) = delete;

private:
    AudioPlay();
    ~AudioPlay(); // 内部取消、通知并回收工作线程后释放合成器与输出资源

    class Impl;
    std::unique_ptr<Impl> impl_;
};

