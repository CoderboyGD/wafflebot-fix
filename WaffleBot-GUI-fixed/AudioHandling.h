#ifndef AUDIOHANDLING_H
#define AUDIOHANDLING_H

#include <array>
#include <filesystem>
#include <string>
#include <vector>

#include "AudioFile.h"
#include "ClickFolder.h"

class Macro;

constexpr int CLICK_FOLDER_TOTAL = 48; // 6 button groups * 8 click kinds

using ClickFiles = std::array<std::vector<AudioFile>, CLICK_FOLDER_TOTAL>;

// Loads every readable audio file in a folder (non-audio files are skipped).
// `skipped` (optional) receives the number of files that couldn't be decoded.
std::vector<AudioFile> loadAudioFolder(const std::filesystem::path& folder, int* skipped = nullptr);

// Loads noise.wav / whitenoise.wav from a folder, empty AudioFile if there isn't one.
AudioFile loadNoiseFile(const std::filesystem::path& folder);

struct RenderResult {
    bool ok{false};
    std::string message{}; // error if !ok, otherwise an optional warning
    int clicksMixed{0};
    int clicksSkipped{0};
};

// Renders the macro's clicks into a 16-bit stereo WAV at outPath.
RenderResult renderMacroToWav(Macro& macro,
                              const ClickFiles& clickFiles,
                              const AudioFile* noise,
                              const std::filesystem::path& outPath);

#endif // AUDIOHANDLING_H
