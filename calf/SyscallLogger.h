#ifndef CALF_SYSCALLLOGGER_H
#define CALF_SYSCALLLOGGER_H

#if defined(__linux__)

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/syscall.h>
#include <type_traits>
#include <unistd.h>

#include "BaseLogger.h"
#include "JsonBaseLogger.h"

struct SyscallLogger : JsonLogBase<SyscallLogger> {

    static thread_local int fileFD;
    static thread_local char filePath[PATH_MAX];

    // Syscall function pointer — defaults to ::syscall.
    using SyscallFn                                = long (*)(long, ...);
    inline static thread_local SyscallFn syscallFn = ::syscall;

    static void setSyscallFn(SyscallFn fn) { syscallFn = fn; }

    explicit SyscallLogger() { ensureFileOpen(); }

    static std::string getLogFileName() {
        return filePath[0] != '\0' ? std::string(filePath) : std::string{};
    }

    static void rawWriteBytes(const char *buf, int len) {
        ensureFileOpen();
        calf_syscall(SYS_write, fileFD, buf, static_cast<size_t>(len));
    }

    static void rawWriteStr(const char *buf) {
        rawWriteBytes(buf, static_cast<int>(::strlen(buf)));
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
        if (fileFD != -1) {
            return;
        }

        ::snprintf(filePath, PATH_MAX, "%s/%s%ld.log", getHostLogDir(), getLogPrefix(),
                   calf_syscall(SYS_gettid));

        calf_syscall(SYS_mkdirat, AT_FDCWD, getLogDir(), 0755);
        calf_syscall(SYS_mkdirat, AT_FDCWD, getSyscallLogDir(), 0755);
        calf_syscall(SYS_mkdirat, AT_FDCWD, getHostLogDir(), 0755);

        fileFD = static_cast<int>(
            calf_syscall(SYS_openat, AT_FDCWD, filePath, O_CREAT | O_WRONLY | O_APPEND, 0644));

        if (fileFD == -1) {
            const char *msg = "CALF: failed to open log file\n";
            calf_syscall(SYS_write, STDOUT_FILENO, msg, ::strlen(msg));
            ::exit(EXIT_FAILURE);
        }
    }

    static const char *getHostname() {
        static char h[HOST_NAME_MAX]{'\0'};
        if (h[0] == '\0') {
            ::gethostname(h, HOST_NAME_MAX);
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
            const char *parent = getSyscallLogDir();
            d                  = new char[::strlen(parent) + HOST_NAME_MAX]{0};
            ::sprintf(d, "%s/%s", parent, getHostname());
        }
        return d;
    }
};

inline thread_local int SyscallLogger::fileFD               = -1;
inline thread_local char SyscallLogger::filePath[PATH_MAX] = {'\0'};

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
