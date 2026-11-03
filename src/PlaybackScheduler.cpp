#include "PlaybackScheduler.h"
#include <algorithm>
#include <exception>

namespace audioplay_detail {
namespace {
bool Same(const VoiceDecision& a, const VoiceDecision& b, const VoiceResource& x, const VoiceResource& y) {
    return a.key.scene == b.key.scene && a.key.detail == b.key.detail && a.key.event_id == b.key.event_id &&
        a.active == b.active && a.audible == b.audible && a.repeat == b.repeat &&
        a.repeat_gap == b.repeat_gap && x.file_path == y.file_path && x.fallback_text == y.fallback_text;
}
}
PlaybackScheduler::PlaybackScheduler(const RuntimeConfig& c, std::unique_ptr<audio_engine::IPlaybackEngine> backend)
    : config_(c), policy_(c), catalog_(c), backend_(std::move(backend)) {
    if (!config_.enabled) return;
    std::string error;
    try {
        audio_engine::EngineOptions options;
        options.volume = c.volume; options.speed = c.speed; options.voice = c.voice;
        if (!backend_ || !backend_->Initialize(options, error)) { Log("Initialization failed: " + error); return; }
        ready_ = true;
        playback_ = std::thread(&PlaybackScheduler::PlaybackLoop, this);
        control_ = std::thread(&PlaybackScheduler::ControlLoop, this);
    } catch (const std::exception& e) {
        { std::lock_guard<std::mutex> lock(mutex_); ready_ = false; stopping_ = true; }
        changed_.notify_all();
        if (playback_.joinable()) playback_.join();
        Log(std::string("Initialization exception: ") + e.what());
    }
}
PlaybackScheduler::~PlaybackScheduler() { Shutdown(); }
AudioPlay::Result PlaybackScheduler::UpdateState(VoiceSnapshot snapshot) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!config_.enabled) return AudioPlay::Result::Disabled;
    if (!ready_) return AudioPlay::Result::NotReady;
    if (stopping_) return AudioPlay::Result::ShuttingDown;
    snapshot.sequence = ++sequence_;
    snapshot.received_at = Clock::now();
    // 在接收锁内观察每次模式变化；只保存最新事件，不建立历史播报队列。
    policy_.ObserveMode(snapshot.mode);
    latest_ = std::move(snapshot); has_state_ = true;
    changed_.notify_all();
    return AudioPlay::Result::Accepted;
}
void PlaybackScheduler::Shutdown() {
    std::lock_guard<std::mutex> lifecycle(shutdown_mutex_);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        if (cancel_) cancel_->store(true);
    }
    changed_.notify_all();
    if (control_.joinable()) control_.join();
    if (playback_.joinable()) playback_.join();
}
void PlaybackScheduler::ControlLoop() {
    try {
        std::unique_lock<std::mutex> lock(mutex_);
        while (!stopping_) {
            const auto now = Clock::now();
            if (has_completion_) {
                has_completion_ = false;
                // 只有当前任务的完成通知能改变 busy；旧场景结果不能改变新场景的间隔。
                if (completion_.task_id == job_.task_id) {
                    busy_ = false; cancel_.reset();
                    // 先消费已完整播放的事件，再评估最新 State，避免完成瞬间的抢占导致重复。
                    if (completion_.result.outcome == PlaybackOutcome::Completed)
                        policy_.OnPlaybackCompleted(job_.scene);
                    if (completion_.scene_revision == revision_) {
                        if (completion_.result.outcome == PlaybackOutcome::Failed) due_ = now + config_.retry_interval;
                        else if (completion_.result.outcome == PlaybackOutcome::Completed)
                            due_ = decision_.repeat ? now + decision_.repeat_gap : TimePoint::max();
                    }
                }
            }
            if (has_state_) {
                const auto next = policy_.Evaluate(latest_, now);
                const auto resource = catalog_.Resolve(next);
                if (!Same(decision_, next, resource_, resource)) {
                    decision_ = next; resource_ = resource; ++revision_; due_ = now;
                    if (cancel_) cancel_->store(true);
                }
            }
            if (!busy_ && has_state_ && decision_.active && decision_.audible && now >= due_) {
                job_ = PlaybackJob(); job_.task_id = ++next_task_; job_.scene_revision = revision_;
                job_.scene = decision_.key; job_.resource = resource_;
                cancel_ = std::make_shared<std::atomic<bool>>(false);
                busy_ = true; pending_ = true; changed_.notify_all();
            }
            TimePoint wake = has_state_ ? policy_.NextDeadline() : TimePoint::max();
            if (!busy_ && decision_.active && decision_.audible) wake = std::min(wake, due_);
            const auto observed = sequence_;
            const auto predicate = [this, observed] { return stopping_ || has_completion_ || sequence_ != observed; };
            if (wake == TimePoint::max()) changed_.wait(lock, predicate);
            else changed_.wait_until(lock, wake, predicate);
        }
    } catch (const std::exception& e) {
        Log(std::string("Control thread failed: ") + e.what());
        std::lock_guard<std::mutex> lock(mutex_);
        ready_ = false; stopping_ = true; if (cancel_) cancel_->store(true); changed_.notify_all();
    } catch (...) {
        Log("Control thread failed with unknown exception");
        std::lock_guard<std::mutex> lock(mutex_);
        ready_ = false; stopping_ = true; if (cancel_) cancel_->store(true); changed_.notify_all();
    }
}
void PlaybackScheduler::PlaybackLoop() {
    for (;;) {
        PlaybackJob job; CancelToken cancel;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            changed_.wait(lock, [this] { return stopping_ || pending_; });
            if (stopping_) return;
            job = job_; cancel = cancel_; pending_ = false;
        }
        // The engine receives only a source and text. IDs, scene and revision never cross the boundary.
        audio_engine::PlaybackRequest request;
        request.file_path = job.resource.file_path; request.text = job.resource.fallback_text;
        audio_engine::PlaybackResult result;
        try { result = backend_->Play(request, cancel); }
        catch (const std::exception& e) { result.message = e.what(); result.error_stage = ErrorStage::Output; }
        catch (...) { result.message = "Unknown playback exception"; result.error_stage = ErrorStage::Output; }
        if (cancel->load()) result.outcome = PlaybackOutcome::Cancelled;
        if (result.outcome == PlaybackOutcome::Failed) LogThrottled("playback", "Playback failed: " + result.message);
        else if (result.used_tts_fallback && !result.message.empty()) LogThrottled("fallback", result.message);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            completion_.task_id = job.task_id; completion_.scene_revision = job.scene_revision;
            completion_.result = std::move(result); has_completion_ = true;
        }
        changed_.notify_all();
    }
}
}
