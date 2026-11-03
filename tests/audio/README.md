# 测试录音

这些 MP3 是本项目用 440 Hz 正弦波生成的测试音，不含真人录音或外部音乐，不是业务播报素材。

| 文件 | 音频时长 | 采样率 | 声道 | 编码 |
| --- | --- | --- | --- | --- |
| short_mono_cbr.mp3 | 0.25 秒 | 16000 Hz | 1 | CBR 32 kbps |
| short_stereo_cbr.mp3 | 0.6 秒 | 48000 Hz | 2 | CBR 128 kbps |
| long_stereo_vbr.mp3 | 4.25 秒 | 44100 Hz | 2 | VBR，LAME quality 4 |

文件包含 ID3 标签和 gapless 元数据。file_tests 验证真实 MP3 解码后的样本数、尾块、输出完成、文件优先和取消；不需要声卡或 ffmpeg。长录音测试将 PCM 送入模拟输出，因此执行时不需要实际等待 4.25 秒。

要重新生成这些文件，安装带 libmp3lame 编码器的 ffmpeg 后执行 `python3 tests/generate_audio_fixtures.py`。ffmpeg 只用于重建测试素材，生产程序不调用它。
