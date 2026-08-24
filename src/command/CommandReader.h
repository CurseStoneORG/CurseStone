// CommandReader — runs a background thread that reads stdin line by
// line. When a line is received, it forwards it to the Server's
// CommandMap. Mirrors pocketmine/command/CommandReader.
#pragma once
#include <thread>
#include <atomic>
#include <functional>
#include <iostream>
#include <string>
namespace atomikpe {
class CommandReader {
public:
    using LineCallback = std::function<void(const std::string&)>;
    explicit CommandReader(LineCallback cb) : cb_(std::move(cb)) {}
    ~CommandReader() { stop(); }
    void start() {
        running_ = true;
        thread_ = std::thread([this](){ this->loop(); });
    }
    void stop() {
        running_ = false;
        if (thread_.joinable()) thread_.detach();
    }
private:
    void loop() {
        std::string line;
        while (running_ && std::getline(std::cin, line)) {
            if (cb_) cb_(line);
        }
    }
    LineCallback cb_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};
} // namespace
