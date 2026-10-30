#ifndef AUDIOFILE_H
#define AUDIOFILE_H
#include <vector>

// A decoded click/release sound: always interleaved stereo, float samples in [-1, 1].
struct AudioFile {
    int sampleRate{0};
    std::vector<float> samples{};

    bool empty() const { return samples.empty(); }
    size_t frames() const { return samples.size() / 2; }
};

#endif // AUDIOFILE_H
