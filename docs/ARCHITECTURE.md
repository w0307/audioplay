# 3.6 映射表业务与独立播放引擎

## 模块边界

| 模块 | 源码位置 | 目标 | 职责 |
| --- | --- | --- | --- |
| ROS 适配 | integration/ros | 宿主工程中的头文件 | 复制现有 State 字段，不选择播报 |
| 车辆播报业务 | include、src、config | AudioPlay::audioplay | 状态解析、故障映射、优先级、计时、去重、最新状态、JSON 和资源匹配 |
| 通用播放引擎 | engine | AudioEngine::audio_engine | 一次播放、WAV/MP3 解码、TTS、音量、输出、取消与结果返回 |

依赖单向：业务层依赖通用引擎接口。引擎头文件不包含业务头文件，构建不需要业务源码或 nlohmann/json，engine 整个目录可独立交付。库内部类型分别位于 audioplay_detail 和 audio_engine 命名空间。

主工程继续只调用 `AudioPlay::GetInstance().Play(ToAudioState(state))`。AudioPlay 是车辆业务入口；它和通用引擎的 `IPlaybackEngine::Play` 是两个不同层次的接口。

## 业务映射

StateParser 提取状态和解析错误；AnnouncementTable 集中维护场景、错误资源键、文案和优先级；VoicePolicy 生成候选并选择最高优先级项；VoiceCatalog 查询已加载的 JSON 资源。详细示例见 MAPPING_FLOW.md。

## 传递的数据

业务调度层将已选择的资源转换为：

```cpp
audio_engine::PlaybackRequest request;
request.file_path = job.resource.file_path;
request.text = job.resource.fallback_text;
const auto result = engine->Play(request, cancel);
```

任务编号、场景、故障码、场景版本、优先级、循环间隔等信息均留在业务层。引擎只读取文件路径、文案及取消令牌。

JSON 也留在业务层：Config 解析 audio、policy、voices，VoiceCatalog 根据场景选出一对文件路径与备用文案。业务将 volume、speed、voice 单独转换为 EngineOptions；不把整个 RuntimeConfig 传给引擎。

## 抢占与完成

1. 主工程调用 Play，业务保存最新完整状态，并在接收锁内观察模式变化、更新最新模式事件编号。
2. 业务控制线程形成候选，按 AnnouncementTable 的优先级选出一个场景；决策变化时将旧令牌置为 true。
3. 播放线程正在引擎中执行一次任务。引擎观察取消并清理缓冲；Play 返回 Cancelled。
4. 业务播放线程附加原任务编号和场景版本，通知业务控制线程。
5. 业务确认旧任务已结束后，按最新状态下发下一次播放；每次使用新的取消令牌。

正常完成后是否再播、何时再播，失败后是否重试，均由业务调度层决定。引擎不保存业务历史，不循环，也不比较优先级。控制线程先处理完成结果，再评估最新状态；模式任务 Completed 时由 VoicePolicy 清除对应事件，Cancelled/Failed 不清除。事件编号防止旧模式任务清除后来的同名模式事件。

优先级：急停 > 故障 > 充电 > 障碍 > 低电 > 模式 > 运动；故障只选择最高一项。模式提示只播一次，等待整句完成，不使用时长窗口；同一模式的周期更新不再触发。首次有效模式仍提示一次，被高优先级打断的当前模式事件恢复后重播，快速切换只保留最新事件。

障碍 30 秒累计选中窗口、低电确认/回差、持续状态输入超时仍在 VoicePolicy 内。输入超时抑制充电和运动，不截断已经触发的模式句子。空值/不支持的模式会暂停模式候选；恢复相同有效模式不会新建事件。

## 文件与合成

录音和 TTS 的选择保留在通用引擎，因为它只依据音源是否可用，不需要车辆知识。有效 MP3 或 PCM WAV 优先；文件源失败时尝试一次 TTS。被取消的任务不合成备用文案，输出错误直接失败。

文件播放不按固定时长截断：引擎读取完整文件并等待输出缓冲播放完毕，才返回 Completed。业务层据此结束一次性模式事件，或开始持续场景的句间隔；高优先级取消和障碍 30 秒累计窗口仍可提前结束播放。MP3 实现使用已有 mpg123 依赖，不向业务层添加格式或时长字段。

业务仍负责相对路径基准：JSON 中的 file 相对于 JSON 所在目录解析；引擎接收处理后的路径。默认状态 demo 的录音放在 build/bin/audio，JSON 位于 build/bin/config/audioplay.json。

## 构建与部署

根目录构建会同时生成 libaudioplay.so 和 libaudio_engine.so。使用源代码接入时，原有 add_subdirectory 和 AudioPlay::audioplay 链接写法继续适用；CMake 自动建立引擎依赖。

部署共享库时必须同时部署两套库及 SONAME 链接，推荐使用 cmake --install。安装后的 libaudioplay 使用同目录的 libaudio_engine。引擎的独立编译、安装和通用调用示例见 engine/README.md。

如需替换音频后端，实现 IPlaybackEngine，并在业务组合入口 AudioPlay.cpp 中注入新实现；StateParser 和 VoicePolicy 无须修改。外部 State 接口没有增加文件、文本或停止重载。

## 本包录音与设备资源匹配

74个MP3位于audio；audioplay.json是运行配置，audio_manifest.json仅为素材清单。设备故障资源按精确故障码、设备通配、通用fault顺序选取。通配仅决定声音，不改变错误身份、排序或调度。Scene::Sideways根据已有速度字段识别纯横移；独立引擎没有改动。未有明确State来源的录音只存档，不制造控制状态。
