// SPDX-License-Identifier: LGPL-3.0-or-later
// SignChangeEvent.h — ported from cn.nukkit.event.block.SignChangeEvent
//
// Fired when a player edits the text on a sign. The four-line text
// payload can be modified by handlers before it is committed to the
// tile entity.
#pragma once
#include "BlockEvent.h"
#include "../Cancellable.h"
#include <array>
#include <cstdint>
#include <string>
namespace atomikpe { class Player; }
namespace atomikpe::event::block {

class SignChangeEvent : public BlockEvent, public Cancellable {
public:
    atomikpe::Player* player;
    std::array<std::string, 4> lines;

    SignChangeEvent(int32_t bx, int32_t by, int32_t bz,
                    atomikpe::Player* p,
                    std::array<std::string, 4> l)
        : BlockEvent(bx, by, bz), player(p), lines(std::move(l)) {}

    std::string getLine(int i) const {
        if (i < 0 || i >= 4) return {};
        return lines[i];
    }
    void setLine(int i, const std::string& s) {
        if (i >= 0 && i < 4) lines[i] = s;
    }

    const std::string& eventName() const override {
        static std::string n = "SignChangeEvent"; return n;
    }
};

} // namespace atomikpe::event::block
