#ifndef CALF_STLLOGGER_H
#define CALF_STLLOGGER_H

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits.h>
#include <memory>
#include <string>
#include <sys/syscall.h>
#include <unistd.h>

#include "BaseLogger.h"
#include "JsonBaseLogger.h"

#if defined(__APPLE__)
#include <pthread.h>
#elif defined(__linux__)
#include <sys/syscall.h>
#include <unistd.h>
#endif

inline long calf_current_tid() {
#if defined(__APPLE__)
    uint64_t tid = 0;
    pthread_threadid_np(nullptr, &tid);
    return static_cast<long>(tid);
#elif defined(__linux__)
    return static_cast<long>(::syscall(SYS_gettid));
#else
    return static_cast<long>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
#endif
}

struct StlLogger : JsonLogBase<StlLogger> {

    inline static thread_local std::unique_ptr<std::ofstream> logfile   = nullptr;
    inline static thread_local std::unique_ptr<std::string> logFileName = nullptr;

    explicit StlLogger() { ensureFileOpen(); }

    static std::string getLogFileName() { return logFileName ? *logFileName : std::string{}; }

    static void rawWriteBytes(const char *buf, const int len) {
        ensureFileOpen();
        logfile->write(buf, len);
        logfile->flush();
    }

    static void rawWriteStr(const char *buf) {
        rawWriteBytes(buf, static_cast<int>(::strlen(buf)));
    }

  private:
    static void ensureFileOpen() {
        if (logfile != nullptr && logfile->is_open()) {
            return;
        }

        std::string logDir;
        std::string prefix;

        if (const char *env = std::getenv("CALF_LOG_DIR"); env != nullptr) {
            logDir = env;
        } else {
            logDir = CALF_DEFAULT_LOG_FOLDER;
        }

        if (const char *env = std::getenv("CALF_LOG_PREFIX"); env != nullptr) {
            prefix = env;
        } else {
            prefix = CALF_STL_DEFAULT_LOG_FILE_PREFIX;
        }

        char hostname[CALF_HOSTNAME_BUFFER_SIZE]{};
        ::gethostname(hostname, sizeof(hostname));
        hostname[sizeof(hostname) - 1] = '\0';

        const std::filesystem::path outputFolder{logDir + "/" + CALF_COMPONENT_NAME + "/" +
                                                 hostname};
        std::filesystem::create_directories(outputFolder);

        const std::filesystem::path path =
            outputFolder / (prefix + std::to_string(calf_current_tid()) + ".log");

        logfile     = std::make_unique<std::ofstream>(path, std::ofstream::app);
        logFileName = std::make_unique<std::string>(path.string());
    }
};

using Logger = TemplateLogger<StlLogger>;

#ifdef CALF_LOG

#define LOG(message, ...) log.log(message, ##__VA_ARGS__)

#define START_LOG(tid, message, ...)                                                               \
    Logger::reset_log_level();                                                                     \
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

#else

#define LOG(message, ...)
#define START_LOG(tid, message, ...)
#define DBG(tid, lambda)
#define ENABLE_LOGGER()
#define DISABLE_LOGGER()

#endif // CALF_LOG

#endif // CALF_STLLOGGER_H
