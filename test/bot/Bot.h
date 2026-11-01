/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| \__/\ |_| |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_| |___/\___\____/ \__\___/|_| |_|\___|

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
// Bot.h — standalone protocol-84 test client (RakNet + MCPE).
//
// A real 0.15.10-shaped client that talks to a running CurseStone server
// over UDP: RakNet offline handshake, reliable datagrams with ACK/NACK,
// MCPE login, chunk radius, movement and chat. Every server-to-client
// game packet can be captured to a JSONL file, which the companion
// differential validator (validate_capture.py) replays through the real
// client's own Packet::read() code to detect client crashes without a
// phone.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace csbot {

// ---------------------------------------------------------------------------
// Wire constants (protocol 84 / RakNet, must stay in sync with src/network).
// ---------------------------------------------------------------------------
constexpr uint8_t ID_OPEN_CONNECTION_REPLY_1 = 0x06;
constexpr uint8_t ID_OPEN_CONNECTION_REPLY_2 = 0x08;
constexpr uint8_t ID_CLIENT_CONNECT = 0x09;   // encapsulated
constexpr uint8_t ID_SERVER_HANDSHAKE = 0x10; // encapsulated
constexpr uint8_t ID_CLIENT_HANDSHAKE = 0x13; // encapsulated
constexpr uint8_t ID_CLIENT_DISCONNECT = 0x15;
constexpr uint8_t ID_RAK_PING = 0x00;
constexpr uint8_t ID_RAK_PONG = 0x03;
constexpr uint8_t ID_ACK = 0xc0;
constexpr uint8_t ID_NACK = 0xa0;

constexpr uint8_t MCPE_LOGIN = 0x01;
constexpr uint8_t MCPE_PLAY_STATUS = 0x02;
constexpr uint8_t MCPE_BATCH = 0x06;
constexpr uint8_t MCPE_TEXT = 0x07;
constexpr uint8_t MCPE_START_GAME = 0x09;
constexpr uint8_t MCPE_MOVE_PLAYER = 0x10;
constexpr uint8_t MCPE_USE_ITEM = 0x1f;        // block tap / place (client->server)
constexpr uint8_t MCPE_CONTAINER_OPEN = 0x2a;  // server->client container UI
constexpr uint8_t MCPE_CONTAINER_CLOSE = 0x2b;
constexpr uint8_t MCPE_CONTAINER_SET_CONTENT = 0x2e;
constexpr uint8_t MCPE_BLOCK_ENTITY_DATA = 0x32;
constexpr uint8_t MCPE_FULL_CHUNK = 0x34;
constexpr uint8_t MCPE_REQUEST_CHUNK_RADIUS = 0x3d;
constexpr uint8_t MCPE_CHUNK_RADIUS_UPDATED = 0x3e;

// furnace block ids (village houses place these naturally)
constexpr uint8_t BLOCK_FURNACE = 61;
constexpr uint8_t BLOCK_BURNING_FURNACE = 62;

constexpr uint8_t RELIABILITY_UNRELIABLE = 0;
constexpr uint8_t RELIABILITY_RELIABLE = 2;
constexpr uint8_t RELIABILITY_RELIABLE_ORDERED = 3;

constexpr uint16_t BOT_MTU = 1400; // max IP packet size, same as the server
constexpr size_t UDP_BUDGET = BOT_MTU - 28;      // IPv4 + UDP headers
constexpr size_t MAX_FRAG_PAYLOAD = UDP_BUDGET - 4 - 10 - 10; // datagram hdr + encap base + split

// ---------------------------------------------------------------------------
// BotClient
// ---------------------------------------------------------------------------
class BotClient {
  public:
    // Scenario drives what the client does after a successful login.
    enum class Scenario { Login, Chunks, Move, Chat, Malformed, Furnace };

    BotClient() = default;
    ~BotClient();

    BotClient(const BotClient&) = delete;
    BotClient& operator=(const BotClient&) = delete;

    // Resolves host, opens the UDP socket.
    bool open(const std::string& host, uint16_t port, std::string& err);

    // Full session: RakNet handshake + login + scenario traffic, running
    // until durationMs elapses. Returns true when the scenario's success
    // condition was met (see goalReached()).
    bool run(uint64_t durationMs, Scenario scenario, const std::string& capturePath,
             const std::string& username, bool verbose);

    // --- observable state -------------------------------------------------
    bool raknetConnected() const { return state_ >= State::Connected; }
    bool loginAccepted() const { return loginAccepted_; }
    bool spawned() const { return spawned_; }
    uint32_t chunksReceived() const { return chunksReceived_; }
    uint64_t chunkBytes() const { return chunkBytes_; }
    uint32_t startGameReceived() const { return startGameCount_; }
    const std::map<uint8_t, uint32_t>& packetCounts() const { return packetCounts_; }
    const std::string& lastError() const { return lastError_; }
    bool goalReached(Scenario s) const;
    // Furnace scenario: true when the streamed area simply had no natural
    // furnace (seed/terrain dependent) — a SKIP, not a server fault.
    bool noFurnaceFound() const { return noFurnace_; }
    // Chunk radius request (default 8, like the real client's render
    // distance option; larger = more terrain explored per run).
    void setRadius(int32_t r) { radius_ = r; }
    // Optional long-distance march for the furnace scenario: the server
    // caps every MovePlayer at 20 blocks, so the bot hops straight-line
    // in 15-block steps until it reaches the village area, letting the
    // chunk stream (and the furnace scan) run as it goes.
    void setGoto(double x, double z) {
        gotoX_ = x;
        gotoZ_ = z;
        goto_ = true;
    }
    std::string statsLine() const;

  private:
    enum class State { Unconnected, Connecting1, Connecting2, Connected, Closed };

    // --- sockets / io -----------------------------------------------------
    void sendRaw(const std::vector<uint8_t>& bytes);
    bool recvOnce(std::vector<uint8_t>& out, int timeoutMs);

    // --- raknet tx ---------------------------------------------------------
    void sendEncap(const std::vector<uint8_t>& payload, uint8_t reliability, uint8_t channel);
    void buildAndSendDatagram(const std::vector<uint8_t>& payload, uint8_t reliability,
                              uint8_t channel, bool hasSplit, uint16_t splitId,
                              uint32_t splitIndex, uint32_t splitCount);
    void sendSplit(const std::vector<uint8_t>& payload, uint8_t reliability, uint8_t channel);
    void sendOcr1();
    void sendOcr2();
    void sendClientConnect();
    void sendClientHandshake();
    void sendRakPing();
    void sendDisconnect();
    void flushAcks();
    void retransmitStale();

    // --- raknet rx ---------------------------------------------------------
    void handleDatagram(const std::vector<uint8_t>& dgram);
    void deliverEncap(std::vector<uint8_t> payload);
    void enqueueOrdered(uint8_t channel, uint32_t orderIndex, std::vector<uint8_t> payload,
                        bool isOrdered);
    void nackResend(uint32_t lo, uint32_t hi);
    void handleAck(const std::vector<uint8_t>& d, bool isNack);
    void handleOffline(const std::vector<uint8_t>& d);

    // --- mcpe --------------------------------------------------------------
    void handleMcpeFrame(std::vector<uint8_t> frame); // may be a batch, 0xFE stripped
    void handleMcpePacket(const std::vector<uint8_t>& pkt);
    void handleBatchBody(const uint8_t* body, size_t len);
    void doLogin(const std::string& username);
    void sendMcpe(const std::vector<uint8_t>& pkt, bool ordered);
    void sendRequestChunkRadius(int32_t radius);
    void sendMovePlayer(double x, double y, double z, float yaw, float pitch, bool onGround);
    void sendChat(const std::string& message);
    void sendMalformed(int kind);
    void sendUseItem(int32_t x, int32_t y, int32_t z, uint8_t face, double px, double py,
                     double pz);
    void sendContainerClose(uint8_t windowid);
    void scanChunkForFurnace(int32_t cx, int32_t cz, const std::vector<uint8_t>& payload);
    void furnaceTick(uint64_t now);

    // --- capture ------------------------------------------------------------
    void capture(const std::string& event, const std::vector<uint8_t>* data, uint8_t pid);
    void openCapture(const std::string& path);
    void closeCapture();

    // --- helpers ------------------------------------------------------------
    static uint64_t nowMs();
    void fail(const std::string& why);

    int fd_ = -1;
    std::string host_;
    uint16_t port_ = 0;
    uint64_t serverGuid_ = 0;
    uint16_t serverMtu_ = BOT_MTU;

    State state_ = State::Unconnected;
    uint64_t guid_ = 0;

    // tx counters (24-bit space, session lifetime is minutes — no wrap)
    uint32_t sendSeq_ = 0;
    uint32_t messageIndex_ = 0;
    uint32_t orderIndex_[32] = {0};
    uint16_t splitIdCounter_ = 0;

    // recovery: un-ACKed reliable datagrams for retransmit
    struct Recovery {
        std::vector<uint8_t> bytes;
        uint64_t timeMs;
        uint32_t retries = 0;
    };
    std::map<uint32_t, Recovery> recovery_;

    // rx state
    int64_t lastRecvSeq_ = -1;
    std::vector<uint32_t> ackQueue_;
    std::vector<std::pair<uint32_t, uint32_t>> nackQueue_;
    uint64_t lastAckFlushMs_ = 0;
    uint64_t lastPingMs_ = 0;

    struct Split {
        uint32_t count = 0;
        uint32_t got = 0;
        std::vector<std::vector<uint8_t>> parts;
    };
    std::map<uint16_t, Split> splits_;

    // ordered delivery hold (channel -> orderIndex -> payload)
    std::map<uint8_t, std::map<uint32_t, std::vector<uint8_t>>> hold_;
    uint32_t nextOrder_[32] = {0};
    bool startedOrder_[32] = {false};

    // mcpe state
    bool loginAccepted_ = false;
    bool spawned_ = false;
    bool radiusSent_ = false;
    uint32_t chunksReceived_ = 0;
    uint64_t chunkBytes_ = 0;
    uint32_t startGameCount_ = 0;
    std::map<uint8_t, uint32_t> packetCounts_;
    std::string username_;
    double moveX_ = 0.5, moveY_ = 40.0, moveZ_ = 0.5;
    uint64_t lastMoveMs_ = 0, lastChatMs_ = 0;
    uint32_t chatSent_ = 0;
    uint32_t malformedSent_ = 0;
    bool malformedDone_ = false;

    // furnace scenario state machine (0 wait-chunks, 1 walk, 2 touch,
    // 3 close, 4 re-touch, 5 re-close, 6 done)
    int furnacePhase_ = 0;
    int32_t furnaceX_ = 0, furnaceY_ = 0, furnaceZ_ = 0;
    bool furnaceFound_ = false;
    bool noFurnace_ = false;
    uint32_t walkSteps_ = 0;
    uint32_t useItemSent_ = 0;
    uint32_t closeSent_ = 0;
    uint32_t containerOpens_ = 0;      // ContainerOpen(0x2a) received
    uint32_t blockEntityDatas_ = 0;    // BlockEntityData(0x32) received
    uint8_t containerWindowId_ = 0;
    uint64_t lastFurnaceMs_ = 0;
    uint64_t phaseStartMs_ = 0;
    int32_t radius_ = 8;
    bool goto_ = false;
    double gotoX_ = 0.0, gotoZ_ = 0.0;
    bool gotoDone_ = false;
    uint32_t gotoSteps_ = 0;

    // capture
    FILE* cap_ = nullptr;
    uint64_t captureStartMs_ = 0;
    uint32_t bigPacketCaptured_ = 0; // full hex dumps of very large packets (chunks)

    std::string lastError_;
    bool verbose_ = false;
};

} // namespace csbot
