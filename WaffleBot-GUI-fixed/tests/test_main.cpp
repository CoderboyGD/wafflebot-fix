// Command line harness: wafflebot-test <clickpack folder> <macro> <out.wav> [noise]
// Prints what was parsed and renders the macro with the default thresholds.
#include <filesystem>
#include <iostream>
#include <map>

#include "AudioHandling.h"
#include "Macro.h"

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "usage: wafflebot-test <clickpack> <macro> <out.wav> [noise]\n";
        return 2;
    }
    fs::path pack = argv[1];

    static const std::map<std::string, int> names{
        {"clicks", 0}, {"releases", 1}, {"softclicks", 2}, {"softreleases", 3},
        {"hardclicks", 4}, {"hardreleases", 5}, {"microclicks", 6}, {"microreleases", 7},
    };
    static const std::pair<const char*, int> groups[] = {
        {"", 0}, {"left1", 1}, {"right1", 2}, {"player2", 3}, {"left2", 4}, {"right2", 5}};

    ClickFiles files;
    int skipped = 0;
    for (auto& [g, gi] : groups) {
        for (auto& [n, ri] : names) {
            fs::path p = pack / g / n;
            for (auto& f : loadAudioFolder(p, &skipped)) files[gi * 8 + ri].push_back(std::move(f));
        }
    }
    std::cout << "skipped undecodable files: " << skipped << "\n";

    Macro macro{argv[2]};
    if (!macro.isLoaded()) {
        std::cout << "LOAD FAILED: " << macro.getError() << "\n";
        return 1;
    }
    std::cout << "format: " << macro.getFormat() << "\nfps: " << macro.getFps()
              << "\nactions: " << macro.getActions().size() << "\nlast frame: " << macro.getFrameCount()
              << "\ntwoPlayer: " << macro.isTwoPlayer() << " platformer: " << macro.isPlatformer() << "\n";

    macro.determineClickTypes(0.05, 0.03, 0.15, 0.1, 2.0, 1.5);
    int counts[4][2][2] = {};
    for (auto& a : macro.getActions()) {
        for (auto* v : {&a.getPlayerOneInputs(), &a.getPlayerTwoInputs()})
            for (auto& i : *v) counts[(int)i.getClickType()][i.isPressed()][0]++;
    }
    const char* tn[] = {"micro", "soft", "normal", "hard"};
    for (int t = 0; t < 4; t++)
        std::cout << tn[t] << ": clicks=" << counts[t][1][0] << " releases=" << counts[t][0][0] << "\n";

    AudioFile noise;
    if (argc > 4) noise = loadNoiseFile(argv[4]);
    auto res = renderMacroToWav(macro, files, noise.empty() ? nullptr : &noise, argv[3]);
    std::cout << "render ok=" << res.ok << " mixed=" << res.clicksMixed << " skipped=" << res.clicksSkipped
              << " msg=" << res.message << "\n";
    return res.ok ? 0 : 1;
}
