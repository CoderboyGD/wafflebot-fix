#ifndef MACROLOADER_H
#define MACROLOADER_H

#include <cstdint>
#include <string>
#include <vector>

// One button press/release as read from any macro format.
struct RawInput {
    int64_t frame{};
    int button{1};   // 1 = jump, 2 = left, 3 = right
    bool player2{};
    bool down{};
};

struct RawMacro {
    double fps{};
    std::string format;            // human readable, e.g. "GDR (JSON)"
    bool swapPlayers{false};       // xdBot JSON quirk: 2p flag is inverted on 2-player macros
    std::vector<RawInput> inputs;  // in file order
};

// Detects the format (by content first, extension second) and parses it.
// Returns false and fills `error` on failure. Never throws, never exits.
bool loadRawMacro(const std::string& path, RawMacro& out, std::string& error);

// Text for the file-open dialog.
const char* supportedMacroFilter();

#endif // MACROLOADER_H
