#include "AudioHandling.h"
#include "Macro.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <random>

#ifdef _WIN32
#define ENABLE_SNDFILE_WINDOWS_PROTOTYPES 1
#endif
#include <sndfile.h>

namespace fs = std::filesystem;

namespace {

SNDFILE* openFile(const fs::path& path, int mode, SF_INFO* info) {
#ifdef _WIN32
    // libsndfile's narrow-char sf_open() uses the ANSI code page on Windows, which breaks
    // for any path containing non-ASCII characters. Use the wide version instead.
    return sf_wchar_open(path.c_str(), mode, info);
#else
    return sf_open(path.c_str(), mode, info);
#endif
}

// Decodes any libsndfile-readable file to interleaved stereo float.
bool decodeFile(const fs::path& path, AudioFile& out) {
    SF_INFO info{};
    SNDFILE* file = openFile(path, SFM_READ, &info);
    if (!file) return false;

    bool ok = false;
    if (info.frames > 0 && info.channels > 0 && info.samplerate > 0 && info.frames < (1LL << 31)) {
        std::vector<float> raw(static_cast<size_t>(info.frames) * info.channels);
        sf_count_t got = sf_readf_float(file, raw.data(), info.frames);
        if (got > 0) {
            out.sampleRate = info.samplerate;
            out.samples.resize(static_cast<size_t>(got) * 2);
            for (sf_count_t i = 0; i < got; ++i) {
                float l = raw[static_cast<size_t>(i) * info.channels];
                float r = info.channels >= 2 ? raw[static_cast<size_t>(i) * info.channels + 1] : l;
                out.samples[static_cast<size_t>(i) * 2] = l;
                out.samples[static_cast<size_t>(i) * 2 + 1] = r;
            }
            ok = true;
        }
    }
    sf_close(file);
    return ok;
}

AudioFile resample(const AudioFile& in, int targetRate) {
    if (in.sampleRate == targetRate || in.empty()) return in;
    AudioFile out;
    out.sampleRate = targetRate;
    double ratio = static_cast<double>(in.sampleRate) / targetRate;
    size_t inFrames = in.frames();
    size_t outFrames = static_cast<size_t>(std::ceil(inFrames / ratio));
    out.samples.resize(outFrames * 2);
    for (size_t i = 0; i < outFrames; ++i) {
        double src = i * ratio;
        size_t i0 = static_cast<size_t>(src);
        size_t i1 = std::min(i0 + 1, inFrames - 1);
        if (i0 >= inFrames) i0 = inFrames - 1;
        float t = static_cast<float>(src - static_cast<double>(i0));
        for (int c = 0; c < 2; ++c) {
            float a = in.samples[i0 * 2 + c];
            float b = in.samples[i1 * 2 + c];
            out.samples[i * 2 + c] = a + (b - a) * t;
        }
    }
    return out;
}

// Which click kind (index within a group of 8) a click type maps to
int roleFor(ClickType type, bool pressed) {
    int base = 0;
    switch (type) {
    case ClickType::NORMAL: base = 0; break;
    case ClickType::SOFT:   base = 2; break;
    case ClickType::HARD:   base = 4; break;
    case ClickType::MICRO:  base = 6; break;
    }
    return pressed ? base : base + 1;
}

// For each role: which roles to try, in order, if the pack has no folder for it.
const std::vector<int>& roleChain(int role) {
    // roles: 0 click, 1 release, 2 soft, 3 softrelease, 4 hard, 5 hardrelease, 6 micro, 7 microrelease
    static const std::array<std::vector<int>, 8> chains{{
        {0}, {1}, {2, 0}, {3, 1}, {4, 0}, {5, 1}, {6, 2, 0}, {7, 3, 1},
    }};
    return chains[role];
}

// Groups: 0 p1 jump, 1 p1 left, 2 p1 right, 3 p2 jump, 4 p2 left, 5 p2 right.
// A missing group falls back to the same player's jump sounds, then to player 1's.
std::vector<int> groupChain(int group) {
    switch (group) {
    case 0: return {0};
    case 1: return {1, 0};
    case 2: return {2, 0};
    case 3: return {3, 0};
    case 4: return {4, 3, 1, 0};
    case 5: return {5, 3, 2, 0};
    }
    return {0};
}

const std::vector<AudioFile>* resolvePool(const ClickFiles& files, int group, int role) {
    for (int g : groupChain(group)) {
        for (int r : roleChain(role)) {
            const auto& pool = files[g * 8 + r];
            if (!pool.empty()) return &pool;
        }
    }
    return nullptr;
}

short toShort(float v) {
    float s = v * 32768.0f;
    if (s > 32767.0f) s = 32767.0f;
    if (s < -32768.0f) s = -32768.0f;
    return static_cast<short>(std::lrintf(s));
}

struct Event {
    double time;
    const AudioFile* sound;
};

} // namespace

std::vector<AudioFile> loadAudioFolder(const fs::path& folder, int* skipped) {
    std::vector<AudioFile> files;
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) return files;

    std::vector<fs::path> paths;
    for (const auto& item : fs::directory_iterator(folder, ec)) {
        std::error_code ec2;
        if (item.is_regular_file(ec2)) paths.push_back(item.path());
    }
    std::sort(paths.begin(), paths.end());

    for (const auto& p : paths) {
        AudioFile f;
        if (decodeFile(p, f)) {
            files.push_back(std::move(f));
        } else if (skipped) {
            ++(*skipped);
        }
    }
    return files;
}

AudioFile loadNoiseFile(const fs::path& folder) {
    AudioFile f;
    for (const char* name : {"noise.wav", "whitenoise.wav"}) {
        std::error_code ec;
        fs::path p = folder / name;
        if (fs::is_regular_file(p, ec) && decodeFile(p, f)) return f;
    }
    return AudioFile{};
}

RenderResult renderMacroToWav(Macro& macro, const ClickFiles& clickFiles, const AudioFile* noise,
                              const fs::path& outPath) {
    RenderResult result;

    // ---- pick the output sample rate: the most common one in the clickpack
    std::map<int, int> rateCount;
    for (const auto& pool : clickFiles)
        for (const auto& f : pool)
            if (f.sampleRate > 0) ++rateCount[f.sampleRate];
    if (rateCount.empty()) {
        result.message = "The clickpack has no readable sounds.";
        return result;
    }
    int rate = std::max_element(rateCount.begin(), rateCount.end(),
                                [](auto& a, auto& b) { return a.second < b.second; })->first;

    // ---- resample pools to that rate (only the ones that need it, once each)
    std::map<const std::vector<AudioFile>*, std::vector<AudioFile>> resampled;
    auto usablePool = [&](const std::vector<AudioFile>* pool) -> const std::vector<AudioFile>* {
        if (!pool) return nullptr;
        bool needs = std::any_of(pool->begin(), pool->end(), [&](const AudioFile& f) { return f.sampleRate != rate; });
        if (!needs) return pool;
        auto it = resampled.find(pool);
        if (it == resampled.end()) {
            std::vector<AudioFile> copy;
            copy.reserve(pool->size());
            for (const auto& f : *pool) copy.push_back(resample(f, rate));
            it = resampled.emplace(pool, std::move(copy)).first;
        }
        return &it->second;
    };

    // ---- collect every click/release with the sound that will play for it
    std::mt19937 rng{std::random_device{}()};
    std::vector<Event> events;
    double fps = macro.getFps();

    auto addInputs = [&](std::vector<Input>& inputs, int frame, bool player2) {
        for (Input& input : inputs) {
            int button = static_cast<int>(input.getButton());
            if (button < 1 || button > 3) continue;
            int group = (player2 ? 3 : 0) + (button - 1);
            int role = roleFor(input.getClickType(), input.isPressed());
            const std::vector<AudioFile>* pool = usablePool(resolvePool(clickFiles, group, role));
            if (!pool || pool->empty()) {
                ++result.clicksSkipped;
                continue;
            }
            std::uniform_int_distribution<size_t> pick(0, pool->size() - 1);
            events.push_back({frame / fps, &(*pool)[pick(rng)]});
        }
    };
    for (Action& action : macro.getActions()) {
        addInputs(action.getPlayerOneInputs(), action.getFrame(), false);
        addInputs(action.getPlayerTwoInputs(), action.getFrame(), true);
    }

    if (events.empty()) {
        result.message = "None of the macro's clicks have a matching sound in this clickpack.";
        return result;
    }

    // ---- work out the length of the output
    size_t totalFrames = 0;
    for (const Event& e : events) {
        size_t start = static_cast<size_t>(std::llround(e.time * rate));
        totalFrames = std::max(totalFrames, start + e.sound->frames());
    }
    totalFrames += static_cast<size_t>(rate); // 1s of tail so nothing gets cut off

    if (totalFrames > static_cast<size_t>(rate) * 60 * 60 * 3) {
        result.message = "The macro is longer than 3 hours - that looks wrong (check the macro's FPS).";
        return result;
    }

    std::vector<float> mix;
    try {
        mix.assign(totalFrames * 2, 0.0f);
    } catch (const std::bad_alloc&) {
        result.message = "Not enough memory to render this macro.";
        return result;
    }

    // ---- background noise, looped across the whole file
    if (noise && !noise->empty()) {
        AudioFile n = resample(*noise, rate);
        size_t nf = n.frames();
        if (nf > 0) {
            for (size_t i = 0; i < totalFrames; ++i) {
                size_t j = i % nf;
                mix[i * 2] += n.samples[j * 2];
                mix[i * 2 + 1] += n.samples[j * 2 + 1];
            }
        }
    }

    // ---- mix the clicks in
    for (const Event& e : events) {
        size_t start = static_cast<size_t>(std::llround(e.time * rate));
        const auto& s = e.sound->samples;
        size_t n = e.sound->frames();
        for (size_t i = 0; i < n; ++i) {
            size_t o = (start + i) * 2;
            if (o + 1 >= mix.size()) break;
            mix[o] += s[i * 2];
            mix[o + 1] += s[i * 2 + 1];
        }
        ++result.clicksMixed;
    }

    // ---- write a plain 16-bit stereo WAV
    std::vector<short> out(mix.size());
    for (size_t i = 0; i < mix.size(); ++i) out[i] = toShort(mix[i]);
    mix.clear();
    mix.shrink_to_fit();

    SF_INFO info{};
    info.samplerate = rate;
    info.channels = 2;
    info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;

    SNDFILE* file = openFile(outPath, SFM_WRITE, &info);
    if (!file) {
        result.message = std::string("Couldn't create the output file: ") + sf_strerror(nullptr);
        return result;
    }
    sf_count_t written = sf_writef_short(file, out.data(), static_cast<sf_count_t>(totalFrames));
    sf_close(file);
    if (written != static_cast<sf_count_t>(totalFrames)) {
        result.message = "Writing the WAV file failed (disk full?).";
        return result;
    }

    if (result.clicksSkipped > 0) {
        result.message = std::to_string(result.clicksSkipped) +
                         " click(s) had no matching sound in the clickpack and were left silent.";
    }
    result.ok = true;
    return result;
}
