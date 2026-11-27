/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | |  \__ \/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
| |___/|_| |_|  |___/\___|\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */

#include "EffectCommand.h"
#include "Server.h"
#include "Player.h"
#include "entity/Effect.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace cursestone {

namespace {

// Lowercase with underscores, exactly as the vanilla command accepts them.
struct EffectName {
    const char* name;
    uint8_t id;
};

constexpr EffectName kEffectNames[] = {
    {"speed", entity::EFF_SPEED},
    {"slowness", entity::EFF_SLOWNESS},
    {"haste", entity::EFF_HASTE},
    {"mining_fatigue", entity::EFF_MINING_FATIGUE},
    {"strength", entity::EFF_STRENGTH},
    {"instant_health", entity::EFF_INSTANT_HEALTH},
    {"instant_damage", entity::EFF_INSTANT_DAMAGE},
    {"jump", entity::EFF_JUMP},
    {"nausea", entity::EFF_NAUSEA},
    {"regeneration", entity::EFF_REGENERATION},
    {"resistance", entity::EFF_RESISTANCE},
    {"fire_resistance", entity::EFF_FIRE_RESISTANCE},
    {"water_breathing", entity::EFF_WATER_BREATHING},
    {"invisibility", entity::EFF_INVISIBILITY},
    {"blindness", entity::EFF_BLINDNESS},
    {"night_vision", entity::EFF_NIGHT_VISION},
    {"hunger", entity::EFF_HUNGER},
    {"weakness", entity::EFF_WEAKNESS},
    {"poison", entity::EFF_POISON},
    {"wither", entity::EFF_WITHER},
    {"health_boost", entity::EFF_HEALTH_BOOST},
    {"absorption", entity::EFF_ABSORPTION},
    {"saturation", entity::EFF_SATURATION},
};

std::string normalize(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s)
        out.push_back((char)std::tolower((unsigned char)c));
    return out;
}

bool parseEffect(const std::string& token, uint8_t& id) {
    std::string norm = normalize(token);
    // Numeric id wins (protocol ids 1..23).
    if (!norm.empty() && std::all_of(norm.begin(), norm.end(), ::isdigit)) {
        long v = std::strtol(norm.c_str(), nullptr, 10);
        if (v >= 1 && v <= 23) {
            id = (uint8_t)v;
            return true;
        }
        return false;
    }
    for (const auto& n : kEffectNames) {
        if (norm == n.name) {
            id = n.id;
            return true;
        }
    }
    return false;
}

int parseSeconds(const std::string& token) {
    // Vanilla clamps negative durations to the 30-second default; "1"
    // would make a non-instant effect blink off, so the 0.15.x command
    // raised it to 5 seconds minimum.
    long v = std::strtol(token.c_str(), nullptr, 10);
    if (v < 0)
        return 30;
    if (v == 0 || v == 1)
        return 5;
    if (v > 1000000)
        return 1000000;
    return (int)v;
}

int parseAmplifier(const std::string& token) {
    long v = std::strtol(token.c_str(), nullptr, 10);
    if (v < 0)
        return 0;
    if (v > 255)
        return 255;
    return (int)v;
}

} // namespace

bool EffectCommand::execute(CommandSender* sender, const std::string&,
                            const std::vector<std::string>& args) {
    if (args.size() < 2) {
        sender->sendMessage(
            "Usage: /effect <player> <effect(id|name)> [seconds] [amplifier] | "
            "/effect <player> clear");
        return true;
    }

    Player* target = Server::getInstance().getPlayer(args[0]);
    if (!target) {
        sender->sendMessage("Player not found: " + args[0]);
        return true;
    }

    if (normalize(args[1]) == "clear") {
        target->removeAllEffects();
        sender->sendMessage("Removed all effects from " + target->getName());
        return true;
    }

    uint8_t effect_id = 0;
    if (!parseEffect(args[1], effect_id)) {
        sender->sendMessage("Unknown effect: " + args[1]);
        return true;
    }

    int seconds = args.size() >= 3 ? parseSeconds(args[2]) : 30;
    int amplifier = args.size() >= 4 ? parseAmplifier(args[3]) : 0;

    if (seconds == 0) {
        // Vanilla rule: a duration of 0 removes the effect.
        target->removeEffect(effect_id);
        sender->sendMessage("Removed effect " + std::to_string(effect_id) + " from " +
                            target->getName());
        return true;
    }

    target->addEffect(std::make_shared<entity::Effect>(
        entity::Effect::create(effect_id, seconds * 20, amplifier)));
    sender->sendMessage("Gave " + std::string(entity::Effect::nameOf(effect_id)) +
                        " (" + std::to_string(seconds) + "s, level " +
                        std::to_string(amplifier + 1) + ") to " + target->getName());
    return true;
}

} // namespace cursestone
