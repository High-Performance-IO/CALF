#include "SyscallLogger.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/syscall.h>
#include <type_traits>
#include <unistd.h>

template <typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
static long to_syscall_arg(T v) {
    return static_cast<long>(v);
}

template <typename T> static long to_syscall_arg(T *v) { return reinterpret_cast<long>(v); }

template <typename T> static long to_syscall_arg(const T *v) { return reinterpret_cast<long>(v); }

template <typename... Args> static inine long captura_syscall(long nr, Args &&...args) {
    return SyscallLogger::syscallFn(nr, to_syscall_arg(args)...);
}

thread_local int SyscallLogger::fileFD              = -1;
thread_local char SyscallLogger::filePath[PATH_MAX] = {'\0'};
SyscallLogger::SyscallFn SyscallLogger::syscallFn   = ::syscall;

SyscallLogger::SyscallLogger() { ensureFileOpen(); }

void SyscallLogger::rawWriteBytes(const char *buf, int len) {
    ensureFileOpen();
    captura_syscall(SYS_write, fileFD, buf, static_cast<size_t>(len));
}

void SyscallLogger::rawWriteStr(const char *buf) {
    rawWriteBytes(buf, static_cast<int>(::strlen(buf)));
}

std::string SyscallLogger::getLogFileName() const {
    return filePath[0] != '\0' ? std::string(filePath) : std::string{};
}

void SyscallLogger::ensureFileOpen() {
    if (fileFD != -1) {
        return;
    }

    ::snprintf(filePath, PATH_MAX, "%s/%s%ld.log", getHostLogDir(), getLogPrefix(),
               captura_syscall(SYS_gettid));

    // Create directory hierarchy.
    captura_syscall(SYS_mkdirat, AT_FDCWD, getLogDir(), 0755);
    captura_syscall(SYS_mkdirat, AT_FDCWD, getSyscallLogDir(), 0755);
    captura_syscall(SYS_mkdirat, AT_FDCWD, getHostLogDir(), 0755);

    fileFD = static_cast<int>(
        captura_syscall(SYS_openat, AT_FDCWD, filePath, O_CREAT | O_WRONLY | O_APPEND, 0644));

    if (fileFD == -1) {
        const char *prefix = "Captura: failed to open log file: ";
        captura_syscall(SYS_write, STDOUT_FILENO, prefix, ::strlen(prefix));
        captura_syscall(SYS_write, STDOUT_FILENO, filePath, ::strlen(filePath));
        captura_syscall(SYS_write, STDOUT_FILENO, " ", 1UL);
        const char *err = ::strerror(errno);
        captura_syscall(SYS_write, STDOUT_FILENO, err, ::strlen(err));
        captura_syscall(SYS_write, STDOUT_FILENO, "\n", 1UL);
        ::exit(EXIT_FAILURE);
    }
}

const char *SyscallLogger::getHostname() {
    static char hostname[HOST_NAME_MAX]{'\0'};
    if (hostname[0] == '\0') {
        ::gethostname(hostname, HOST_NAME_MAX);
    }
    return hostname;
}

const char *SyscallLogger::getLogDir() {
    static char *dir = nullptr;
    if (dir == nullptr) {
        const char *env = std::getenv("CAPTURA_LOG_DIR");
        if (env != nullptr) {
            dir = new char[::strlen(env) + 1];
            ::strcpy(dir, env);
        } else {
            dir = new char[::strlen(CAPIO_DEFAULT_LOG_FOLDER) + 1];
            ::strcpy(dir, CAPIO_DEFAULT_LOG_FOLDER);
        }
    }
    return dir;
}

const char *SyscallLogger::getLogPrefix() {
    static char *prefix = nullptr;
    if (prefix == nullptr) {
        const char *env = std::getenv("CAPTURA_LOG_PREFIX");
        if (env != nullptr) {
            prefix = new char[::strlen(env) + 1];
            ::strcpy(prefix, env);
        } else {
            prefix = new char[::strlen(CAPIO_LOG_POSIX_DEFAULT_LOG_FILE_PREFIX) + 1];
            ::strcpy(prefix, CAPIO_LOG_POSIX_DEFAULT_LOG_FILE_PREFIX);
        }
    }
    return prefix;
}

const char *SyscallLogger::getSyscallLogDir() {
    static char *dir = nullptr;
    if (dir == nullptr) {
        const char *base = getLogDir();
        dir              = new char[::strlen(base) + 9]{0}; // "/syscall\0"
        ::sprintf(dir, "%s/syscall", base);
    }
    return dir;
}

const char *SyscallLogger::getHostLogDir() {
    static char *dir = nullptr;
    if (dir == nullptr) {
        const char *parent = getSyscallLogDir();
        dir                = new char[::strlen(parent) + HOST_NAME_MAX]{0};
        ::sprintf(dir, "%s/%s", parent, getHostname());
    }
    return dir;
}