# 引擎第三方依赖

- Ekho 8.9：https://github.com/hgneng/ekho/tree/v8.9 。不附带源码、静态库或语音数据；使用调用方已构建、带 v2 取消补丁的版本。取消补丁脚本位于 scripts/patch_ekho89.py。
- libsndfile、PulseAudio、eSpeak-ng、mpg123：由目标环境开发包提供。cmake/FindEkho89.cmake 查找并链接；不附带其二进制。AudioEngine 1.1 起直接使用 mpg123 解码 MP3，libsndfile 解码 PCM WAV。
- Ekho、第三方依赖、语音数据以及包含静态链接代码的产物，在分发时须遵守各自许可。

本引擎没有 JSON 解析器依赖。车辆播报模块使用的 nlohmann/json 位于父工程，未链接到引擎。
