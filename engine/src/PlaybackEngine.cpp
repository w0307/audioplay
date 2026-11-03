#include "PlaybackEngine.h"
#include <audio_engine/Text.h>
#include <cmath>
#include <sndfile.h>
#include <mpg123.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <vector>

namespace audio_engine {
namespace {
struct ScopedFd {
    explicit ScopedFd(int value) : value(value) {}
    ~ScopedFd() { ::close(value); }
    int value;
};
}
PlaybackEngine::PlaybackEngine(std::unique_ptr<AudioOutput> output, std::unique_ptr<SpeechSynthesizer> speech)
    : output_(std::move(output)), speech_(std::move(speech)) {}
bool PlaybackEngine::Initialize(const EngineOptions& config, std::string& error) {
    initialized_ = false;
    error.clear();
    if (!std::isfinite(config.volume) || config.volume < 0 || config.volume > 1 ||
        config.speed < -50 || config.speed > 100 ||
        (config.voice != "Mandarin" && config.voice != "pinyin")) {
        error = "Invalid engine volume, speed or voice"; return false;
    }
    if (!output_ || !speech_) { error = "Missing playback backend"; return false; }
    output_->SetVolume(config.volume);
    initialized_ = speech_->Initialize(config, error);
    return initialized_;
}
PlaybackResult PlaybackEngine::DecodeAndPlayFile(const std::string& path, const CancelToken& cancel) {
    PlaybackResult r; r.error_stage = ErrorStage::FileSource;
    const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) { r.message = "File unavailable: " + path + ": " + std::strerror(errno); return r; }
    ScopedFd file(fd);
    struct stat st;
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) { r.message = "Audio path is not a regular file"; return r; }
    char header[12];
    const auto bytes = ::pread(fd, header, sizeof(header), 0);
    if (bytes < 0) { r.message = "Cannot read audio file: " + path; return r; }
    // 按文件内容选择解码器；文件扩展名不决定格式。
    const bool wav = bytes == static_cast<ssize_t>(sizeof(header)) &&
        (!std::memcmp(header, "RIFF", 4) || !std::memcmp(header, "RIFX", 4)) && !std::memcmp(header + 8, "WAVE", 4);
    return wav ? DecodeAndPlayWav(fd, path, cancel) : DecodeAndPlayMp3(fd, path, cancel);
}
PlaybackResult PlaybackEngine::DecodeAndPlayWav(int fd, const std::string& path, const CancelToken& cancel) {
    PlaybackResult r; r.error_stage = ErrorStage::FileSource;
    SF_INFO info{};
    SNDFILE* raw = sf_open_fd(fd, SFM_READ, &info, SF_FALSE);
    if (!raw) { r.message = "Cannot decode WAV: " + path; return r; }
    std::unique_ptr<SNDFILE, int(*)(SNDFILE*)> file(raw, &sf_close);
    const int subtype = info.format & SF_FORMAT_SUBMASK;
    if ((info.format & SF_FORMAT_TYPEMASK) != SF_FORMAT_WAV ||
        (subtype != SF_FORMAT_PCM_U8 && subtype != SF_FORMAT_PCM_S8 && subtype != SF_FORMAT_PCM_16 && subtype != SF_FORMAT_PCM_24 && subtype != SF_FORMAT_PCM_32) ||
        info.channels < 1 || info.channels > 2 || info.samplerate < 8000 || info.samplerate > 192000 || info.frames <= 0) {
        r.message = "Expected nonempty PCM WAV, mono/stereo, 8..192 kHz: " + path; return r;
    }
    if (cancel->load()) { r.outcome = PlaybackOutcome::Cancelled; return r; }
    if (!output_->MatchesFormat(info.samplerate, info.channels) && !output_->Open(info.samplerate, info.channels, cancel, r.message)) {
        r.error_stage = ErrorStage::Output;
        if (cancel->load()) r.outcome = PlaybackOutcome::Cancelled;
        return r;
    }
    std::vector<short> pcm(4096 * static_cast<std::size_t>(info.channels));
    sf_count_t total = 0;
    for (;;) {
        if (cancel->load()) { r.outcome = PlaybackOutcome::Cancelled; return r; }
        const sf_count_t frames = sf_readf_short(file.get(), pcm.data(), 4096);
        if (frames <= 0) {
            if (frames < 0 || sf_error(file.get()) != SF_ERR_NO_ERROR || total != info.frames) {
                r.message = "WAV decode/read failed: " + path; return r;
            }
            break;
        }
        total += frames;
        r.outcome = output_->Write(pcm.data(), static_cast<std::size_t>(frames) * static_cast<std::size_t>(info.channels), cancel, r.message);
        if (r.outcome != PlaybackOutcome::Completed) { r.error_stage = ErrorStage::Output; return r; }
        r.outcome = PlaybackOutcome::Failed; // 后续源错误不能保留前一次 Write 的 Completed
    }
    r.outcome = output_->Drain(cancel, r.message);
    r.error_stage = r.outcome == PlaybackOutcome::Failed ? ErrorStage::Output : ErrorStage::None;
    return r;
}
PlaybackResult PlaybackEngine::DecodeAndPlayMp3(int fd, const std::string& path, const CancelToken& cancel) {
    PlaybackResult r; r.error_stage = ErrorStage::FileSource;
    // 兼容目标机上的旧版 mpg123；只初始化一次，不在单个任务结束时退出全局库。
    static const int initialized = mpg123_init();
    if (initialized != MPG123_OK) { r.message = "Cannot initialize MP3 decoder"; return r; }
    int error = MPG123_OK;
    std::unique_ptr<mpg123_handle, void(*)(mpg123_handle*)> decoder(mpg123_new(nullptr, &error), &mpg123_delete);
    if (!decoder) { r.message = mpg123_plain_strerror(error); return r; }
    if (mpg123_param(decoder.get(), MPG123_ADD_FLAGS, MPG123_QUIET, 0.0) != MPG123_OK ||
        mpg123_format_none(decoder.get()) != MPG123_OK) {
        r.message = "Cannot configure MP3 decoder"; return r;
    }
    const long* rates = nullptr; std::size_t count = 0;
    mpg123_rates(&rates, &count);
    for (std::size_t i = 0; i < count; ++i) {
        if (mpg123_format(decoder.get(), rates[i], MPG123_MONO | MPG123_STEREO, MPG123_ENC_SIGNED_16) != MPG123_OK) {
            r.message = "Cannot configure MP3 output format"; return r;
        }
    }
    if (mpg123_open_fd(decoder.get(), fd) != MPG123_OK) { r.message = "Cannot open MP3: " + path; return r; }
    std::vector<short> pcm(4096);
    long rate = 0; int channels = 0, encoding = 0;
    bool new_format = true, have_samples = false;
    for (;;) {
        if (cancel->load()) { r.outcome = PlaybackOutcome::Cancelled; return r; }
        if (new_format) {
            if (mpg123_getformat(decoder.get(), &rate, &channels, &encoding) != MPG123_OK ||
                encoding != MPG123_ENC_SIGNED_16 || channels < 1 || channels > 2 || rate < 8000 || rate > 192000) {
                r.message = "Cannot decode MP3 mono/stereo audio: " + path; return r;
            }
            if (cancel->load()) { r.outcome = PlaybackOutcome::Cancelled; return r; }
            if (!output_->MatchesFormat(static_cast<int>(rate), channels)) {
                // 同一文件若改变采样格式，先播放完旧格式的输出缓冲。
                if (have_samples) {
                    r.outcome = output_->Drain(cancel, r.message);
                    if (r.outcome != PlaybackOutcome::Completed) { r.error_stage = ErrorStage::Output; return r; }
                    r.outcome = PlaybackOutcome::Failed;
                }
                if (!output_->Open(static_cast<int>(rate), channels, cancel, r.message)) {
                    r.error_stage = ErrorStage::Output;
                    if (cancel->load()) r.outcome = PlaybackOutcome::Cancelled;
                    return r;
                }
            }
            new_format = false;
        }
        std::size_t bytes = 0;
        const int status = mpg123_read(decoder.get(), reinterpret_cast<unsigned char*>(pcm.data()), pcm.size() * sizeof(short), &bytes);
        // DONE/NEW_FORMAT 返回时也可能带有最后一段旧格式 PCM，先送完再处理状态。
        if (bytes) {
            if (bytes % (sizeof(short) * static_cast<std::size_t>(channels))) {
                r.message = "Incomplete MP3 PCM frame: " + path; return r;
            }
            r.outcome = output_->Write(pcm.data(), bytes / sizeof(short), cancel, r.message);
            if (r.outcome != PlaybackOutcome::Completed) { r.error_stage = ErrorStage::Output; return r; }
            have_samples = true; r.outcome = PlaybackOutcome::Failed;
        }
        if (status == MPG123_NEW_FORMAT) { new_format = true; continue; }
        if (status == MPG123_DONE) {
            if (!have_samples) { r.message = "MP3 contains no audio: " + path; return r; }
            r.outcome = output_->Drain(cancel, r.message);
            r.error_stage = r.outcome == PlaybackOutcome::Failed ? ErrorStage::Output : ErrorStage::None;
            return r;
        }
        if (status != MPG123_OK) { r.message = "MP3 decode/read failed: " + path + ": " + mpg123_strerror(decoder.get()); return r; }
    }
}
PlaybackResult PlaybackEngine::Play(const PlaybackRequest& request, const CancelToken& cancel) {
    if (!cancel || !initialized_ || request.file_path.size() > 4096 ||
        request.file_path.find('\0') != std::string::npos) {
        PlaybackResult invalid; invalid.error_stage = ErrorStage::Request;
        invalid.message = !cancel ? "A non-null cancellation token is required" :
            !initialized_ ? "Playback engine is not initialized" : "Invalid file path";
        return invalid;
    }
    try { return RunOnce(request, cancel); }
    catch (const std::exception& e) {
        output_->Close();
        PlaybackResult r;
        r.outcome = cancel->load() ? PlaybackOutcome::Cancelled : PlaybackOutcome::Failed;
        r.error_stage = ErrorStage::Output; r.message = e.what(); return r;
    } catch (...) {
        output_->Close();
        PlaybackResult r;
        r.outcome = cancel->load() ? PlaybackOutcome::Cancelled : PlaybackOutcome::Failed;
        r.error_stage = ErrorStage::Output; r.message = "Unexpected playback exception"; return r;
    }
}
PlaybackResult PlaybackEngine::RunOnce(const PlaybackRequest& request, const CancelToken& cancel) {
    PlaybackResult r;
    if (cancel->load()) { r.outcome = PlaybackOutcome::Cancelled; return r; }
    bool fallback = false;
    std::string fallback_reason;
    if (!request.file_path.empty()) {
        r = DecodeAndPlayFile(request.file_path, cancel);
        if (cancel->load()) r.outcome = PlaybackOutcome::Cancelled;
        if (r.outcome != PlaybackOutcome::Completed) {
            std::string flush_error;
            if (!output_->Flush(flush_error) && r.outcome != PlaybackOutcome::Cancelled) {
                r.outcome = PlaybackOutcome::Failed; r.error_stage = ErrorStage::Output; r.message = flush_error;
            }
        }
        if (r.outcome != PlaybackOutcome::Failed || r.error_stage != ErrorStage::FileSource) {
            if (r.outcome == PlaybackOutcome::Failed) output_->Close();
            return r;
        }
        fallback = true; fallback_reason = r.message;
    }
    // 在文件失败与合成之间重新检查，不能让被取消任务开始说话。
    if (cancel->load()) r.outcome = PlaybackOutcome::Cancelled;
    else if (!IsValidSpeechText(request.text)) {
        r = PlaybackResult(); r.error_stage = ErrorStage::TtsSource; r.message = "Invalid fallback text";
    } else r = speech_->Speak(request.text, cancel);
    if (cancel->load()) r.outcome = PlaybackOutcome::Cancelled;
    r.used_tts_fallback = fallback;
    if (fallback && r.outcome == PlaybackOutcome::Completed) r.message = fallback_reason + "; used TTS";

    if (r.outcome == PlaybackOutcome::Failed) output_->Close();
    return r;
}
std::unique_ptr<IPlaybackEngine> CreatePlaybackEngine() {
    auto output = MakeAudioOutput();
    auto speech = MakeSpeechSynthesizer(*output);
    return std::unique_ptr<IPlaybackEngine>(new PlaybackEngine(std::move(output), std::move(speech)));
}
}
