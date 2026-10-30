#include "Macro.h"
#include "MacroLoader.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

namespace {

// Strip "foo.gdr.json" / "foo.mhr.json" down to "foo" so the output is "foo.wav"
std::string cleanMacroName(const std::string& filepath) {
    std::filesystem::path p{std::u8string(filepath.begin(), filepath.end())};
    std::u8string stem = p.stem().u8string();
    std::string name(stem.begin(), stem.end());
    for (const char* inner : {".gdr", ".mhr", ".echo"}) {
        std::string suffix{inner};
        if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
            name.erase(name.size() - suffix.size());
            break;
        }
    }
    return name;
}

} // namespace

Macro::Macro(const std::string& filepath) {
    m_name = cleanMacroName(filepath);

    RawMacro raw;
    if (!loadRawMacro(filepath, raw, m_error)) {
        return;
    }

    m_fps = raw.fps;
    m_format = raw.format;

    // Group inputs by frame (stable, so the order inside a frame is kept:
    // e.g. a release followed by a click on the same frame stays in that order)
    std::stable_sort(raw.inputs.begin(), raw.inputs.end(),
                     [](const RawInput& a, const RawInput& b) { return a.frame < b.frame; });

    int lastFrame = 0;
    for (const RawInput& in : raw.inputs) {
        if (in.frame < 0) continue;
        int frame = static_cast<int>(in.frame);
        if (m_actions.empty() || m_actions.back().getFrame() != frame) {
            m_actions.emplace_back(frame);
        }
        Input input{static_cast<Button>(in.button), in.down, ClickType::NORMAL};
        if (in.player2) {
            m_actions.back().getPlayerTwoInputs().push_back(input);
        } else {
            m_actions.back().getPlayerOneInputs().push_back(input);
        }
        lastFrame = frame;
    }
    m_frameCount = lastFrame;

    if (m_actions.empty()) {
        m_error = "the macro contains no usable clicks";
        return;
    }

    m_isLoaded = true;

    // xdBot writes the wrong 2p flag on 2 player macros
    if (raw.swapPlayers && isTwoPlayer()) {
        swapPlayerOneAndTwoActions();
    }
}

namespace {

struct ButtonState {
    bool seen{false};
    int lastFrame{0};      // frame of the previous input of this button (a release, when a click comes in)
    int lastLastFrame{0};  // frame of the input before that (the previous click)
    ClickType lastClickType{ClickType::NORMAL}; // type of the last click, used for its release
};

void classify(std::vector<Input>& inputs, int currentFrame, double fps,
              std::array<ButtonState, 4>& states,
              double microClickTime, double microClickAfterReleaseTime,
              double softClickTime, double softClickAfterReleaseTime,
              double hardClickTime, double hardClickAfterReleaseTime) {
    for (Input& input : inputs) {
        int button = static_cast<int>(input.getButton());
        if (button < 1 || button > 3) continue;
        ButtonState& s = states[button];

        if (!input.isPressed()) {
            // a release sounds like the click it belongs to
            input.setClickType(s.lastClickType);
        } else {
            ClickType type = ClickType::NORMAL;
            if (s.seen) {
                double sinceRelease = static_cast<double>(currentFrame - s.lastFrame) / fps;
                double sinceClick = static_cast<double>(currentFrame - s.lastLastFrame) / fps;

                if (sinceRelease < softClickAfterReleaseTime || sinceClick < softClickTime) {
                    type = ClickType::SOFT;
                } else if (sinceRelease > hardClickAfterReleaseTime || sinceClick > hardClickTime) {
                    type = ClickType::HARD;
                } else if (sinceRelease < microClickAfterReleaseTime || sinceClick < microClickTime) {
                    type = ClickType::MICRO;
                }
            }
            input.setClickType(type);
            s.lastClickType = type;
        }

        s.lastLastFrame = s.seen ? s.lastFrame : currentFrame;
        s.lastFrame = currentFrame;
        s.seen = true;
    }
}

} // namespace

void Macro::determineClickTypes(
    double microClickTime,
    double microClickAfterReleaseTime,
    double softClickTime,
    double softClickAfterReleaseTime,
    double hardClickTime,
    double hardClickAfterReleaseTime) {

    std::array<ButtonState, 4> p1{};
    std::array<ButtonState, 4> p2{};

    for (Action& action : m_actions) {
        int frame = action.getFrame();
        classify(action.getPlayerOneInputs(), frame, m_fps, p1,
                 microClickTime, microClickAfterReleaseTime, softClickTime, softClickAfterReleaseTime,
                 hardClickTime, hardClickAfterReleaseTime);
        classify(action.getPlayerTwoInputs(), frame, m_fps, p2,
                 microClickTime, microClickAfterReleaseTime, softClickTime, softClickAfterReleaseTime,
                 hardClickTime, hardClickAfterReleaseTime);
    }
}

bool Macro::isTwoPlayer() {
    bool hasPlayer1 = std::any_of(m_actions.begin(), m_actions.end(),
        [](Action& a) { return !a.getPlayerOneInputs().empty(); });

    bool hasPlayer2 = std::any_of(m_actions.begin(), m_actions.end(),
        [](Action& a) { return !a.getPlayerTwoInputs().empty(); });

    return hasPlayer1 && hasPlayer2;
}

bool Macro::isPlatformer() {
    for (Action& action : m_actions) {
        for (Input& input : action.getPlayerOneInputs()) {
            if (input.getButton() == Button::LEFT || input.getButton() == Button::RIGHT) return true;
        }
        for (Input& input : action.getPlayerTwoInputs()) {
            if (input.getButton() == Button::LEFT || input.getButton() == Button::RIGHT) return true;
        }
    }
    return false;
}

void Macro::swapPlayerOneAndTwoActions() {
    for (Action& action : m_actions) {
        std::swap(action.getPlayerOneInputs(), action.getPlayerTwoInputs());
    }
}
