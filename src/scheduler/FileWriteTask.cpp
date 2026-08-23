#include "FileWriteTask.h"
#include <fstream>
namespace cenisyspe::scheduler {
void FileWriteTask::onRun() {
    std::ofstream f(path_, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(bytes_.data()), bytes_.size());
}
} // namespace
