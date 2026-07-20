#ifndef CALF_BASELOGGER_H
#define CALF_BASELOGGER_H

#include <atomic>
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cxxabi.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <type_traits>
#include <unistd.h>

#include "constants.h"

inline std::atomic<bool> continue_on_error{false};

inline void raise_termination(const bool raise_exception, const std::string &message) {
    if (raise_exception) {
        throw std::runtime_error(message);
    }
    int write_res = write(fileno(stderr), message.c_str(), message.size());
    if (write_res == -1) {
        perror("write: error printing error msg!");
    }
    exit(EXIT_FAILURE);
}

inline long long current_time_in_millis() {
    timespec ts{};
    static const long long start_time = []() {
        timespec init_ts{};
        clock_gettime(CLOCK_REALTIME, &init_ts);
        return static_cast<long long>(init_ts.tv_sec) * 1000 + init_ts.tv_nsec / 1000000;
    }();

    clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<long long>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000 - start_time;
}

#ifdef __CALF_POSIX
inline thread_local bool enable_logger = false;
#else
// Server / non-POSIX: logging is always active from the first call.
inline thread_local bool enable_logger = true;
#endif

class SyscallLoggingSuspender {
    bool prev_;

  public:
    SyscallLoggingSuspender() : prev_(enable_logger) { enable_logger = false; }
    ~SyscallLoggingSuspender() { enable_logger = prev_; }
};

template <typename Adapter> class TemplateLogger {
    static thread_local int current_log_level;

    char invoker[256]{};
    char file[256]{};
    unsigned int line;
    long int tid;

    Adapter adapter;

  public:
    TemplateLogger(const char invoker[], const char file[], unsigned int line, long int tid,
                   const char *message, ...) {
        this->tid  = tid;
        this->line = line;
        strncpy(this->invoker, invoker, sizeof(this->invoker) - 1);
        strncpy(this->file, file, sizeof(this->file) - 1);

        if (!enable_logger) {
            return;
        }

        current_log_level++;

        va_list argp;
        va_start(argp, message);

        adapter.writeOpening(current_time_in_millis(), this->invoker, this->file, this->line,
                             message, argp);

        va_end(argp);
    }

    TemplateLogger(const TemplateLogger &)            = delete;
    TemplateLogger &operator=(const TemplateLogger &) = delete;

    ~TemplateLogger() {
        if (!enable_logger) {
            return;
        }

        adapter.writeEpilogue(static_cast<unsigned long>(current_time_in_millis()));

        current_log_level--;
    }

    void log(const char *message, ...) {
        if (!enable_logger) {
            return;
        }

        va_list argp;
        va_start(argp, message);
        adapter.printFormatted(current_time_in_millis(), this->invoker, this->file, this->line,
                               CALF_LOG_PRE_MSG, message, argp);
        va_end(argp);
    }

    std::string getLogFileName() { return adapter.getLogFileName(); }

    static void reset_log_level() { current_log_level = 0; }
};

template <typename T> inline thread_local int TemplateLogger<T>::current_log_level = 0;

#ifdef CALF_LOG

#define ERR_EXIT_EXCEPT_CHOICE(raise_exception, message, ...)                                      \
    log.log(message, ##__VA_ARGS__);                                                               \
    if (!continue_on_error) {                                                                      \
        char err_msg[CALF_LOG_MAX_MSG_LEN];                                                        \
        snprintf(err_msg, CALF_LOG_MAX_MSG_LEN, message, ##__VA_ARGS__);                           \
        raise_termination(raise_exception, err_msg);                                               \
    }
#define ERR_EXIT(message, ...) ERR_EXIT_EXCEPT_CHOICE(true, message, ##__VA_ARGS__)

#else

#define ERR_EXIT_EXCEPT_CHOICE(raise_exception, message, ...)                                      \
    if (!continue_on_error) {                                                                      \
        char err_msg[CALF_LOG_MAX_MSG_LEN];                                                        \
        snprintf(err_msg, sizeof(err_msg), message, ##__VA_ARGS__);                                \
        raise_termination(raise_exception, err_msg);                                               \
    }
#define ERR_EXIT(message, ...) ERR_EXIT_EXCEPT_CHOICE(true, message, ##__VA_ARGS__)

#endif // CALF_LOG

#endif // CALF_BASELOGGER_H
