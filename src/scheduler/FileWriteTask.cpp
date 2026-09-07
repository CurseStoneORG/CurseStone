#include "FileWriteTask.h"
#include "../utils/MainLogger.hpp"
#include <fstream>
#include <string>
namespace atomikpe::scheduler {
void FileWriteTask::onRun() {
    // FIX: previously this ignored every possible failure — a missing
    // parent directory, a full disk, a permissions error — and just
    // silently produced a truncated or absent file with the caller
    // (e.g. ChunkManager flushing a region file) never finding out.
    // Now failures are logged with the path, so a broken save shows
    // up in the log instead of being discovered later as silent data
    // loss (this also runs inside AsyncPool's try/catch, but that
    // only guards against exceptions — ofstream reports errors via
    // its failbit, not exceptions, unless explicitly configured to
    // throw, so it needs its own check).
    std::ofstream f(path_, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) {
        utils::MainLogger::get().warn(
            "[FileWriteTask] Failed to open for writing: " + path_);
        return;
    }
    f.write(reinterpret_cast<const char*>(bytes_.data()), bytes_.size());
    if (!f.good()) {
        utils::MainLogger::get().warn(
            "[FileWriteTask] Write failed or was incomplete: " + path_);
        return;
    }
    f.close();
    if (f.fail()) {
        utils::MainLogger::get().warn(
            "[FileWriteTask] Error closing file (data may not be flushed): " + path_);
    }
}
} // namespace
