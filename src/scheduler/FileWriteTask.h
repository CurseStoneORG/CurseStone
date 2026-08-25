// FileWriteTask — AsyncTask that writes a buffer to disk. Used by
// ChunkManager to flush chunks to anvil region files.
#pragma once
#include "Task.h"
#include <string>
#include <vector>
#include <cstdint>
namespace atomikpe::scheduler {
class FileWriteTask : public AsyncTask {
public:
    FileWriteTask(std::string path, std::vector<uint8_t> bytes)
        : path_(std::move(path)), bytes_(std::move(bytes)) {}
    void onRun() override;
private:
    std::string path_;
    std::vector<uint8_t> bytes_;
};
} // namespace
