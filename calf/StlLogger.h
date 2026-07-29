#ifndef CALF_STLLOGGER_H
#define CALF_STLLOGGER_H

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <climits>
#include <memory>
#include <mutex>
#include <string>
#include <unistd.h>

#include "format/BaseLogger.h"
#include "format/LogFormat.h"
#include "utils//ThreadId.h"

#ifdef CALF_LOG_FORMAT_PROTOBUF
#define CALF_STL_LOGGER_TYPE PerfettoStlLogger
#else
#define CALF_STL_LOGGER_TYPE JsonStlLogger
#endif

struct CALF_STL_LOGGER_TYPE : CalfLogBase<CALF_STL_LOGGER_TYPE> {

    inline static thread_local std::unique_ptr<std::fstream> logfile     = nullptr;
    inline static thread_local std::unique_ptr<std::string> logFileName = nullptr;
#ifdef CALF_LOG_FORMAT_PROTOBUF
    inline static std::unique_ptr<std::fstream> perfettoLogfile     = nullptr;
    inline static std::unique_ptr<std::string> perfettoLogFileName = nullptr;
    inline static std::recursive_mutex perfettoLogfileMutex;
    inline static pid_t logfilePid = 0;
#endif

    explicit CALF_STL_LOGGER_TYPE() { ensureFileOpen(); }

    static unsigned long currentTimestamp() {
#ifdef CALF_LOG_FORMAT_PROTOBUF
        timespec now{};
        ::clock_gettime(CLOCK_MONOTONIC, &now);
        return static_cast<unsigned long>(now.tv_sec) * 1000000000UL +
               static_cast<unsigned long>(now.tv_nsec);
#else
        return static_cast<unsigned long>(current_time_in_millis());
#endif
    }

#ifdef CALF_LOG_FORMAT_PROTOBUF
    static pid_t processId() { return ::getpid(); }

    static long threadId() { return calf_current_tid(); }
#endif

    static std::string getLogFileName() {
#ifdef CALF_LOG_FORMAT_PROTOBUF
        const std::lock_guard<std::recursive_mutex> lock(perfettoLogfileMutex);
        return perfettoLogFileName ? *perfettoLogFileName : std::string{};
#else
        return logFileName ? *logFileName : std::string{};
#endif
    }

    static void rawWriteBytes(const char *buf, const int len) {
#ifdef CALF_LOG_FORMAT_PROTOBUF
        const std::lock_guard<std::recursive_mutex> lock(perfettoLogfileMutex);
#endif
        ensureFileOpen();
#ifdef CALF_LOG_FORMAT_PROTOBUF
        perfettoLogfile->write(buf, len);
#else
        logfile->write(buf, len);
#endif
        if (!CALF_LOG_FILE_BINARY) {
            logfile->flush();
        }
    }

    static void rawWriteStr(const char *buf) {
        rawWriteBytes(buf, static_cast<int>(::strlen(buf)));
    }

    static void flush() {
#ifdef CALF_LOG_FORMAT_PROTOBUF
        const std::lock_guard<std::recursive_mutex> lock(perfettoLogfileMutex);
        if (perfettoLogfile) {
            perfettoLogfile->flush();
        }
#else
        if (logfile) {
            logfile->flush();
        }
#endif
    }

    static void reopenRootArray() {
        ensureFileOpen();
        logfile->seekp(-2, std::ios::end);
        logfile->write(",\n", 2);
        logfile->flush();
    }

  private:
    static void ensureFileOpen() {
#ifdef CALF_LOG_FORMAT_PROTOBUF
        const std::lock_guard<std::recursive_mutex> lock(perfettoLogfileMutex);
        const auto currentPid = ::getpid();
        if (perfettoLogfile != nullptr && perfettoLogfile->is_open() &&
            logfilePid != currentPid) {
            perfettoLogfile->close();
            perfettoLogfile.reset();
            perfettoLogFileName.reset();
        }
        if (perfettoLogfile != nullptr && perfettoLogfile->is_open()) {
            return;
        }
#else
        if (logfile != nullptr && logfile->is_open()) {
            return;
        }
#endif

        std::string logDir;
#ifndef CALF_LOG_FORMAT_PROTOBUF
        std::string prefix;
#endif

        if (const char *env = std::getenv("CALF_LOG_DIR"); env != nullptr) {
            logDir = env;
        } else {
            logDir = CALF_DEFAULT_LOG_FOLDER;
        }

#ifndef CALF_LOG_FORMAT_PROTOBUF
        if (const char *env = std::getenv("CALF_LOG_PREFIX"); env != nullptr) {
            prefix = env;
        } else {
            prefix = CALF_STL_DEFAULT_LOG_FILE_PREFIX;
        }
#endif

        char hostname[CALF_HOSTNAME_BUFFER_SIZE]{};
        ::gethostname(hostname, sizeof(hostname));
        hostname[sizeof(hostname) - 1] = '\0';

#ifdef CALF_LOG_FORMAT_PROTOBUF
        const std::filesystem::path outputFolder{logDir + "/" + hostname};
#else
        const std::filesystem::path outputFolder{logDir + "/" + CALF_COMPONENT_NAME + "/" +
                                                 hostname};
#endif
        std::filesystem::create_directories(outputFolder);

#ifdef CALF_LOG_FORMAT_PROTOBUF
        const std::filesystem::path path =
            outputFolder / (std::string(CALF_COMPONENT_NAME) + "_" + std::to_string(::getpid()) +
                            CALF_LOG_FILE_EXTENSION);
#else
        const std::filesystem::path path =
            outputFolder /
            (prefix + std::to_string(calf_current_tid()) + CALF_LOG_FILE_EXTENSION);
#endif

#ifdef CALF_LOG_FORMAT_PROTOBUF
        std::ios::openmode mode = std::ios::out | std::ios::trunc | std::ios::binary;
#else
        std::ios::openmode mode = std::ios::in | std::ios::out | std::ios::trunc;
        if (CALF_LOG_FILE_BINARY) {
            mode |= std::ios::binary;
        }
#endif
#ifdef CALF_LOG_FORMAT_PROTOBUF
        perfettoLogfile = std::make_unique<std::fstream>(path, mode);
        perfettoLogFileName = std::make_unique<std::string>(path.string());
        logfilePid = currentPid;
#else
        logfile = std::make_unique<std::fstream>(path, mode);
        logFileName = std::make_unique<std::string>(path.string());
#endif
    }
};

using StlLogger = CALF_STL_LOGGER_TYPE;
#undef CALF_STL_LOGGER_TYPE

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
