#include "calf/SyscallLogger.h"
#include "calf/protobuf/calf_trace.pb.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/syscall.h>
#include <unistd.h>

TEST(SyscallProtobufLoggerTest, PreservesInterfaceAndWritesProtobuf) {
    const std::filesystem::path root = std::filesystem::temp_directory_path() /
                                       ("calf-syscall-protobuf-tests-" +
                                        std::to_string(::getpid()));
    std::filesystem::remove_all(root);
    ASSERT_EQ(::setenv("CALF_LOG_DIR", root.string().c_str(), 1), 0);
    ASSERT_EQ(::setenv("CALF_LOG_PREFIX", "raw_proto_", 1), 0);
    EXPECT_FALSE(enable_logger);
    ENABLE_LOGGER();
    SyscallLogger::resetProtobufState();

    std::string path;
    {
        START_LOG(::syscall(SYS_gettid), "scope=%d", 2);
        path = log.getLogFileName();
        LOG("event=%s", "written");
    }

    std::ifstream input(path, std::ios::binary);
    const std::string data((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    calf::proto::TraceFile trace;
    ASSERT_TRUE(trace.ParseFromString(data));
    ASSERT_EQ(trace.records_size(), 3);
    EXPECT_EQ(trace.records(0).kind(), calf::proto::TraceRecord::SCOPE_ENTER);
    EXPECT_EQ(trace.records(0).args(), "scope=2");
    EXPECT_EQ(trace.records(1).kind(), calf::proto::TraceRecord::EVENT);
    EXPECT_EQ(trace.records(1).args(), "event=written");
    EXPECT_EQ(trace.records(2).kind(), calf::proto::TraceRecord::SCOPE_EXIT);
    EXPECT_EQ(std::filesystem::path(path).extension(), ".pb");

    std::filesystem::remove_all(root);
}
