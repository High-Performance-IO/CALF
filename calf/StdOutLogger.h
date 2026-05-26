#ifndef CALF_STDOUTLOGGER_H
#define CALF_STDOUTLOGGER_H

#include <chrono>
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <unistd.h>

#include "BaseLogger.h"

// Cli pre messages
constexpr char CAPIO_LOG_SERVER_CLI_LEVEL_RESET[] = "\033[0m";
constexpr char CALF_CLI_LEVEL_STATUS[]            = "\033[1;34m";
constexpr char CALF_CLI_LEVEL_INFO[]              = "\033[1;32m";
constexpr char CALF_CLI_LEVEL_WARNING[]           = "\033[1;33m";
constexpr char CALF_CLI_LEVEL_ERROR[]             = "\033[1;31m";

struct StdoutLoggerOptions {
    std::string color         = CAPIO_LOG_SERVER_CLI_LEVEL_RESET;
    std::string workflowName  = "NOT SET";
    std::string componentName = "NOT SET";
    bool printHeader          = true;
    bool useColor             = true;
};

struct StdoutLogger {

    inline static StdoutLoggerOptions options{};
    static void setOptions(const StdoutLoggerOptions &o) { options = o; }
    static void setColor(const char *color) { options.color = color; }
    static void setPrintHeader(bool v) { options.printHeader = v; }

    explicit StdoutLogger() {
        if (!::isatty(STDOUT_FILENO)) {
            options.useColor = false;
        }
    }

    std::string getLogFileName() const { return "stdout"; }

    static void writeOpening(unsigned long int /*timestamp*/, const char *invoker,
                             const char * /*file*/, int /*line*/, const char *message_format,
                             va_list args) {
        char expanded[CALF_LOG_MAX_MSG_LEN];
        va_list copy;
        va_copy(copy, args);
        ::vsnprintf(expanded, sizeof(expanded), message_format, copy);
        va_end(copy);

        printLine(invoker, expanded);
    }

    static void printFormatted(unsigned long int /*timestamp*/, const char *invoker,
                               const char * /*file*/, int /*line*/,
                               const char * /*output_template*/, const char *message_format,
                               va_list args) {
        char expanded[CALF_LOG_MAX_MSG_LEN];
        va_list copy;
        va_copy(copy, args);
        ::vsnprintf(expanded, sizeof(expanded), message_format, copy);
        va_end(copy);

        printLine(invoker, expanded);
    }

    static void writeEpilogue(unsigned long int /*timestamp*/) {}

    static void write(const char *buf, const size_t len) {
        std::cout.write(buf, static_cast<std::streamsize>(len));
        std::cout << '\n' << std::flush;
    }

    // Formats and prints one log line to stdout.
    static void printLine(const char *invoker, const char *message) {
        if (!options.printHeader) {
            std::cout << message << '\n' << std::flush;
            return;
        }

        // Hostname — initialised once.
        static char hostname[HOST_NAME_MAX]{'\0'};
        [[maybe_unused]] static bool hostname_init = []() {
            if (::gethostname(hostname, HOST_NAME_MAX) != 0) {
                ::snprintf(hostname, HOST_NAME_MAX, "unknown");
            }
            return true;
        }();

        // Timestamp.
        const auto now = std::chrono::system_clock::now();
        const auto t   = std::chrono::system_clock::to_time_t(now);

        const bool color = options.useColor && !options.color.empty() && options.color[0] != '\0';

        std::cout << "[" << options.componentName << " " << hostname << "] "
                  << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S") << "("
                  << options.workflowName << ")" << std::left << " | ";

        // Invoker + message, matching the style of the original server_println.
        std::cout << std::left << std::setw(30) << std::string(invoker).substr(0, 30) << " | ";

        if (color) {
            std::cout << options.color;
        }

        std::cout << message;

        if (color) {
            std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_RESET;
        }

        std::cout << '\n' << std::flush;
    }
};

using CalfCliLogger = TemplateLogger<StdoutLogger>;

#define CALF_PRINT(message, ...)                                                                   \
    do {                                                                                           \
        char _calf_buf[CALF_LOG_MAX_MSG_LEN];                                                      \
        ::snprintf(_calf_buf, sizeof(_calf_buf), message, ##__VA_ARGS__);                          \
        StdoutLogger::printLine(__func__, _calf_buf);                                              \
    } while (0)

#define CALF_PRINT_COLOR(status, message, ...)                                                     \
    do {                                                                                           \
        auto _calf_saved            = StdoutLogger::options.color;                                 \
        StdoutLogger::options.color = (status);                                                    \
        char _calf_buf[CALF_LOG_MAX_MSG_LEN];                                                      \
        ::snprintf(_calf_buf, sizeof(_calf_buf), message, ##__VA_ARGS__);                          \
        StdoutLogger::printLine(__func__, _calf_buf);                                              \
        StdoutLogger::options.color = _calf_saved;                                                 \
    } while (0)

#define UPDATE_CALF_CLI_CONFIG(component_name, workflow_name)                                      \
    StdoutLogger::options.componentName = "SERVER";                                                \
    StdoutLogger::options.workflowName  = "";

#ifdef CALF_LOG
#define DBG_CALF_PRINT_COLOR(status, message, ...) CALF_PRINT_COLOR(status, message, ##__VA_ARGS__)
#else
#define DBG_CALF_PRINT_COLOR(status, message, ...)
#endif

#endif // CALF_STDOUTLOGGER_H