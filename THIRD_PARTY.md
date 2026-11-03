# 第三方依赖

- nlohmann/json 3.11.3：随包提供 single_include/nlohmann/json.hpp，位于 third_party/nlohmann；MIT 许可证见同目录 LICENSE.MIT。来源：https://github.com/nlohmann/json/tree/v3.11.3 。JSON 解析接口依据：https://json.nlohmann.me/api/basic_json/parse/ 。
- Ekho 8.9：不随包分发源码、库或语音数据。使用者提供已构建的源码树，应用 scripts/patch_ekho89.py 的取消兼容补丁。来源：https://github.com/hgneng/ekho/tree/v8.9 。Ekho、相关语音数据以及静态链接产物的分发须遵守各自许可。
- libsndfile、PulseAudio、eSpeak-ng、mpg123：由系统开发包提供，不随包分发二进制。源码树中的 engine/cmake/FindEkho89.cmake 查找依赖。

包内默认中文文案是依据状态/故障语义编写的模板，不是原 MCU 语音素材的逐字转录。

- audio目录的74个MP3来自用户提供的“版本1-男主女辅.zip”，未转码或剪裁；原文件名和校验值保存在config/audio_manifest.json。
