#include "AudioPlay.h"
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

namespace { volatile std::sig_atomic_t quit_requested = 0; void OnSignal(int) { quit_requested = 1; } }
int main() {
    std::signal(SIGINT, OnSignal); std::signal(SIGTERM, OnSignal);
    auto& audio = AudioPlay::GetInstance();
    for (int tick = 0; tick < 220 && !quit_requested; ++tick) {
        AudioState s;
        s.e_stop = "NONE"; s.battery_percent = 80; s.operating_mode = "AUTOMATIC";
        s.driving = true; s.vx_m_s = 0.3;
        if (tick >= 50 && tick < 80) s.field_violation = true;
        if (tick >= 80 && tick < 110) { s.field_violation = true; s.e_stop = "AUTOACK"; s.paused = true; }
        if (tick >= 110 && tick < 140) s.field_violation = true;
        if (tick >= 140 && tick < 170) s.omega_rad_s = 0.3;
        if (tick >= 170 && tick < 200) s.charging = true;
        if (tick >= 200) { s.driving = false; s.vx_m_s = 0; }
        const auto result = audio.Play(s);
        if (result != AudioPlay::Result::Accepted) {
            std::cerr << "Play rejected: " << static_cast<int>(result) << " (see [AudioPlay] logs)\n";
            return result == AudioPlay::Result::Disabled ? 0 : 1;
        }
        if (tick % 10 == 0) std::cout << "t=" << tick / 10 << "s, state accepted\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    // 析构会取消并回收线程；信号处理函数本身不调用播放器。
    return 0;
}
