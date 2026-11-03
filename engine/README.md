# AudioEngine 1.1：通用播放引擎

本目录可单独复制、编译和安装。只依赖 Ekho 8.9、PulseAudio、libsndfile、mpg123 等音频依赖，不依赖父目录、ROS、AudioState、车辆故障规则、JSON 或业务配置文件。

## 职责

- 串行执行一次 `PlaybackRequest{file_path, text}`。
- 有可解码的 MP3 或 PCM WAV 时播放文件；未提供文件或文件源失败时合成备用文案。
- 接收取消令牌，停止当前任务并完成清理；取消不触发旧文案合成。
- 返回 `Completed`、`Cancelled` 或 `Failed`；输出设备故障直接失败，不以 TTS 重试。
- 不管理业务优先级、场景、历史任务、循环次数、间隔或状态过期；这些由调用方决定。

所有结果通过 `Play()` 的返回值反馈。此方法会等待本次任务完成或清理完毕，调用方可在自己的播放线程中执行；引擎不另外创建业务调度线程，Ekho 仍使用其内部合成线程。

## 最小调用

```cpp
#include <audio_engine/PlaybackEngine.h>

auto engine = audio_engine::CreatePlaybackEngine();
audio_engine::EngineOptions options;
options.volume = 0.8;
options.speed = 0;
std::string error;
if (!engine->Initialize(options, error)) {
    // 向调用方报告 error
    return;
}

audio_engine::PlaybackRequest request;
request.file_path = "/opt/my_app/audio/notice.wav";
request.text = "提示信息。";
auto cancel = std::make_shared<std::atomic<bool>>(false);
const auto result = engine->Play(request, cancel);
```

`Initialize`、`Play` 和析构必须串行。取消时，其他线程只调用 `cancel->store(true)`；不要同时析构引擎。等待 `Play()` 返回后才能开始下一任务或析构。每个任务使用新的非空令牌，已取消令牌不复位。

调用方应提供解析后的实际路径，推荐绝对路径。引擎不猜测 JSON、可执行文件或录音目录；直接传相对路径时，文件系统按当前工作目录解析。路径上限 4096 字节且不允许 NUL。文件可用时允许 text 为空；需要合成时，text 必须为非空有效 UTF-8，最多 4096 字节。

支持 MP3（CBR/VBR、单/双声道）和整数 PCM WAV（单/双声道、8～192 kHz、8/16/24/32 位）。按内容识别 WAV，其他文件尝试 mpg123 解码；文件扩展名不决定格式。AAC、浮点 WAV 未支持。文本合成后端当前支持普通话（voice 为 Mandarin 或 pinyin）；speed 范围 -50..100，volume 范围 0..1，音量统一作用于录音与 TTS。

MP3 使用 mpg123 解码，文件描述符由引擎管理，每个任务使用独立解码句柄。WAV/MP3 均分块输出，不预先估计文件长度；最后一块音频及输出缓冲全部播放完成后才返回 Completed。文件长短不影响接口；调用方的取消令牌仍可中断。运行时不调用 ffmpeg 或外部播放器。

Ekho 后端的回调上下文为进程内单实例：同一时刻只能有一个已加载 TTS 的引擎实例，后续实例尝试加载会返回错误。初始化只校验后端，普通话数据在首次需要 TTS 时加载。

## 独立构建

Ubuntu 开发依赖：

```bash
sudo apt install build-essential cmake python3 autoconf automake libtool pkg-config \
  libsndfile1-dev libespeak-ng-dev espeak-ng-data libpulse-dev libmpg123-dev
```

在本目录操作，设置实际 Ekho 源码路径：

```bash
export EKHO_SRC=/home/xf/code/ekho-8.9
python3 scripts/patch_ekho89.py "$EKHO_SRC"
```

需要使用打过 v2 取消补丁、通过 `-fPIC` 重编译的 Ekho 8.9。已有同版补丁及产物可复用；否则在 Ekho 源码目录重新构建：

```bash
autoreconf -fi
# 已经构建过时先执行 make clean
./configure CFLAGS="-O2 -fPIC" CXXFLAGS="-O2 -fPIC"
make -j1
```

不要额外启用 Festival、MusicXML、Friso、SoundTouch、LAME 或 SOXR。回到引擎目录：

```bash
cmake -S . -B build \
  -DEKHO_SOURCE_DIR="$EKHO_SRC" \
  -DEKHO_BUILD_DIR="$EKHO_SRC" \
  -DAUDIO_ENGINE_BUILD_DEMO=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build -j1

export EKHO_DATA_PATH=/opt/ekho-8.9/share/ekho-data
./build/bin/audio_engine_demo /opt/my_app/audio/notice.wav "提示信息。"
# 仅合成：
./build/bin/audio_engine_demo "" "提示信息。"
```

普通话数据目录须包含有效的 zh.dict、pinyin.index、pinyin.voice；eSpeak-ng 数据也必须完整。自定义 eSpeak-ng 安装可设置 ESPEAK_DATA_PATH。实际播放需要当前用户的 PulseAudio 会话。

## 接入其他项目

源码方式：

```cmake
add_subdirectory(third_party/audio_engine)
target_link_libraries(my_app PRIVATE AudioEngine::audio_engine)
```

安装方式：先 `cmake --install build --prefix /your/prefix`，再在宿主中使用：

```cmake
find_package(AudioEngine 1 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE AudioEngine::audio_engine)
```

复制本目录时同时保留 cmake、include、src 和 scripts。第三方来源及许可说明见 THIRD_PARTY.md。
