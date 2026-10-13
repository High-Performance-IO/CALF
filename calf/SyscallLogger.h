#ifndef CALF_SYSCALLLOGGER_H
#define CALF_SYSCALLLOGGER_H

#if defined(__linux__)

#include <atomic>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <pthread.h>
#include <string>
#include <sys/file.h>
#include <sys/syscall.h>
#include <type_traits>
#include <unistd.h>

#include "format/BaseLogger.h"
#include "format/LogFormat.h"

// Trace-event buffer capacity (bytes per thread). Events accumulate here and are flushed to
// disk in bulk when the buffer fills or the process exits. Tune up for even fewer syscalls on
// heavy syscall traces; keep modest if many threads each allocate one.
#ifndef CALF_LOG_BUFFER_CAPACITY
#define CALF_LOG_BUFFER_CAPACITY 65536
#endif

#ifdef CALF_LOG_FORMAT_PROTOBUF
#define CALF_SYSCALL_LOGGER_TYPE PerfettoSyscallLogger
#else
#define CALF_SYSCALL_LOGGER_TYPE JsonSyscallLogger
#endif

struct CALF_SYSCALL_LOGGER_TYPE : CalfLogBase<CALF_SYSCALL_LOGGER_TYPE> {

    static thread_local int fileFD;
    static thread_local char filePath[PATH_MAX];
#ifdef CALF_LOG_FORMAT_PROTOBUF
    static thread_local pid_t filePid;
    inline static std::atomic<int> traceFileState{0};
#endif

    // Caches for getpid()/gettid() so the per-event hot path issues no syscall for them.
    // pid/tid are constant for a thread's lifetime; the only invalidation is fork(), handled
    // by the atfork child hook below (clears them so the child re-reads its real pid/tid).
    static thread_local pid_t cachedPid;
    static thread_local long cachedTid;

    // Trace-event buffer: events append here and are written to disk in bulk only when the
    // buffer is full or the owning thread/process terminates. This avoids one write+flock
    // syscall per event. Each thread owns its buffer, matching the per-thread fileFD below.
    static constexpr std::size_t traceBufferCapacity = CALF_LOG_BUFFER_CAPACITY;
    static thread_local char traceBuffer[CALF_LOG_BUFFER_CAPACITY];
    static thread_local std::size_t traceBufferUsed;

    // Flush the owning thread's buffer when the thread exits (so short-lived worker threads
    // do not silently drop buffered events); the atexit handler covers the root thread.
    struct TraceFlushOnExit {
        ~TraceFlushOnExit() { flushTraceBuffer(); }
    };
#ifdef CALF_LOG_FORMAT_PROTOBUF
    inline static thread_local TraceFlushOnExit traceFlushGuard;
#endif

    // Write the accumulated trace bytes to disk (single flock+write+unflock), then reset.
    // The buffer must be empty on entry to the child path of fork(), so the atfork child hook
    // drops inherited in-flight bytes instead of writing a parent's events into the child file.
    static void flushTraceBuffer() {
        if (traceBufferUsed == 0) {
            return;
        }
        ensureFileOpen();
#ifdef CALF_LOG_FORMAT_PROTOBUF
        calf_syscall(SYS_flock, fileFD, LOCK_EX);
#endif
        std::size_t written = 0;
        while (written < traceBufferUsed) {
            const long result = calf_syscall(SYS_write, fileFD, traceBuffer + written,
                                             traceBufferUsed - written);
            if (result <= 0) {
                break;
            }
            written += static_cast<std::size_t>(result);
        }
#ifdef CALF_LOG_FORMAT_PROTOBUF
        calf_syscall(SYS_flock, fileFD, LOCK_UN);
#endif
        traceBufferUsed = 0;
    }

    static void registerLifecycleHandlers() {
        static const bool registered = [] {
            ::pthread_atfork(nullptr, nullptr, +[] {
                cachedPid = 0;
                cachedTid = 0;
                traceBufferUsed = 0;  // child file differs; inherit nothing
            });
            ::atexit(+[] { flushTraceBuffer(); });  // force-dump any partial tail on exit
            return true;
        }();
        (void)registered;
    }

    // Syscall function pointer — defaults to ::syscall.
    using SyscallFn                                = long (*)(long, ...);
    inline static thread_local SyscallFn syscallFn = ::syscall;

    static void setSyscallFn(SyscallFn fn) { syscallFn = fn; }

    static unsigned long currentTimestamp() {
        timespec now{};
#ifdef CALF_LOG_FORMAT_PROTOBUF
        constexpr clockid_t clock = CLOCK_MONOTONIC;
#else
        constexpr clockid_t clock = CLOCK_REALTIME;
#endif
        if (calf_syscall(SYS_clock_gettime, clock, &now) != 0) {
            return 0;
        }
#ifdef CALF_LOG_FORMAT_PROTOBUF
        return static_cast<unsigned long>(now.tv_sec) * 1000000000UL +
               static_cast<unsigned long>(now.tv_nsec);
#else
        static const unsigned long start = static_cast<unsigned long>(now.tv_sec) * 1000UL +
                                           static_cast<unsigned long>(now.tv_nsec) / 1000000UL;
        return static_cast<unsigned long>(now.tv_sec) * 1000UL +
               static_cast<unsigned long>(now.tv_nsec) / 1000000UL - start;
#endif
    }

#ifdef CALF_LOG_FORMAT_PROTOBUF
    static pid_t processId() {
        registerLifecycleHandlers();
        if (cachedPid != 0) {
            return cachedPid;
        }
        cachedPid = static_cast<pid_t>(calf_syscall(SYS_getpid));
        return cachedPid;
    }

    static long threadId() {
        registerLifecycleHandlers();
        if (cachedTid != 0) {
            return cachedTid;
        }
        cachedTid = calf_syscall(SYS_gettid);
        return cachedTid;
    }
#endif

    explicit CALF_SYSCALL_LOGGER_TYPE() {
        if (enable_logger) {
            ensureFileOpen();
        }
    }

    static std::string getLogFileName() {
        return filePath[0] != '\0' ? std::string(filePath) : std::string{};
    }

    static void rawWriteBytes(const char *buf, int len) {
#ifdef CALF_LOG_FORMAT_PROTOBUF
        const auto size = static_cast<std::size_t>(len);
        // Oversized single write goes straight out so we never copy it twice.
        if (size >= traceBufferCapacity) {
            flushTraceBuffer();
            ensureFileOpen();
            calf_syscall(SYS_flock, fileFD, LOCK_EX);
            std::size_t written = 0;
            while (written < size) {
                const long result = calf_syscall(SYS_write, fileFD, buf + written, size - written);
                if (result <= 0) {
                    break;
                }
                written += static_cast<std::size_t>(result);
            }
            calf_syscall(SYS_flock, fileFD, LOCK_UN);
            return;
        }
        if (traceBufferUsed + size > traceBufferCapacity) {
            flushTraceBuffer();  // make room
        }
        std::memcpy(traceBuffer + traceBufferUsed, buf, size);
        traceBufferUsed += size;
#else
        ensureFileOpen();
        int written = 0;
        while (written < len) {
            const long result =
                calf_syscall(SYS_write, fileFD, buf + written, static_cast<size_t>(len - written));
            if (result <= 0) {
                return;
            }
            written += static_cast<int>(result);
        }
#endif
    }

    static void rawWriteStr(const char *buf) {
        rawWriteBytes(buf, static_cast<int>(::strlen(buf)));
    }

    // Force-write any buffered bytes to disk now. Called by the atexit handler and by any
    // caller that needs durability before process/thread exit (buffered bytes are otherwise
    // lost when a thread terminates).
    static void forceFlush() {
#ifdef CALF_LOG_FORMAT_PROTOBUF
        flushTraceBuffer();
#endif
    }

    // Discard the per-thread caching. After a fork()/exec() or between isolated test scopes,
    // this forces the next processId()/threadId() to hit the real syscall once more.
    static void resetPidTidCache() {
        cachedPid = 0;
        cachedTid = 0;
    }

    // Called by ProtobufLogBase after every SliceEnd, i.e. once per recorded syscall.
    // Only a genuinely full buffer triggers a write; otherwise events stay buffered and are
    // flushed when the buffer fills or the process terminates (atexit).
    static void flush() {
#ifdef CALF_LOG_FORMAT_PROTOBUF
        if (traceBufferUsed >= traceBufferCapacity) {
            flushTraceBuffer();
        }
#endif
    }

    static void reopenRootArray() {
        ensureFileOpen();
        calf_syscall(SYS_lseek, fileFD, -2, SEEK_END);
        calf_syscall(SYS_write, fileFD, ",\n", static_cast<size_t>(2));
    }

  private:
    template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
    static long to_arg(T v) {
        return static_cast<long>(v);
    }

    template <typename T> static long to_arg(T *v) { return reinterpret_cast<long>(v); }

    template <typename T> static long to_arg(const T *v) { return reinterpret_cast<long>(v); }

    template <typename... Args> static long calf_syscall(long nr, Args &&...args) {
        return syscallFn(nr, to_arg(args)...);
    }

    static void ensureFileOpen() {
#ifdef CALF_LOG_FORMAT_PROTOBUF
        // odr-use the per-thread flush guard so it constructs (and its destructor flushes the
        // thread's buffer) for every thread that actually records events.
        (void)&traceFlushGuard;
        const auto currentPid = processId();
        if (fileFD != -1 && filePid != currentPid) {
            calf_syscall(SYS_close, fileFD);
            fileFD = -1;
            traceFileState.store(0, std::memory_order_release);
        }
#endif
        if (fileFD != -1) {
            return;
        }

#ifdef CALF_LOG_FORMAT_PROTOBUF
        int expected = 0;
        const bool initializesTrace = traceFileState.compare_exchange_strong(
            expected, 1, std::memory_order_acq_rel, std::memory_order_acquire);
        if (!initializesTrace) {
            while (traceFileState.load(std::memory_order_acquire) != 2) {
                calf_syscall(SYS_sched_yield);
            }
        }
        ::snprintf(filePath, PATH_MAX, "%s/%s_%ld%s", getHostLogDir(), CALF_COMPONENT_NAME,
                   static_cast<long>(currentPid), CALF_LOG_FILE_EXTENSION);
#else
        ::snprintf(filePath, PATH_MAX, "%s/%s%ld%s", getHostLogDir(), getLogPrefix(),
                   calf_syscall(SYS_gettid), CALF_LOG_FILE_EXTENSION);
#endif

        calf_syscall(SYS_mkdirat, AT_FDCWD, getLogDir(), 0755);
#ifndef CALF_LOG_FORMAT_PROTOBUF
        calf_syscall(SYS_mkdirat, AT_FDCWD, getSyscallLogDir(), 0755);
#endif
        calf_syscall(SYS_mkdirat, AT_FDCWD, getHostLogDir(), 0755);

#ifdef CALF_LOG_FORMAT_PROTOBUF
        const int openFlags = O_CREAT | O_RDWR | O_APPEND | (initializesTrace ? O_TRUNC : 0);
#else
        constexpr int openFlags = O_CREAT | O_RDWR | O_TRUNC;
#endif
        fileFD = static_cast<int>(
            calf_syscall(SYS_openat, AT_FDCWD, filePath, openFlags, 0644));

        if (fileFD == -1) {
            const char *msg = "CALF: failed to open log file\n";
            calf_syscall(SYS_write, STDOUT_FILENO, msg, ::strlen(msg));
            ::exit(EXIT_FAILURE);
        }
#ifdef CALF_LOG_FORMAT_PROTOBUF
        if (initializesTrace) {
            traceFileState.store(2, std::memory_order_release);
        }
        filePid = currentPid;
#endif
    }

    static const char *getHostname() {
        static char h[CALF_HOSTNAME_BUFFER_SIZE]{'\0'};
        if (h[0] == '\0') {
            ::gethostname(h, sizeof(h));
            h[sizeof(h) - 1] = '\0';
        }
        return h;
    }

    static const char *getLogDir() {
        static char *d = nullptr;
        if (d == nullptr) {
            const char *e   = std::getenv("CALF_LOG_DIR");
            const char *src = e ? e : CALF_DEFAULT_LOG_FOLDER;
            d               = new char[::strlen(src) + 1];
            ::strcpy(d, src);
        }
        return d;
    }

    static const char *getLogPrefix() {
        static char *p = nullptr;
        if (p == nullptr) {
            const char *e   = std::getenv("CALF_LOG_PREFIX");
            const char *src = e ? e : CALF_SYSCALL_DEFAULT_LOG_FILE_PREFIX;
            p               = new char[::strlen(src) + 1];
            ::strcpy(p, src);
        }
        return p;
    }

    static const char *getSyscallLogDir() {
        static char *d = nullptr;
        if (d == nullptr) {
            const char *base = getLogDir();
            d                = new char[::strlen(base) + 9]{0};
            ::sprintf(d, "%s/%s", base, CALF_COMPONENT_NAME);
        }
        return d;
    }

    static const char *getHostLogDir() {
        static char *d = nullptr;
        if (d == nullptr) {
#ifdef CALF_LOG_FORMAT_PROTOBUF
            const char *parent = getLogDir();
#else
            const char *parent = getSyscallLogDir();
#endif
            d                  = new char[::strlen(parent) + CALF_HOSTNAME_BUFFER_SIZE]{0};
            ::sprintf(d, "%s/%s", parent, getHostname());
        }
        return d;
    }
};

inline thread_local int CALF_SYSCALL_LOGGER_TYPE::fileFD               = -1;
inline thread_local char CALF_SYSCALL_LOGGER_TYPE::filePath[PATH_MAX] = {'\0'};
#ifdef CALF_LOG_FORMAT_PROTOBUF
inline thread_local pid_t CALF_SYSCALL_LOGGER_TYPE::filePid = 0;
#endif
inline thread_local pid_t CALF_SYSCALL_LOGGER_TYPE::cachedPid = 0;
inline thread_local long CALF_SYSCALL_LOGGER_TYPE::cachedTid  = 0;
inline thread_local char CALF_SYSCALL_LOGGER_TYPE::traceBuffer[CALF_SYSCALL_LOGGER_TYPE::traceBufferCapacity];
inline thread_local std::size_t CALF_SYSCALL_LOGGER_TYPE::traceBufferUsed = 0;

using SyscallLogger = CALF_SYSCALL_LOGGER_TYPE;
#undef CALF_SYSCALL_LOGGER_TYPE

using Logger = TemplateLogger<SyscallLogger>;

#ifdef CALF_LOG

#define LOG(message, ...) log.log(message, ##__VA_ARGS__)

#define START_LOG(tid, message, ...)                                                               \
    Logger log(__func__, __FILE__, __LINE__, tid, message, ##__VA_ARGS__)

#define ENABLE_LOGGER() enable_logger = true
#define DISABLE_LOGGER()                                                                           \
    SyscallLoggingSuspender sls {}

#define DBG(tid, lambda)                                                                           \
    {                                                                                              \
        START_LOG(tid, "[  DBG  ]~~~ START ~~~[  DBG  ]");                                         \
        lambda;                                                                                    \
        LOG("[  DBG  ]~~~ END   ~~~[  DBG  ]");                                                    \
    }

#define SET_CALF_SYSCALL_HANDLER(syscall_ptr) SyscallLogger::setSyscallFn(syscall_ptr)

#else

#define LOG(message, ...)
#define START_LOG(tid, message, ...)
#define DBG(tid, lambda)
#define ENABLE_LOGGER()
#define DISABLE_LOGGER()
#define SET_CALF_SYSCALL_HANDLER(syscall_ptr)

#endif

#endif

#endif
