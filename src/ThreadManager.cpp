#include "ThreadManager.h"
namespace cursestone {
ThreadManager& ThreadManager::get() { static ThreadManager m; return m; }
void ThreadManager::joinAll() {
    for (auto& t : threads_) if (t->joinable()) t->join();
    threads_.clear();
}
} // namespace
