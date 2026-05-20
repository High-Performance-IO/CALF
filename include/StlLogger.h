#ifndef CAPTURA_STLLOGGER_H
#define CAPTURA_STLLOGGER_H

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/syscall.h>
#include <unistd.h>

#include "BaseLogger.h"
#include "JsonBaseLogger.h"

/**
 * Adapter that writes structured JSON log output using std::ofstream.
 * Suitable for server processes and any context where STL I/O is safe.
 * Header-only: all methods are inline.
 */
struct StlLogger : JsonLogBase<StlLogger> {

    inline static thread_local std::ofstream *logfile   = nullptr;
    inline static thread_local std::string *logFileName = nullptr;

    explicit StlLogger() { ensureFileOpen(); }

    std::string getLogFileName() const { return logFileName ? *logFileName : std::string{}; }

    static void rawWriteBytes(const char *buf, int len) {
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

        if (const char *env = std::getenv("CAPTURA_LOG_DIR"); env != nullptr) {
            logDir = env;
        } else {
            logDir = CAPIO_DEFAULT_LOG_FOLDER;
        }

        if (const char *env = std::getenv("CAPTURA_LOG_PREFIX"); env != nullptr) {
            prefix = env;
        } else {
            prefix = CAPIO_SERVER_DEFAULT_LOG_FILE_PREFIX;
        }

        char hostname[HOST_NAME_MAX];
        ::gethostname(hostname, HOST_NAME_MAX);

        const std::filesystem::path outputFolder{logDir + "/stl/" + hostname};
        std::filesystem::create_directories(outputFolder);

        const std::filesystem::path path =
            outputFolder / (prefix + std::to_string(::syscall(SYS_gettid)) + ".log");

        logfile     = new std::ofstream(path, std::ofstream::app);
        logFileName = new std::string(path.string());
    }
};

using Logger = TemplateLogger<StlLogger>;

// -------------------------------------------------------------------------
//  Macros — STL / server build
// -------------------------------------------------------------------------

#ifdef CAPTURA_LOG

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

#endif // CAPTURA_LOG

#endif // CAPTURA_STLLOGGER_H