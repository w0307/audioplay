# AudioPlay 3.6：完整录音包与 State 故障映射

本包保留已经确认的 R3 状态接口：业务方只调用 `GetInstance()` 和 `Play(const AudioState&)`。库根据当前状态选择唯一场景、处理优先级和取消，并优先播放该场景的 MP3 或 PCM WAV；没有可用文件时使用 Ekho 8.9 合成文案。配置使用随包提供的 nlohmann/json 3.11.3，公共头文件没有 ROS、JSON、Ekho 或 PulseAudio 依赖。

交付源码采用 C++11，目标环境为 Ubuntu 20.04 / ARM64。构建和自动测试结果见 VALIDATION.md。包内含用户提供的74个MP3录音；不含目标机二进制、Ekho源码或TTS语音数据。

## 3.6 本次录音接入

- `audio/001.mp3` 等包含全部74个原始音频，只重命名，没有裁剪、转码或改变音量。时长为1.680～62.100秒。
- `config/audioplay.json` 已填好55项运行资源：40个录音接入现有State，2个为可替换录音，32个等待明确触发字段/错误码；全部文件都保留。
- `config/audio_manifest.json` / `docs/AUDIO_INVENTORY.md` 记录所有原文件名、时长、用途及未绑定原因。manifest是查阅清单，运行时只读取audioplay.json；台词标签根据文件名整理，未逐句听写。
- `docs/FAULT_MAPPING.md` 补齐通用错误、IPC错误、设备ID、录音路径和TTS文案。录音编号不是错误码，不按103+IPC枚举顺序自动绑定。
- 设备资源支持 `fault:device/ID/*`；先查具体码，再查该设备，再查fault。不同设备码仍是不同故障，不因共享录音而合并。
- 利用既有vx/vy/omega增加纯横移候选 `sideways`，使用009，与其他运动提示同为优先级7。没有增加公开State字段或Play接口。
- demo构建/安装会同时部署config和audio；ROS接入片段与补丁也已更新录音部署。
- **005障碍录音长50.076秒，现有累计选中窗口仍为30秒，届满会取消当前文件。** 未按录音长度擅自改变业务规则；其他持续录音自然结束后再间隔1秒重播，模式提示完整一次。

## 模块边界

| 模块 | 目录 / CMake 目标 | 职责 |
| --- | --- | --- |
| 车辆播报业务 | include、src、config / AudioPlay::audioplay | State/errors 解析、场景、优先级、去重、循环、计时、JSON 和资源映射 |
| 通用播放引擎 | engine / AudioEngine::audio_engine | 接收文件路径与备用文案，执行一次录音/TTS 播放、取消并返回结果 |

业务依赖引擎接口；引擎不包含 AudioState、故障码、场景或 RuntimeConfig。engine 目录可独立复制和构建，不需要 ROS 或 nlohmann/json。内部通用请求只有 `PlaybackRequest{file_path, text}`；任务编号与场景版本留在业务调度层。

业务调用方式不变，JSON 配置兼容旧版。根目录构建生成 **libaudioplay.so 和 libaudio_engine.so 两个共享库**，部署时需要同时安装；源码接入仍然链接 AudioPlay::audioplay。详细边界见 docs/ARCHITECTURE.md，独立引擎用法见 engine/README.md。

## 3.5 录音格式与时长

- 通用引擎支持 MP3（CBR/VBR、单声道/双声道）和原有 PCM WAV，可在 JSON 中混用；按实际内容识别格式。
- 使用项目已有的 mpg123 依赖解码 MP3，按块输出 PCM，不整段读入内存，也不启动外部播放器。
- 不设置每个录音的固定时长；读完全部音频并等待输出缓冲播放完成后才返回 Completed。录音里的前后静音也会正常保留。
- 模式提示保持一次完整播放；持续故障等场景在本句播完后再间隔 repeat_interval_ms 重播。高优先级抢占、场景解除及障碍累计窗口仍由业务层决定。
- file 路径应包含实际文件名与扩展名，不自动寻找同名 MP3/WAV。无可用文件时按原规则回退 TTS。

录音目录、混用配置、不同长度的处理和障碍 30 秒窗口说明见 docs/RECORDINGS.md。

## 3.4 模式提示调整

- 自动切换到手动时播报“手动模式已开启。”；手动切换到自动时播报“自动模式已开启。”。首次收到有效模式仍提示一次。
- 模式提示不循环、不受原 3 秒窗口限制，收到引擎 Completed 后结束并释放优先级。持续提交同一模式不再触发；没有新的 State 也不会因输入超时截断已经触发的这句话。
- 更高优先级仍可抢占；Cancelled/Failed 不算完成。抢占解除后重播尚未完成的当前模式提示，失败按原重试间隔重试。
- 连续切换只保留最新模式事件，旧任务的完成通知不能清除新事件；空值/不支持的模式暂停模式候选，但不会改写上一次有效模式或制造重复切换。
- 新配置使用 `policy.mode_prompt_enabled`。旧 `mode_prompt_ms=0` 仍表示关闭；正值表示启用，不再作为播放时长。两者同时存在时新开关优先。
- 录音优先、无可用录音时 TTS 的规则相同；已配置的自定义 text 和录音内容按配置使用。

## 3.3 设备故障补充

- 按协议补充故障设备 ID 1～16 的名称；0 表示无故障设备，6 为预留。
- DEVICE_ERROR 的设备 ID 和故障码分别按原始 8 位、16 位数值解析，兼容主工程 int8_t/int16_t 产生的负数及十六进制表示。
- 驱动器 ID 9～16 的故障码 `-1`、`65535`、`0xFFFF` 统一为 65535，播报“驱动器N掉线，复位无法清除，需断电重启。”；不将此码套用于其他设备。
- 未定义设备码使用“设备名＋故障码”；已知 ID 不再只读设备编号。未知 ID 使用通用设备故障文案。设备故障码是原值传送，不按 bit 拆分。
- COMMON_ERROR 补充 bit2 IPC 汇总及 bit6 舵轮差速转向超时，bit3/bit5 文案校正为举升限位/工控机参数匹配异常。具体 IPC 错误优先于汇总提示；bit6 保持原“其他故障”的排序位置。
- 3.3曾预留驱动器掉线、IPC汇总、转向超时的WAV路径；3.6已按本次实际素材替换。驱动器7/8和转向超时未提供匹配录音，继续TTS。

设备号、配置键和上位机有符号数说明见 docs/DEVICE_MAPPING.md。公开接口、播放引擎和原有抢占机制保持不变。

## 主流程

State → StateParser → 播报候选 → 按映射表 priority 选中一项 → VoiceCatalog 查 JSON 资源 → 通用引擎播放。

- `src/AnnouncementTable.cpp` 的 scenes 表维护场景 key、优先级和默认文案；errors 表维护错误名称/位图映射；devices/device_codes 表维护设备名和设备码文案；fault_order 维护故障类别顺序。
- StateParser 只提取用到的字段、转换十进制位图和设备原始数值；不逐帧检查数组/字符串长度或构造输入告警列表。未知错误仍有通用提示，数字转换失败仍返回通用故障文案。
- VoicePolicy 先形成候选，再比较表中的数值优先级；优先级不依靠 if/else 的排列顺序。模式提示按事件完成，障碍保留累计计时。
- JSON 在初始化时读取一次，播放时查内存中的 voices 表，不重复读文件。查到 file 路径就交给引擎打开；文件不可用则用 text 合成，不再额外做一遍文件存在性预检查。
- 文件解码、播放取消、线程同步和初始化失败处理保留在对应边界，不在每个业务步骤重复校验。

具体维护位置和示例见 docs/MAPPING_FLOW.md。

## 公开接口

```cpp
#include "AudioPlay.h"

auto& audio = AudioPlay::GetInstance(); // 在主程序初始化阶段调用

// 周期提交同一辆车的完整快照
AudioState state;
state.e_stop = "NONE";
state.battery_percent = 80.0;
state.operating_mode = "AUTOMATIC";
state.driving = true;
state.vx_m_s = 0.3;
const auto result = audio.Play(state);
```

状态必须是完整快照。Play 返回后不保留入参引用；调用期间源对象须保持稳定。库支持并发提交，顺序以内部接收顺序为准；实际主工程建议只有一个完整状态更新路径。相同场景的周期更新不会入队或重启当前句。

| Result | 含义 |
| --- | --- |
| Accepted | 状态已接收；不等于已经出声或播放成功 |
| Disabled | JSON 中 enabled=false |
| NotReady | 配置、后端契约或线程初始化失败 |
| InvalidState | 保留枚举值兼容旧代码，3.2 不再整体拒绝 State；不可用的电量/速度字段仅不参与对应判断 |
| ShuttingDown | 对象仍存活，但内部已关闭接收 |

运行错误和文件回退写入 stderr，前缀为 `[AudioPlay]`，重复运行错误每类最多 5 秒输出一次。车辆业务入口没有公开文本、优先级、文件播放、队列、停止或配置重载接口；独立引擎提供通用的一次播放接口。进程正常退出时析构会取消任务并回收线程；宿主必须先停止提交者，不得从信号处理函数直接析构播放器。

## 构建

目标机开发依赖：

```bash
sudo apt install build-essential cmake python3 autoconf automake libtool pkg-config \
  libsndfile1-dev libespeak-ng-dev espeak-ng-data libpulse-dev libmpg123-dev
```

使用现有 Ekho **8.9 源码及编译目录**。仅有 `/opt/ekho-8.9/bin/ekho` 可执行文件不够；本库链接 `libekho.a`，需要相同源码的内部头文件和 config.h。

```bash
# 在本包根目录执行，把路径改为实际源码位置。
export EKHO_SRC=/home/hx/audioplay/third_party/ekho-8.9
export EKHO_SRC=/home/hx/audioplay_state_v3_6/third_party/ekho-8.9
python3 scripts/patch_ekho89.py "$EKHO_SRC"
```

脚本是之前验证的 v2 取消补丁，支持官方 8.9、v1 补丁和重复执行 v2。首次打补丁会保留 `.before-audioplay-v2` 备份。补丁保证缓存清理后再发送完成通知；不能只替换 AudioPlay 而继续使用未打补丁的静态库。

若已应用同一 v2 补丁且 libekho.a 已重新编译，可复用。否则在 Ekho 源码目录执行：

```bash
autoreconf -fi
# 已构建过该源码时先执行 make clean
./configure CFLAGS="-O2 -fPIC" CXXFLAGS="-O2 -fPIC"
make -j1
```

沿用精简 Ekho 构建，不额外启用 Festival、MusicXML、Friso、SoundTouch、LAME 或 SOXR。完整语音数据必须已准备好；复用现有能正常说中文的普通话包：

```bash
export EKHO_DATA_PATH=/opt/ekho-8.9/share/ekho-data
export EKHO_DATA_PATH=/home/hx/audioplay/third_party/ekho-8.9/ekho-data
```

确认该目录包含有效的 `zh.dict`、`pinyin.index` 和 `pinyin.voice`。eSpeak-ng 的数据也必须安装完整；使用自定义安装目录时设置 `ESPEAK_DATA_PATH` 指向实际的 `espeak-ng-data` 目录。Ekho 上游在缺失部分 eSpeak 数据时可能退出进程，这属于第三方引擎部署前置条件。

回到本包根目录：

```bash
cmake -S . -B build \
  -DEKHO_SOURCE_DIR="$EKHO_SRC" \
  -DEKHO_BUILD_DIR="$EKHO_SRC" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build -j1
./build/bin/audioplay_state_demo
```

演示持续约 22 秒：自动模式提示、运行、障碍、急停、恢复障碍、左转、充电、停止运动。每 100 ms 提交快照；按 Ctrl+C 正常结束。示例配置已绑定本包MP3，构建自动复制到 build/bin/audio；文件不可用时自动TTS。程序读取的JSON为 build/bin/config/audioplay.json；相对录音路径以该 JSON 所在目录为基准。需在目标机可用的 PulseAudio 用户会话中运行。

若安装到独立目录：

```bash
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/opt/audioplay-v3
cmake --build build -j1
sudo cmake --install build
```

独立宿主可用 `find_package(AudioPlay 3 CONFIG REQUIRED)` 和 `AudioPlay::audioplay`；源码集成使用 `add_subdirectory`。3.2 保留既有公开 State 接口；源码接入时统一重新构建，并部署新增的引擎共享库。

## 接入 2d_amr_vda5050

库接收普通 C++ `AudioState`，主工程用 `integration/ros/ToAudioState.h` 复制现有 ROS 消息中的必要字段：


```cpp
#include "AudioPlay.h"
#include "ToAudioState.h"

// TaskManager 初始化阶段
(void)AudioPlay::GetInstance();

// PublishState 中，state 所有字段构造完成后
const auto result = AudioPlay::GetInstance().Play(
    agv_audio_integration::ToAudioState(state));
```


无需新增 Topic、订阅节点或 JSON 状态序列化。库内完成故障解析和播报选择。当前 PublishState 默认约 5 Hz，播报识别延迟会受这一周期限制。

将本包完整目录复制到 `task_manager/third_party/audioplay`。`integration/ros/CMakeLists.snippet.cmake` 已按现有工程中的 `task_manager` 库和 `task_manager_node` 可执行目标编写；链接沿用原工程的非关键字签名，避免混用 PRIVATE 与原 target_link_libraries 形式。

随包的 `integration/ros/task_manager_audio.patch` 针对本会话提供的原始工程，只改 task_manager 的 CMakeLists.txt 和 task_manager.cpp，加入链接、配置/音频部署及 Play 调用。它不修改 MCU、车辆控制逻辑或其他构建问题。先在工程根目录核对：

```bash
git apply --check task_manager/third_party/audioplay/integration/ros/task_manager_audio.patch
git apply task_manager/third_party/audioplay/integration/ros/task_manager_audio.patch
```

若你已经修改过相关位置导致补丁不匹配，按上述调用示例与 CMake 片段接入即可，不要覆盖已有改动。编译主工程时传入 EKHO_SOURCE_DIR 和 EKHO_BUILD_DIR。

## JSON 与音频资源

默认配置固定为 **主程序真实可执行文件目录/config/audioplay.json**，通过 `/proc/self/exe` 定位，与当前工作目录或共享库位置无关。ROS 安装后的默认位置通常是节点所在 `lib/task_manager/config/audioplay.json`，接入片段会部署到该处。配置初始化读取一次，修改后需重启。

```json
{
  "audio": {"enabled": true, "volume": 0.8, "voice": "Mandarin", "speed": 0},
  "policy": {"repeat_interval_ms": 1000},
  "voices": {
    "running": {
      "file": "../audio/001.mp3",
      "text": "车辆正在运行，请注意安全。"
    },
    "fault:device/2/9": {
      "file": "../audio/device_2_error_9.wav",
      "text": "设备二发生故障，请检查。"
    }
  }
}
```

顶层 audio、policy、voices 都是必需对象；组内可省略默认项。相对 file 路径以 JSON 所在目录为基准。没有配置场景、没有文件、文件不可读或无法解码时，使用配置 text 或内置文案。text 空白等同未配置。故障先查 `fault:<stable_key>`；设备故障还会查 `fault:device/ID/*`；再查 `fault`，最后使用内置映射。精确条目存在且file为空时表示直接TTS，不继续查设备通用项。选中项的 file/text 成对生效，不把不同故障录音与文案混用。

支持 **MP3（CBR/VBR，单声道或双声道）** 和 **PCM WAV（单声道或双声道，8～192 kHz，8/16/24/32 位整数 PCM）**。按内容选择解码器，不根据扩展名保证可播放；AAC 和浮点 WAV 未支持。文件按块读取，不整段载入内存。每次启动播放任务重新打开配置路径，向既有配置路径放入文件后，下次任务即可使用。播放时长由文件实际音频决定，完成输出缓冲播放后才通知业务层。

音量在统一输出层应用一次，范围 0..1；语速 -50..100，仅作用于 TTS。取消和输出设备错误不触发旧文案回退。文件部分播放后发生源错误时，先清缓冲，再从完整文案开头合成；每次任务最多回退一次。

JSON 使用 UTF-8，采用库自身的语法/类型处理；只保留必要参数范围检查，不增加自定义大小、深度、资源数量或重复键检查。重复键使用最后一个值，未知键忽略。配置缺失或无法加载时 Play 返回 NotReady。文案是否可合成只在引擎实际进行 TTS 前检查一次。文件尚未准备好不导致初始化失败。Ekho 语音数据在首次需要 TTS 时加载，音频输出按任务连接。

## 规则与默认参数

优先级：急停 > 故障 > 充电 > 障碍 > 低电量 > 模式提示 > 运动。文件存在与否不影响优先级。故障具体映射见 docs/FAULT_MAPPING.md。

| policy 配置 | 默认值 | 含义 |
| --- | --- | --- |
| repeat_interval_ms | 1000 | 持续场景正常播完后的循环间隔，不用于一次性模式提示 |
| retry_interval_ms | 1000 | 播放失败后的重试间隔，最小 100 ms |
| mode_prompt_enabled | true | 模式变化时完整提示一次；false 关闭模式提示 |
| obstacle_limit_ms | 30000 | 障碍累计被选中窗口；0 为障碍立即静音占位 |
| low_battery_trigger_percent | 20 | 低电触发 SOC，包含边界 |
| low_battery_recover_percent | 25 | 低电解除 SOC，必须大于触发值 |
| low_battery_confirm_ms | 3000 | 低电持续确认时间 |
| input_timeout_ms | 3000 | 接收快照超时，最小 100 ms |
| linear_enter_m_s / linear_exit_m_s | 0.02 / 0.01 | 平移进入/退出门限 |
| angular_enter_rad_s / angular_exit_rad_s | 0.10 / 0.07 | 转向进入/退出门限 |

时间上限 3600000 ms。低电阈值、确认时间、输入超时及速度门限是本实现的可调默认值，需要按车辆实际设置；不是声称单片机使用这些数值。

每次有效模式变化生成一次提示事件，首次收到有效模式也会提示。同一事件仅在整句正常播放完成后清除，并允许运动等低优先级场景继续参与选择；持续传入相同模式不再播报。高优先级抢占后，尚未完成的当前模式事件仍可重播；如果期间模式又发生变化，仅保留最新事件。旧配置 `mode_prompt_ms` 仅兼容启用/关闭语义，不再计时。

障碍窗口累计分支被选中的时间，包括句间隔，高优先级覆盖期间暂停累计。窗口届满会取消正在播放的障碍提示并保持无声占位，不回落到运行或低电；障碍解除才释放并重置计时。

持续场景在有效且允许出声时循环。更高级场景或当前场景失效会取消旧句，待旧任务完成清理后从最新场景开头播，不补播历史句。没有 FIFO 队列。相同状态更新只更新时效。

运动须同时满足 driving=true、paused=false 和有效速度。依据平移速度模长及角速度带回差判断，正角速度按左转、负角速度按右转；无平移但有角速度按自旋。方向采用当前 ROS 车体坐标约定，上车需要核对实际正负号。

输入超时抑制充电和运动提示，不因断流截断已经触发的一次性模式提示；最后已确认急停、故障、障碍和已确认低电保持原规则，不能把断流当作告警解除。未完成的低电确认被取消。新快照恢复后重新判断。该策略不控制车辆，也不替代主工程故障锁存和复位。

现有 State 不提供独立 BMS 数据新鲜度：默认 0% 与真实 0% 无法由适配器区分；battery_percent 的 NaN 只代表普通 C++ 调用方未提供。fieldViolation 已合并减速/停车，operatingMode 是上报模式，paused 不是全局静音。不额外推断升级、初始化完成或其他缺失 MCU 场景。

## 测试与源文件

```bash
# 不需要 Ekho、PulseAudio 或声卡，仍需 libsndfile1-dev 和 libmpg123-dev 来测试 WAV/MP3 解码
cmake -S . -B build-test -DAUDIOPLAY_TEST_ONLY=ON
cmake --build build-test -j1
cd build-test
ctest --output-on-failure
```

真实 Ekho + 模拟输出集成测试：在正常构建时增加 `-DAUDIOPLAY_BUILD_TESTS=ON -DAUDIOPLAY_BUILD_BACKEND_TESTS=ON`。该测试默认使用 EKHO_SOURCE_DIR/ekho-data；可在该目录准备配套语音数据。实际 PulseAudio/扬声器、ARM64、2 GB 内存占用和实车响应需要在目标机验证。

业务源码位于 src：AudioPlay.cpp 管理公开入口；Config.cpp 解析 JSON；StateParser.cpp 提取状态并解析 errors；AnnouncementTable.cpp 集中维护场景、错误映射和优先级；VoicePolicy.cpp 生成候选并按表选优，保留计时；PlaybackScheduler.cpp 管理控制/播放线程。引擎源码位于 engine/src：PlaybackEngine.cpp 处理 WAV/MP3 与 TTS 回退；EkhoBackend.cpp 处理合成回调；PulseOutput.cpp 管理统一输出。

第三方说明见 THIRD_PARTY.md。
