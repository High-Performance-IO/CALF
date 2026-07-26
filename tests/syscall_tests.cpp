#include "calf/SyscallLogger.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/syscall.h>
#include <unistd.h>

static std::string readFile(const std::string &path) {
    std::ifstream input(path);
    std::ostringstream content;
    content << input.rdbuf();
    return content.str();
}

TEST(SyscallLoggerTest, WritesConfiguredFile) {
    const std::filesystem::path root = std::filesystem::temp_directory_path() /
                                       ("calf-syscall-tests-" + std::to_string(::getpid()));
    std::filesystem::remove_all(root);
    ASSERT_EQ(::setenv("CALF_LOG_DIR", root.string().c_str(), 1), 0);
    ASSERT_EQ(::setenv("CALF_LOG_PREFIX", "raw_", 1), 0);
    EXPECT_FALSE(enable_logger);
    ENABLE_LOGGER();

    std::string path;
    {
        Logger logger("syscall_test", "syscall_tests.cpp", 22, ::syscall(SYS_gettid),
                      "scope=%d", 2);
        path = logger.getLogFileName();
        logger.log("event=%s", "written");
    }
    { Logger logger("flush", "syscall_tests.cpp", 23, ::syscall(SYS_gettid), "next"); }

    ASSERT_FALSE(path.empty());
    ASSERT_TRUE(std::filesystem::exists(path));
    EXPECT_EQ(std::filesystem::path(path).parent_path().parent_path().filename(), "calf");
    EXPECT_EQ(std::filesystem::path(path).filename().string().find("raw_"), 0u);

    const std::string output = readFile(path);
    EXPECT_NE(output.find("\"invoker\": \"syscall_test\""), std::string::npos);
    EXPECT_NE(output.find("\"args\": \"scope=2\""), std::string::npos);
    EXPECT_NE(output.find("\"args\": \"event=written\""), std::string::npos);
    EXPECT_NE(output.find("\"ts_exit\":"), std::string::npos);

    const nlohmann::json document = nlohmann::json::parse(output);
    ASSERT_TRUE(document.is_array());
    ASSERT_EQ(document.size(), 2u);
    EXPECT_EQ(document[0]["invoker"], "syscall_test");
    EXPECT_EQ(document[1]["invoker"], "flush");

    std::filesystem::remove_all(root);
}
