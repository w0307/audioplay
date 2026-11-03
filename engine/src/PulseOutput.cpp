#include "PulseOutput.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <vector>
#include <cmath>

namespace audio_engine {
namespace {
using Clock = std::chrono::steady_clock;
// 单位 ms：单次 poll 等待、连接/无进度超时、清缓冲超时。
const int kPollMs = 10;
const int kIoTimeoutMs = 3000;
const int kFlushTimeoutMs = 500;
bool Cancelled(const CancelToken& token) { return token && token->load(); }
}
PulseOutput::~PulseOutput() { Close(); }
void PulseOutput::Close() {
    if (stream_) { pa_stream_disconnect(stream_); pa_stream_unref(stream_); stream_ = nullptr; }
    if (context_) { pa_context_disconnect(context_); pa_context_unref(context_); context_ = nullptr; }
    if (loop_) { pa_mainloop_free(loop_); loop_ = nullptr; }
}
bool PulseOutput::Healthy(std::string& error) const {
    if (!context_ || !PA_CONTEXT_IS_GOOD(pa_context_get_state(context_)) ||
        (stream_ && !PA_STREAM_IS_GOOD(pa_stream_get_state(stream_)))) {
        error = std::string("PulseAudio connection failed: ") +
                (context_ ? pa_strerror(pa_context_errno(context_)) : "no context");
        return false;
    }
    return true;
}
bool PulseOutput::Pump(std::string& error) {
    if (pa_mainloop_prepare(loop_, kPollMs) < 0 || pa_mainloop_poll(loop_) < 0 ||
        pa_mainloop_dispatch(loop_) < 0) {
        error = "PulseAudio mainloop failed"; return false;
    }
    return Healthy(error);
}
bool PulseOutput::Open(int sample_rate, int channels, const CancelToken& cancel, std::string& error) {
    Close();
    if (Cancelled(cancel)) { error = "Cancelled before opening output"; return false; }
    if (channels != 1 && channels != 2) { error = "Only mono/stereo output supported"; return false; }
    rate_ = sample_rate; channels_ = channels;
    pa_sample_spec spec;
    spec.format = PA_SAMPLE_S16NE;
    spec.rate = static_cast<std::uint32_t>(sample_rate);
    spec.channels = static_cast<std::uint8_t>(channels);
    if (!pa_sample_spec_valid(&spec)) { error = "Invalid Ekho sample rate"; return false; }
    loop_ = pa_mainloop_new();
    if (!loop_) { error = "Cannot create PulseAudio mainloop"; return false; }
    context_ = pa_context_new(pa_mainloop_get_api(loop_), "AudioEngine");
    if (!context_ || pa_context_connect(context_, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0) {
        error = "Cannot connect PulseAudio context"; Close(); return false;
    }
    auto deadline = Clock::now() + std::chrono::milliseconds(kIoTimeoutMs);
    while (pa_context_get_state(context_) != PA_CONTEXT_READY) {
        if (Cancelled(cancel)) { error = "Cancelled opening context"; Close(); return false; }
        if (Clock::now() >= deadline) { error = "PulseAudio connect timeout"; Close(); return false; }
        if (!Pump(error)) { Close(); return false; }
    }
    stream_ = pa_stream_new(context_, "Audio playback", &spec, nullptr);
    if (!stream_) { error = "Cannot create PulseAudio stream"; Close(); return false; }
    pa_buffer_attr attr;
    // 目标播放缓冲 60 ms，上限 240 ms；最终硬件延迟由服务端决定。
    attr.tlength = static_cast<std::uint32_t>(pa_usec_to_bytes(60000, &spec));
    attr.maxlength = attr.tlength * 4;
    attr.minreq = static_cast<std::uint32_t>(pa_usec_to_bytes(20000, &spec));
    // 自动预缓冲：句间空闲/欠载时暂停读索引，下一句不会写入已经过去的时间段。
    // prebuf=0 需要额外的手动 cork/seek 控制，不能用于这里的间歇播报。
    attr.prebuf = static_cast<std::uint32_t>(-1);
    attr.fragsize = static_cast<std::uint32_t>(-1);
    if (pa_stream_connect_playback(stream_, nullptr, &attr, PA_STREAM_ADJUST_LATENCY, nullptr, nullptr) < 0) {
        error = "Cannot connect PulseAudio playback stream"; Close(); return false;
    }
    deadline = Clock::now() + std::chrono::milliseconds(kIoTimeoutMs);
    while (pa_stream_get_state(stream_) != PA_STREAM_READY) {
        if (Cancelled(cancel)) { error = "Cancelled opening stream"; Close(); return false; }
        if (Clock::now() >= deadline) { error = "PulseAudio stream timeout"; Close(); return false; }
        if (!Pump(error)) { Close(); return false; }
    }
    return true;
}
SpeakResult PulseOutput::Write(const short* pcm, std::size_t samples, const CancelToken& cancel,
                               std::string& error) {
    if (!stream_ || !pcm || samples % static_cast<std::size_t>(channels_)) {
        error = "Invalid output PCM buffer"; return SpeakResult::Failed;
    }
    // 统一缩放文件与 TTS，Ekho 自身保持 100% 音量。
    std::vector<short> scaled;
    if (volume_ != 1.0) {
        scaled.resize(samples);
        for (std::size_t i = 0; i < samples; ++i) scaled[i] = static_cast<short>(std::lround(pcm[i] * volume_));
        pcm = scaled.data();
    }
    const char* data = reinterpret_cast<const char*>(pcm);
    std::size_t bytes = samples * sizeof(short);
    const std::size_t frame_bytes = sizeof(short) * static_cast<std::size_t>(channels_);
    auto deadline = Clock::now() + std::chrono::milliseconds(kIoTimeoutMs);
    while (bytes) {
        if (Cancelled(cancel)) return SpeakResult::Cancelled;
        if (!Healthy(error)) return SpeakResult::Failed;
        const std::size_t writable = pa_stream_writable_size(stream_);
        if (writable == static_cast<std::size_t>(-1)) {
            error = "PulseAudio writable-size failed"; return SpeakResult::Failed;
        }
        const std::size_t chunk = std::min(bytes, writable) / frame_bytes * frame_bytes;
        if (chunk) {
            if (pa_stream_write(stream_, data, chunk, nullptr, 0, PA_SEEK_RELATIVE) < 0) {
                error = "PulseAudio write failed"; return SpeakResult::Failed;
            }
            data += chunk; bytes -= chunk;
            deadline = Clock::now() + std::chrono::milliseconds(kIoTimeoutMs);
        } else {
            if (Clock::now() >= deadline) { error = "PulseAudio write timeout"; return SpeakResult::Failed; }
            if (!Pump(error)) return SpeakResult::Failed;
        }
    }
    return Cancelled(cancel) ? SpeakResult::Cancelled : SpeakResult::Completed;
}
void PulseOutput::Completed(pa_stream*, int success, void* data) {
    auto* completion = static_cast<Completion*>(data);
    completion->success = success != 0;
    completion->done = true;
}
SpeakResult PulseOutput::Wait(pa_operation* op, Completion& completion, const CancelToken& cancel,
                              int timeout_ms, std::string& error) {
    if (!op) { error = "PulseAudio operation creation failed"; return SpeakResult::Failed; }
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
    SpeakResult result = SpeakResult::Completed;
    while (!completion.done && pa_operation_get_state(op) == PA_OPERATION_RUNNING) {
        if (Cancelled(cancel)) { result = SpeakResult::Cancelled; break; }
        if (Clock::now() >= deadline) { error = "PulseAudio operation timeout"; result = SpeakResult::Failed; break; }
        if (!Pump(error)) { result = SpeakResult::Failed; break; }
    }
    if (result == SpeakResult::Completed) {
        if (Cancelled(cancel)) result = SpeakResult::Cancelled;
        else if (!completion.done || !completion.success) {
            error = "PulseAudio operation failed"; result = SpeakResult::Failed;
        }
    }
    // 先取消可能迟到的回调，再销毁栈上的 Completion。
    if (pa_operation_get_state(op) == PA_OPERATION_RUNNING) pa_operation_cancel(op);
    pa_operation_unref(op);
    return result;
}
SpeakResult PulseOutput::Drain(const CancelToken& cancel, std::string& error) {
    Completion completion;
    return Wait(pa_stream_drain(stream_, &Completed, &completion), completion,
                cancel, kIoTimeoutMs, error);
}
bool PulseOutput::Flush(std::string& error) {
    if (!stream_) return true;
    Completion completion;
    const bool ok = Wait(pa_stream_flush(stream_, &Completed, &completion), completion,
                         CancelToken(), kFlushTimeoutMs, error) == SpeakResult::Completed;
    if (!ok) Close();
    return ok;
}
std::unique_ptr<AudioOutput> MakeAudioOutput() { return std::unique_ptr<AudioOutput>(new PulseOutput); }
}
