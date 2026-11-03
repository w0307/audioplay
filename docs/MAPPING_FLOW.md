# 映射表驱动的播报流程

## 一条业务路径

主工程继续提交完整状态：

```cpp
AudioPlay::GetInstance().Play(agv_audio_integration::ToAudioState(state));
```

内部顺序为：提取 State → 查错误/场景映射 → 形成播报候选 → 比较优先级 → 查 JSON 中对应资源 → 通用引擎执行一次播放。

主线程只提交状态。后面的选优、重复间隔和调用引擎仍由业务工作线程完成。

## 映射表在哪里

统一在 `src/AnnouncementTable.cpp`：

```cpp
const SceneRule scenes[] = {
    // 场景                    JSON key          优先级   默认文案
    {Scene::EmergencyStop, "emergency_stop",      1, "急停已触发，请检查并复位。"},
    {Scene::Fault,         "fault",               2, "车辆故障，请检查。"},
    {Scene::Charging,      "charging",            3, "车辆正在充电。"},
    // 其余场景见源文件
};
```

priority 数值越小，优先级越高。VoicePolicy 先生成所有当前满足条件的候选，再比较 priority，故障内部按 fault_order 的类别顺序、具体条目顺序和设备编号/故障码排序。

errors 表按 `errorType + bit` 或 `errorType + 可选 errorDescription` 匹配，得到稳定资源键和默认文案。设备错误的 `ID -> code` 保留简单数字拆解，因为设备编号和故障码会随输入变化。

已有错误类型只修改文案/录音时，直接改 JSON 即可。新增需要特殊解析的输入格式时修改 StateParser；新增已知类型映射或调整优先级时修改 AnnouncementTable。新增 State 条件则在 VoicePolicy 的候选生成处加入对应条件。

## 示例

State 同时包含：

- `COMMON_ERROR / "10"`：触碰、限位。
- `DEVICE_ERROR / "2 -> 9"`：设备二故障码九。
- `charging=true`：充电。

业务先选故障场景，再按故障顺序选触碰，生成资源键 `fault:common/1`。在现有 JSON 的 voices 中加入：

```json
"fault:common/1": {
  "file": "../audio/044.mp3",
  "text": "触碰保护已触发，请检查。"
}
```

VoiceCatalog 查到该条目，解析相对路径，再交给通用引擎：

```cpp
audio_engine::PlaybackRequest request;
request.file_path = resource.file_path;
request.text = resource.fallback_text;
auto result = engine->Play(request, cancel);
```

044.mp3 可用时播放录音；不存在或无法解码时合成该 text。没有具体故障配置时，设备故障先查 fault:device/ID/*，然后查通用 fault 条目；都未配置时使用映射表文案。资源选择按条目成对取文件与文案，不会把两个故障的资源混合。

录音不存在不会降低故障优先级，也不会跳去播低优先级场景。抢占由业务层设置取消令牌；引擎清理返回后，业务选择最新场景。

## 精简后的检查边界

- StateParser 直接读取约定字段并遍历 errors；不再构造 warnings 数组、检查字段长度或限制错误条数。保留数字转换、有效电量/速度判断，避免误判所需状态。
- Config 只在启动时调用 JSON 库读取配置，保留基本类型/参数范围处理；移除额外的重复键、深度、文件大小、资源数量及未知键告警检查。重复键采用末值，未知键忽略。
- VoicePolicy 处理候选与优先级，不增加异常分支。原有去重、计时和最新状态调度继续保留。
- 播放前不额外调用文件存在性检查；引擎直接尝试读取文件，失败后按约定回退 TTS。实际解码、取消和音频输出错误仍由引擎处理。
- 初始化和线程/引擎边界保留异常收口，业务映射与正常的“没有录音”路径不依赖异常控制流程。

外部 State 接口、JSON 结构、录音目录和独立引擎 API 不变。`Result::InvalidState` 保留数值兼容，但 3.2 不再整体拒绝状态；缺失或不可用的字段只影响对应候选。


3.3 补充：设备名称与设备专用错误码也在 AnnouncementTable.cpp 中维护。DEVICE_ERROR 原始 8/16 位数值先归一化再查表，具体映射与配置键见 DEVICE_MAPPING.md。

## 3.4 一次性模式提示

接收 State 时，PlaybackScheduler 在接收锁内调用 VoicePolicy::ObserveMode；只有首次有效模式或有效模式发生变化才创建新的待播事件。相同模式的周期更新不修改该事件。VoicePolicy 按既有优先级将待播事件作为 manual_mode / automatic_mode 候选，VoiceCatalog 查询同名 JSON 条目，文件和 TTS 继续共用一条引擎路径。

模式决策的 repeat=false，任务正常返回 Completed 后，调度器把 SceneKey 交给 VoicePolicy::OnPlaybackCompleted。只有事件编号匹配才清除待播标记，随后重新选优，因此模式句子播完不会继续占用优先级。取消和失败不清除；快速切换会替换待播事件，不累计历史队列。该逻辑全部在业务层，引擎接口和实现不包含模式信息。

## 3.6 录音接入

运行配置已指向包内实际MP3；设备ID与故障码仍先归一化，再查精确/设备通用资源。全部音频目录和待绑定项目见 AUDIO_INVENTORY.md，完整错误与录音关系见 FAULT_MAPPING.md。
