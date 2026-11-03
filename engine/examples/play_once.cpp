#include <audio_engine/PlaybackEngine.h>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: audio_engine_demo FILE_OR_EMPTY FALLBACK_TEXT\n";
        return 2;
    }
    auto engine = audio_engine::CreatePlaybackEngine();
    audio_engine::EngineOptions options;
    std::string error;
    if (!engine->Initialize(options, error)) { std::cerr << error << '\n'; return 1; }
    audio_engine::PlaybackRequest request;
    request.file_path = argv[1]; request.text = argv[2];
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    const auto result = engine->Play(request, cancel);
    if (result.outcome == audio_engine::PlaybackOutcome::Failed) {
        std::cerr << result.message << '\n'; return 1;
    }
    return 0;
}
