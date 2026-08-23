#include "ThreadManager.h"
namespace cenisyspe {
ThreadManager& ThreadManager::get() { static ThreadManager m; return m; }
void ThreadManager::joinAll() {
    for (auto& t : threads_) if (t->joinable()) t->join();
    threads_.clear();
}
} // namespace
