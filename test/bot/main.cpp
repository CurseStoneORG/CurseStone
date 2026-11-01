/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| \__/\ |_| |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_| |___/\___\____/ \__\___/|_| |_|\___/

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneTeam
 *
 * Developed by CurseStoneTeam.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneTeam/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later
// main.cpp — cursestone-bot command line.
//
//   cursestone-bot --host 127.0.0.1 --port 19132 --scenario chunks \
//                  --duration 20000 --capture capture.jsonl --username BotD -v
//
// Exit code 0 when the scenario's success condition was met.
#include "Bot.h"

#include <cstdio>
#include <cstring>
#include <string>

using csbot::BotClient;

static void usage() {
    std::printf(
        "cursestone-bot — protocol-84 test client\n"
        "\n"
        "Usage:\n"
        "  cursestone-bot [options]\n"
        "\n"
        "Options:\n"
        "  --host <ip>        server address (default 127.0.0.1)\n"
        "  --port <n>         server UDP port (default 19132)\n"
        "  --scenario <name>  login | chunks | move | chat | malformed | furnace (default login)\n"
        "  --duration <ms>    session length (default 15000; 30000 for chunks)\n"
        "  --username <name>  bot username (default BotD)\n"
        "  --capture <file>   JSONL capture of every server->client packet\n"
        "  -v                 verbose log to stderr\n"
        "\n"
        "Scenarios:\n"
        "  login      RakNet handshake + MCPE login; success = PlayStatus(0)\n"
        "  chunks     login + chunk radius; success = spawn + at least one chunk\n"
        "  move       chunks + MovePlayer every second while the session runs\n"
        "  chat       chunks + one chat TextPacket every 3 seconds\n"
        "  malformed  six corrupt packets, then an unconnected ping: the server\n"
        "             must survive all of them\n"
        "  furnace    scan the streamed chunks for a NATURAL furnace (village\n"
        "             house), walk to it and tap it twice with a close in\n"
        "             between — reproduces the village-furnace client crash;\n"
        "             success = two ContainerOpen windows fully cycled. Exit 3\n"
        "             = no natural furnace in the streamed chunks (skip, not a\n"
        "             server fault)\n");
}

int main(int argc, char** argv) {
    std::string host = "127.0.0.1";
    uint16_t port = 19132;
    std::string scenario = "login";
    std::string capture;
    std::string username = "BotD";
    uint64_t duration = 0;
    int radius = 8;
    double gotoX = 0.0, gotoZ = 0.0;
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const char* what) -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for %s\n", what);
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--host")
            host = next("--host");
        else if (a == "--port")
            port = (uint16_t)std::atoi(next("--port").c_str());
        else if (a == "--scenario")
            scenario = next("--scenario");
        else if (a == "--duration")
            duration = (uint64_t)std::atoll(next("--duration").c_str());
        else if (a == "--username")
            username = next("--username");
        else if (a == "--radius")
            radius = std::atoi(next("--radius").c_str());
        else if (a == "--goto") {
            // --goto X,Z: long march to (X,Z) before the furnace scan starts
            std::string v = next("--goto");
            auto comma = v.find(',');
            if (comma == std::string::npos) {
                std::fprintf(stderr, "--goto expects X,Z\n");
                return 2;
            }
            gotoX = std::atof(v.substr(0, comma).c_str());
            gotoZ = std::atof(v.substr(comma + 1).c_str());
        } else if (a == "--capture")
            capture = next("--capture");
        else if (a == "-v" || a == "--verbose")
            verbose = true;
        else if (a == "-h" || a == "--help") {
            usage();
            return 0;
        } else {
            std::fprintf(stderr, "unknown option: %s\n", a.c_str());
            usage();
            return 2;
        }
    }

    BotClient::Scenario sc;
    if (scenario == "login")
        sc = BotClient::Scenario::Login;
    else if (scenario == "chunks")
        sc = BotClient::Scenario::Chunks;
    else if (scenario == "move")
        sc = BotClient::Scenario::Move;
    else if (scenario == "chat")
        sc = BotClient::Scenario::Chat;
    else if (scenario == "malformed")
        sc = BotClient::Scenario::Malformed;
    else if (scenario == "furnace")
        sc = BotClient::Scenario::Furnace;
    else {
        std::fprintf(stderr, "unknown scenario: %s\n", scenario.c_str());
        return 2;
    }
    if (duration == 0) {
        if (sc == BotClient::Scenario::Login)
            duration = 15000;
        else if (sc == BotClient::Scenario::Furnace)
            duration = 60000;
        else
            duration = 30000;
    }

    BotClient bot;
    bot.setRadius(radius);
    if (gotoX != 0.0 || gotoZ != 0.0)
        bot.setGoto(gotoX, gotoZ);
    std::string err;
    if (!bot.open(host, port, err)) {
        std::fprintf(stderr, "bot: %s\n", err.c_str());
        return 1;
    }

    bool ok = bot.run(duration, sc, capture, username, verbose);

    std::printf("%s\n", bot.statsLine().c_str());
    if (!bot.lastError().empty())
        std::printf("error: %s\n", bot.lastError().c_str());
    if (bot.goalReached(sc)) {
        std::printf("result: PASS (%s)\n", scenario.c_str());
        return 0;
    }
    if (sc == BotClient::Scenario::Furnace && bot.noFurnaceFound()) {
        std::printf("result: SKIP (%s)\n", scenario.c_str());
        return 3;
    }
    std::printf("result: FAIL (%s)\n", scenario.c_str());
    (void)ok;
    return 1;
}
