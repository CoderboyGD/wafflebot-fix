#ifndef ACTION_H
#define ACTION_H
#include <vector>

#include "Input.h"

// All inputs (for both players) that happen on one frame.
class Action {
private:
    int m_frame{};
    std::vector<Input> m_playerOneInputs{};
    std::vector<Input> m_playerTwoInputs{};

public:
    explicit Action(int frame) : m_frame{frame} {}

    int getFrame() const { return m_frame; }
    std::vector<Input> &getPlayerOneInputs() { return m_playerOneInputs; }
    std::vector<Input> &getPlayerTwoInputs() { return m_playerTwoInputs; }

    void setPlayerOneInputs(const std::vector<Input> &p1) { m_playerOneInputs = p1; }
    void setPlayerTwoInputs(const std::vector<Input> &p2) { m_playerTwoInputs = p2; }
};
#endif // ACTION_H
