// SPDX-License-Identifier: LGPL-3.0-or-later
// Diagnostics.cpp — implementation of the always-on diagnostic recorder.
#include "diagnostics/Diagnostics.h"

#include "utils/MainLogger.hpp"
#include "network/protocol/Info.h"
#include "main.h"   // CURSESTONE_VERSION / MINECRAFT_VERSION / PROTOCOL_VERSION

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

#if defined(__unix__) || defined(__APPLE__)
#  include <sys/utsname.h>
#  include <unistd.h>   // getpid
#endif
#if defined(_WIN32)
#  include <process.h>
#  define getpid _getpid
#endif

namespace cursestone {
namespace diagnostics {

namespace {

uint64_t nowWallMs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

// Human-readable name for a protocol-84 packet id. Falls back to "UNKNOWN".
const char* packetName(uint8_t pid) {
    using namespace cursestone::network::protocol;
    switch (pid) {
        case LOGIN_PACKET: return "Login";
        case PLAY_STATUS_PACKET: return "PlayStatus";
        case SERVER_TO_CLIENT_HANDSHAKE: return "ServerToClientHandshake";
        case CLIENT_TO_SERVER_HANDSHAKE: return "ClientToServerHandshake";
        case DISCONNECT_PACKET: return "Disconnect";
        case BATCH_PACKET: return "Batch";
        case TEXT_PACKET: return "Text";
        case SET_TIME_PACKET: return "SetTime";
        case START_GAME_PACKET: return "StartGame";
        case ADD_PLAYER_PACKET: return "AddPlayer";
        case ADD_ENTITY_PACKET: return "AddEntity";
        case REMOVE_ENTITY_PACKET: return "RemoveEntity";
        case ADD_ITEM_ENTITY_PACKET: return "AddItemEntity";
        case TAKE_ITEM_ENTITY_PACKET: return "TakeItemEntity";
        case MOVE_ENTITY_PACKET: return "MoveEntity";
        case MOVE_PLAYER_PACKET: return "MovePlayer";
        case REMOVE_BLOCK_PACKET: return "RemoveBlock";
        case UPDATE_BLOCK_PACKET: return "UpdateBlock";
        case LEVEL_EVENT_PACKET: return "LevelEvent";
        case BLOCK_EVENT_PACKET: return "BlockEvent";
        case ENTITY_EVENT_PACKET: return "EntityEvent";
        case MOB_EQUIPMENT_PACKET: return "MobEquipment";
        case MOB_ARMOR_EQUIPMENT_PACKET: return "MobArmorEquipment";
        case INTERACT_PACKET: return "Interact";
        case USE_ITEM_PACKET: return "UseItem";
        case PLAYER_ACTION_PACKET: return "PlayerAction";
        case ANIMATE_PACKET: return "Animate";
        case RESPAWN_PACKET: return "Respawn";
        case DROP_ITEM_PACKET: return "DropItem";
        case CONTAINER_OPEN_PACKET: return "ContainerOpen";
        case CONTAINER_CLOSE_PACKET: return "ContainerClose";
        case CONTAINER_SET_SLOT_PACKET: return "ContainerSetSlot";
        case CONTAINER_SET_DATA_PACKET: return "ContainerSetData";
        case CONTAINER_SET_CONTENT_PACKET: return "ContainerSetContent";
        case CRAFTING_DATA_PACKET: return "CraftingData";
        case CRAFTING_EVENT_PACKET: return "CraftingEvent";
        case BLOCK_ENTITY_DATA_PACKET: return "BlockEntityData";
        case PLAYER_INPUT_PACKET: return "PlayerInput";
        case FULL_CHUNK_DATA_PACKET: return "FullChunkData";
        case SET_DIFFICULTY_PACKET: return "SetDifficulty";
        case CHANGE_DIMENSION_PACKET: return "ChangeDimension";
        case SET_PLAYER_GAMETYPE_PACKET: return "SetPlayerGameType";
        case PLAYER_LIST_PACKET: return "PlayerList";
        case REQUEST_CHUNK_RADIUS_PACKET: return "RequestChunkRadius";
        case CHUNK_RADIUS_UPDATED_PACKET: return "ChunkRadiusUpdated";
        case ADD_ITEM_PACKET: return "AddItem";
        default: return "UNKNOWN";
    }
}

std::string unameLine() {
#if defined(__unix__) || defined(__APPLE__)
    struct utsname u{};
    if (uname(&u) == 0) {
        return std::string(u.sysname) + " " + u.release + " " + u.machine;
    }
#endif
    return "(unknown)";
}

} // namespace

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

Diagnostics& Diagnostics::get() {
    static Diagnostics instance;
    return instance;
}

void Diagnostics::setDumpDir(const std::string& dir) {
    std::lock_guard<std::mutex> lk(mtx_);
    dump_dir_ = dir;
}

void Diagnostics::setUptimeStart(uint64_t wall_ms) {
    std::lock_guard<std::mutex> lk(mtx_);
    start_ms_ = wall_ms;
}

void Diagnostics::recordPacket(bool out, const std::string& who, uint8_t pid, size_t size) {
    std::lock_guard<std::mutex> lk(mtx_);
    char buf[200];
    std::snprintf(buf, sizeof(buf),
                  "%s %s %s pid=0x%02x (%s) size=%zu",
                  out ? "OUT ->" : "IN  <-", who.c_str(),
                  out ? "" : "", pid, packetName(pid), size);
    action_ = buf;
    packets_.push_back(PacketRecord{nowWallMs(), out, who, pid, size});
    if (packets_.size() > kMaxPacketRecords) packets_.erase(packets_.begin());
}

void Diagnostics::setAction(const std::string& action) {
    std::lock_guard<std::mutex> lk(mtx_);
    action_ = action;
}

void Diagnostics::report(const std::string& category, const std::string& message,
                         const std::string& detail, bool console) {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        errors_.push_back(ErrorRecord{nowWallMs(), category, message, detail});
        if (errors_.size() > kMaxErrorRecords) errors_.erase(errors_.begin());
    }
    if (console) {
        // Surface it on the console too — errors must never be silent.
        utils::MainLogger::get().warn("[" + category + "] " + message +
            (detail.empty() ? "" : (" | " + detail)));
    }
}

void Diagnostics::updatePlayers(std::vector<PlayerSnapshot> snapshot) {
    if (snapshot.size() > kMaxPlayerSnapshot) snapshot.resize(kMaxPlayerSnapshot);
    std::lock_guard<std::mutex> lk(mtx_);
    players_ = std::move(snapshot);
}

bool Diagnostics::busy() const {
    // try_lock on a const mutex requires a mutable handle; mtx_ is mutable.
    std::unique_lock<std::mutex> lk(mtx_, std::try_to_lock);
    return !lk.owns_lock();
}

std::string Diagnostics::wallToUtc(uint64_t wall_ms) {
    std::time_t t = static_cast<std::time_t>(wall_ms / 1000);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

std::string Diagnostics::shortTime(uint64_t wall_ms) {
    std::time_t t = static_cast<std::time_t>(wall_ms / 1000);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[16];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
    return buf;
}

// ---- locked renderers -------------------------------------------------

std::string Diagnostics::renderContextLocked() const {
    uint64_t now = nowWallMs();
    uint64_t uptime_s = (start_ms_ > 0 && now > start_ms_) ? (now - start_ms_) / 1000 : 0;

    std::ostringstream os;
    os << "CurseStone " << CURSESTONE_VERSION << " \"" << CURSESTONE_CODENAME << "\"\n";
    os << "Minecraft:   " << MINECRAFT_VERSION << " (protocol " << PROTOCOL_VERSION << ")\n";
    os << "Time (UTC):  " << wallToUtc(now) << "\n";
    os << "Uptime:      " << uptime_s << "s\n";
    os << "Tick:        " << tick_.load(std::memory_order_relaxed) << "\n";
    os << "Platform:    " << unameLine() << "\n";
    os << "PID:         " <<
#if defined(_WIN32)
        "(unavailable)"
#else
        static_cast<long>(::getpid())
#endif
       << "\n";
    os << "Players:     " << players_.size() << " online\n";
    os << "Last action: " << action_ << "\n";
    return os.str();
}

std::string Diagnostics::renderPlayersLocked() const {
    if (players_.empty()) return "(no players online)\n";
    std::ostringstream os;
    for (const auto& p : players_) {
        char line[320];
        std::snprintf(line, sizeof(line),
            "  %-16s eid=%-6lld %s:%u pos=(%.1f, %.1f, %.1f) chunk=(%d,%d) "
            "gamemode=%d connected=%s logged_in=%s spawned=%s container=%s idle=%llus\n",
            p.name.empty() ? "?" : p.name.c_str(),
            static_cast<long long>(p.eid),
            p.ip.empty() ? "?" : p.ip.c_str(), static_cast<unsigned>(p.port),
            p.x, p.y, p.z, p.cx, p.cz, p.gamemode,
            p.connected ? "yes" : "no",
            p.logged_in ? "yes" : "no",
            p.spawned ? "yes" : "no",
            p.open_container ? "yes" : "no",
            static_cast<unsigned long long>(p.ms_since_activity / 1000));
        os << line;
    }
    return os.str();
}

std::string Diagnostics::renderRecentErrorsLocked(size_t max) const {
    if (errors_.empty()) return "(no errors recorded)\n";
    std::ostringstream os;
    size_t start = errors_.size() > max ? errors_.size() - max : 0;
    for (size_t i = start; i < errors_.size(); ++i) {
        const auto& e = errors_[i];
        os << "  [" << shortTime(e.wall_ms) << "] " << e.category << ": " << e.message;
        if (!e.detail.empty()) os << "  (" << e.detail << ")";
        os << "\n";
    }
    return os.str();
}

std::string Diagnostics::renderRecentPacketsLocked(size_t max) const {
    if (packets_.empty()) return "(no packets recorded)\n";
    std::ostringstream os;
    size_t start = packets_.size() > max ? packets_.size() - max : 0;
    for (size_t i = start; i < packets_.size(); ++i) {
        const auto& p = packets_[i];
        char line[256];
        std::snprintf(line, sizeof(line),
            "  [%s] %s %-16s pid=0x%02x (%-22s) %zu B\n",
            shortTime(p.wall_ms).c_str(),
            p.out ? "OUT" : "IN ", p.who.c_str(), p.pid,
            packetName(p.pid), p.size);
        os << line;
    }
    return os.str();
}

// ---- public renderers (try_lock; signal-safe) -------------------------

std::string Diagnostics::renderContext() const {
    std::unique_lock<std::mutex> lk(mtx_, std::try_to_lock);
    if (!lk.owns_lock()) return "(diagnostics busy — context may be incomplete)\n";
    return renderContextLocked();
}

std::string Diagnostics::renderPlayers() const {
    std::unique_lock<std::mutex> lk(mtx_, std::try_to_lock);
    if (!lk.owns_lock()) return "(diagnostics busy — player list unavailable)\n";
    return renderPlayersLocked();
}

std::string Diagnostics::renderRecentErrors(size_t max) const {
    std::unique_lock<std::mutex> lk(mtx_, std::try_to_lock);
    if (!lk.owns_lock()) return "(diagnostics busy — error log unavailable)\n";
    return renderRecentErrorsLocked(max);
}

std::string Diagnostics::renderRecentPackets(size_t max) const {
    std::unique_lock<std::mutex> lk(mtx_, std::try_to_lock);
    if (!lk.owns_lock()) return "(diagnostics busy — packet log unavailable)\n";
    return renderRecentPacketsLocked(max);
}

std::string Diagnostics::lastErrorSummary() const {
    std::unique_lock<std::mutex> lk(mtx_, std::try_to_lock);
    if (!lk.owns_lock() || errors_.empty()) return {};
    const auto& e = errors_.back();
    std::string s = e.message;
    if (!e.detail.empty()) s += " (" + e.detail + ")";
    return s;
}

// ---- non-fatal reports ------------------------------------------------

std::string Diagnostics::writeReport(const std::string& category,
                                     const std::string& message,
                                     const std::string& detail) {
    uint64_t now = nowWallMs();
    std::string dir;
    std::string context, players, errs, pkts;
    uint64_t seq = 0;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        // Record it in the ring too, so a later crashdump shows the lead-up.
        errors_.push_back(ErrorRecord{now, category, message, detail});
        if (errors_.size() > kMaxErrorRecords) errors_.erase(errors_.begin());

        dir = dump_dir_;
        seq = report_seq_++;
        context = renderContextLocked();
        players = renderPlayersLocked();
        errs = renderRecentErrorsLocked(20);
        pkts = renderRecentPacketsLocked(40);
    }

    // Make the name filesystem-safe + unique (timestamp + sequence).
    std::string stamp = dir + "/crashdumps/error_" + wallToUtc(now);
    for (char& c : stamp) if (c == ':' || c == '-') c = '_';
    stamp += "_" + std::to_string(seq);

    const std::string json_path = stamp + ".json";
    const std::string log_path  = stamp + ".log";

    {
        std::ofstream f(json_path);
        if (f) {
            f << "{\n"
              << "  \"kind\": \"" << json_escape(category) << "\",\n"
              << "  \"message\": \"" << json_escape(message) << "\",\n"
              << "  \"detail\": \"" << json_escape(detail) << "\",\n"
              << "  \"time_utc\": \"" << json_escape(wallToUtc(now)) << "\",\n"
              << "  \"context\": \"" << json_escape(context) << "\",\n"
              << "  \"players\": \"" << json_escape(players) << "\",\n"
              << "  \"recent_errors\": \"" << json_escape(errs) << "\",\n"
              << "  \"recent_packets\": \"" << json_escape(pkts) << "\"\n"
              << "}\n";
        } else {
            return {};
        }
    }

    {
        std::ofstream lf(log_path);
        if (lf) {
            lf << "CurseStone error report\n"
               << "=======================\n"
               << "Kind:    " << category << "\n"
               << "Message: " << message << "\n";
            if (!detail.empty()) lf << "Detail:  " << detail << "\n";
            lf << "\n--- Context ---\n" << context
               << "\n--- Players ---\n" << players
               << "\n--- Recent errors ---\n" << errs
               << "\n--- Recent packets ---\n" << pkts;
        }
    }

    utils::MainLogger::get().error("[" + category + "] " + message +
        (detail.empty() ? "" : (" | " + detail)));
    utils::MainLogger::get().error("Report written to " + json_path);
    return json_path;
}

} // namespace diagnostics
} // namespace cursestone
