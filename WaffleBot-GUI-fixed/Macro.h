#ifndef MACRO_H
#define MACRO_H
#include <string>
#include <vector>
#include "Action.h"

class Macro {
    std::string m_name{};
    std::string m_format{};
    std::string m_error{};
    double m_fps{};
    int m_frameCount{};
    std::vector<Action> m_actions{};
    bool m_isLoaded{false};

public:
    // filepath is UTF-8. Check isLoaded() / getError() afterwards.
    explicit Macro(const std::string& filepath);

    const std::string& getName() const { return m_name; }
    std::string getModifiableName() const { return m_name; }
    void setName(const std::string& name) { m_name = name; }

    const std::string& getFormat() const { return m_format; }
    const std::string& getError() const { return m_error; }

    double getFps() const { return m_fps; }
    int getFrameCount() const { return m_frameCount; }

    std::vector<Action>& getActions() { return m_actions; }

    bool isLoaded() const { return m_isLoaded; }

    void determineClickTypes(
        double microClickTime,
        double microClickAfterReleaseTime,
        double softClickTime,
        double softClickAfterReleaseTime,
        double hardClickTime,
        double hardClickAfterReleaseTime);

    bool isTwoPlayer();
    bool isPlatformer();
    void swapPlayerOneAndTwoActions();
};
#endif // MACRO_H
