#include "Input/KeyCode.hpp"

namespace ID
{
    KeyCode::StateMap KeyCode::s_keyStates;

    void KeyCode::init()
    {
        for(int i = static_cast<int>(KeyCodes::Space); i <= static_cast<int>(KeyCodes::Menu); ++i)
        {
            s_keyStates[i] = false;
        }
    }
} // namespace ID
