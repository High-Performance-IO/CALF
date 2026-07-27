#include "calf/format/JsonBaseLogger.h"
#include "calf/StdOutLogger.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <sstream>
#include <stdexcept>
#include <string>

struct CaptureJsonLogger : JsonLogBase<CaptureJsonLogger> {
    inline static thread_local std::string output;

    static void rawWriteBytes(const char *data, int length) { output.append(data, length); }
    static void rawWriteStr(const char *data) { output += data; }
    static void reopenRootArray() { output.replace(output.size() - 2, 2, ",\n"); }

    static void reset() {
        output.clear();
        nestingDepth  = 0;
        rootArrayOpen = false;
        pendingLen    = 0;
    }

    static std::string finish() {
        flushPending(false);
        if (rootArrayOpen && nestingDepth > 0) {
            output += "]\n";
        }
        return output;
    }
};

struct CountingAdapter {
    inline static int openings  = 0;
    inline static int events    = 0;
    inline static int epilogues = 0;

    static void reset() { openings = events = epilogues = 0; }
    static void writeOpening(unsigned long, const char *, const char *, int, const char *,
                             va_list) {
        ++openings;
    }
    static void printFormatted(unsigned long, const char *, const char *, int, const char *,
                               const char *, va_list) {
        ++events;
    }
    static void writeEpilogue(unsigned long) { ++epilogues; }
    static std::string getLogFileName() { return "capture.log"; }
};

class CoutCapture {
    std::ostringstream stream_;
    std::streambuf *previous_;

  public:
    CoutCapture() : previous_(std::cout.rdbuf(stream_.rdbuf())) {}
    ~CoutCapture() { std::cout.rdbuf(previous_); }
    std::string str() const { return stream_.str(); }
};

TEST(JsonLoggerTest, WritesScopeAndEvent) {
    CaptureJsonLogger::reset();
    enable_logger = true;
    {
        TemplateLogger<CaptureJsonLogger> logger("worker", "source.cpp", 42, 7, "request=%d", 12);
        logger.log("value=%s", "done");
    }

    const std::string output = CaptureJsonLogger::finish();
    EXPECT_NE(output.find("\"invoker\": \"worker\""), std::string::npos);
    EXPECT_NE(output.find("\"file\": \"source.cpp\""), std::string::npos);
    EXPECT_NE(output.find("\"line\": 42"), std::string::npos);
    EXPECT_NE(output.find("\"args\": \"request=12\""), std::string::npos);
    EXPECT_NE(output.find("\"args\": \"value=done\""), std::string::npos);
    EXPECT_NE(output.find("\"ts_enter\":"), std::string::npos);
    EXPECT_NE(output.find("\"ts_exit\":"), std::string::npos);
    ASSERT_FALSE(output.empty());
    EXPECT_EQ(output.front(), '[');
    const std::size_t lastCharacter = output.find_last_not_of("\n ");
    ASSERT_NE(lastCharacter, std::string::npos);
    EXPECT_EQ(output[lastCharacter], ']');

    const nlohmann::json document = nlohmann::json::parse(output);
    ASSERT_TRUE(document.is_array());
    ASSERT_EQ(document.size(), 1u);
    EXPECT_EQ(document[0]["invoker"], "worker");
    ASSERT_EQ(document[0]["events"].size(), 1u);
    EXPECT_EQ(document[0]["events"][0]["args"], "value=done");
}

TEST(JsonLoggerTest, EscapesJsonStrings) {
    CaptureJsonLogger::reset();
    enable_logger = true;
    {
        TemplateLogger<CaptureJsonLogger> logger("say\"hi", "a\\b.cpp", 3, 1, "line\n\t%c", 1);
        logger.log("quote=\" slash=\\ return=\r");
    }

    const std::string output = CaptureJsonLogger::finish();
    EXPECT_NE(output.find("say\\\"hi"), std::string::npos);
    EXPECT_NE(output.find("a\\\\b.cpp"), std::string::npos);
    EXPECT_NE(output.find("line\\n\\t\\u0001"), std::string::npos);
    EXPECT_NE(output.find("quote=\\\" slash=\\\\ return=\\r"), std::string::npos);
    const nlohmann::json document = nlohmann::json::parse(output);
    EXPECT_TRUE(document.is_array());
}

TEST(JsonLoggerTest, PreservesLongEscapedMessages) {
    CaptureJsonLogger::reset();
    enable_logger = true;
    const std::string message(3000, '\x01');
    {
        TemplateLogger<CaptureJsonLogger> logger("worker", "source.cpp", 42, 7, "%s",
                                                 message.c_str());
        logger.log("%s", message.c_str());
    }

    const nlohmann::json document = nlohmann::json::parse(CaptureJsonLogger::finish());
    ASSERT_EQ(document.size(), 1u);
    EXPECT_EQ(document[0]["args"], message);
    ASSERT_EQ(document[0]["events"].size(), 1u);
    EXPECT_EQ(document[0]["events"][0]["args"], message);
}

TEST(JsonLoggerTest, WritesNestedScopes) {
    CaptureJsonLogger::reset();
    enable_logger = true;
    {
        TemplateLogger<CaptureJsonLogger> outer("outer", "nested.cpp", 1, 1, "begin");
        {
            TemplateLogger<CaptureJsonLogger> inner("inner", "nested.cpp", 2, 1, "child");
            inner.log("inside");
        }
        outer.log("after child");
    }

    const std::string output = CaptureJsonLogger::finish();
    EXPECT_NE(output.find("\"invoker\": \"outer\""), std::string::npos);
    EXPECT_NE(output.find("\"invoker\": \"inner\""), std::string::npos);
    EXPECT_NE(output.find("\"args\": \"inside\""), std::string::npos);
    EXPECT_NE(output.find("\"args\": \"after child\""), std::string::npos);
    const nlohmann::json document = nlohmann::json::parse(output);
    EXPECT_TRUE(document.is_array());
}

TEST(TemplateLoggerTest, HonorsEnableAndSuspenderState) {
    CountingAdapter::reset();
    enable_logger = false;
    {
        TemplateLogger<CountingAdapter> logger("off", "test.cpp", 1, 1, "ignored");
        logger.log("ignored");
    }
    EXPECT_EQ(CountingAdapter::openings, 0);
    EXPECT_EQ(CountingAdapter::events, 0);
    EXPECT_EQ(CountingAdapter::epilogues, 0);

    enable_logger = true;
    {
        SyscallLoggingSuspender suspender;
        EXPECT_FALSE(enable_logger);
        {
            SyscallLoggingSuspender nested;
            EXPECT_FALSE(enable_logger);
        }
        EXPECT_FALSE(enable_logger);
    }
    EXPECT_TRUE(enable_logger);

    {
        TemplateLogger<CountingAdapter> logger("on", "test.cpp", 2, 1, "open");
        logger.log("event");
        EXPECT_EQ(logger.getLogFileName(), "capture.log");
    }
    EXPECT_EQ(CountingAdapter::openings, 1);
    EXPECT_EQ(CountingAdapter::events, 1);
    EXPECT_EQ(CountingAdapter::epilogues, 1);
}

TEST(BaseLoggerTest, RaisesTerminationException) {
    EXPECT_THROW(
        {
            try {
                raise_termination(true, "fatal message");
            } catch (const std::runtime_error &error) {
                EXPECT_STREQ(error.what(), "fatal message");
                throw;
            }
        },
        std::runtime_error);
}

TEST(StdoutLoggerTest, WritesPlainMessageWithoutHeader) {
    StdoutLoggerOptions options;
    options.printHeader = false;
    options.useColor    = false;
    StdoutLogger::setOptions(options);

    CoutCapture capture;
    StdoutLogger::printLine("ignored", "plain message");
    EXPECT_EQ(capture.str(), "plain message\n");
}

TEST(StdoutLoggerTest, WritesHeaderAndColor) {
    StdoutLoggerOptions options;
    options.color        = CALF_CLI_LEVEL_WARNING;
    options.workflowName = "workflow";
    options.printHeader  = true;
    options.useColor     = true;
    StdoutLogger::setOptions(options);

    CoutCapture capture;
    StdoutLogger::printLine("operation", "warning 9");
    const std::string output = capture.str();
    EXPECT_NE(output.find("[calf "), std::string::npos);
    EXPECT_NE(output.find("(workflow) | operation"), std::string::npos);
    EXPECT_NE(output.find(CALF_CLI_LEVEL_WARNING), std::string::npos);
    EXPECT_NE(output.find("warning 9"), std::string::npos);
    EXPECT_NE(output.find(CAPIO_LOG_SERVER_CLI_LEVEL_RESET), std::string::npos);
}

TEST(StdoutLoggerTest, ExpandsTemplateMessages) {
    StdoutLoggerOptions options;
    options.printHeader = false;
    options.useColor    = false;
    StdoutLogger::setOptions(options);
    enable_logger = true;

    CoutCapture capture;
    {
        CalfCliLogger logger("task", "test.cpp", 5, 1, "opening %d", 4);
        logger.log("event %s", "ok");
    }
    EXPECT_EQ(capture.str(), "opening 4\nevent ok\n");
}
