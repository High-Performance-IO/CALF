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
#include <string>
#include <sys/file.h>
#include <sys/syscall.h>
#include <type_traits>
#include <unistd.h>

#include "format/BaseLogger.h"
#include "format/LogFormat.h"

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
    static pid_t processId() { return static_cast<pid_t>(calf_syscall(SYS_getpid)); }

    static long threadId() { return calf_syscall(SYS_gettid); }
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
        ensureFileOpen();
#ifdef CALF_LOG_FORMAT_PROTOBUF
        calf_syscall(SYS_flock, fileFD, LOCK_EX);
#endif
        int written = 0;
        while (written < len) {
            const long result = calf_syscall(SYS_write, fileFD, buf + written,
                                             static_cast<size_t>(len - written));
            if (result <= 0) {
#ifdef CALF_LOG_FORMAT_PROTOBUF
                calf_syscall(SYS_flock, fileFD, LOCK_UN);
#endif
                return;
            }
            written += static_cast<int>(result);
        }
#ifdef CALF_LOG_FORMAT_PROTOBUF
        calf_syscall(SYS_flock, fileFD, LOCK_UN);
#endif
    }

    static void rawWriteStr(const char *buf) {
        rawWriteBytes(buf, static_cast<int>(::strlen(buf)));
    }

    static void flush() {}

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
