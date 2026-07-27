#ifndef CALF_THREADID_H
#define CALF_THREADID_H

#include <cstdint>

#if defined(__APPLE__)
#include <pthread.h>
#elif defined(__linux__)
#include <sys/syscall.h>
#include <unistd.h>
#else
#include <functional>
#include <thread>
#endif

inline long calf_current_tid() {
#if defined(__APPLE__)
    std::uint64_t tid = 0;
    pthread_threadid_np(nullptr, &tid);
    return static_cast<long>(tid);
#elif defined(__linux__)
    return static_cast<long>(::syscall(SYS_gettid));
#else
    return static_cast<long>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
#endif
}

#endif // CALF_THREADID_H
