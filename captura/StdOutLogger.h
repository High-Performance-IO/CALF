#ifndef CAPTURA_STDOUTLOGGER_H
#define CAPTURA_STDOUTLOGGER_H

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <unistd.h>

#include "BaseLogger.h"

#define CAPTURA_COLOR_RESET "\033[0m"
#define CAPTURA_COLOR_RED "\033[31m"
#define CAPTURA_COLOR_GREEN "\033[32m"
#define CAPTURA_COLOR_YELLOW "\033[33m"
#define CAPTURA_COLOR_BLUE "\033[34m"
#define CAPTURA_COLOR_MAGENTA "\033[35m"
#define CAPTURA_COLOR_CYAN "\033[36m"
#define CAPTURA_COLOR_WHITE "\033[37m"
#define CAPTURA_COLOR_BOLD "\033[1m"

struct StdoutLoggerOptions {
    const char *color         = CAPTURA_COLOR_RESET;
    const char *workflowName  = "CAPTURA";
    const char *componentName = "CAPTURA";
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
        char expanded[CAPTURA_LOG_MAX_MSG_LEN];
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
        char expanded[CAPTURA_LOG_MAX_MSG_LEN];
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

        const bool color = options.useColor && options.color != nullptr && options.color[0] != '\0';

        if (color) {
            std::cout << options.color;
        }

        std::cout << "[" << options.workflowName << "]";

        if (color) {
            std::cout << CAPTURA_COLOR_RESET;
        }

        std::cout << " [" << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S") << "] "
                  << hostname << "@" << std::left << std::setw(20)
                  << std::string(options.componentName).substr(0, 20) << " | ";

        if (color) {
            std::cout << options.color;
        }

        // Invoker + message, matching the style of the original server_println.
        std::cout << std::left << std::setw(30) << std::string(invoker).substr(0, 30) << " | "
                  << message;

        if (color) {
            std::cout << CAPTURA_COLOR_RESET;
        }

        std::cout << '\n' << std::flush;
    }
};

using CapturaCliLogger = TemplateLogger<StdoutLogger>;

#define CAPTURA_PRINT(message, ...)                                                                \
    do {                                                                                           \
        char _captura_buf[CAPTURA_LOG_MAX_MSG_LEN];                                                \
        ::snprintf(_captura_buf, sizeof(_captura_buf), message, ##__VA_ARGS__);                    \
        StdoutLogger::printLine(__func__, _captura_buf);                                           \
    } while (0)

#define CAPTURA_PRINT_COLOR(color, message, ...)                                                   \
    do {                                                                                           \
        const char *_captura_saved  = StdoutLogger::options.color;                                 \
        StdoutLogger::options.color = (color);                                                     \
        char _captura_buf[CAPTURA_LOG_MAX_MSG_LEN];                                                \
        ::snprintf(_captura_buf, sizeof(_captura_buf), message, ##__VA_ARGS__);                    \
        StdoutLogger::printLine(__func__, _captura_buf);                                           \
        StdoutLogger::options.color = _captura_saved;                                              \
    } while (0)

#endif // CAPTURA_STDOUTLOGGER_H