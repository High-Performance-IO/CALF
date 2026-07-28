#include "calf/StlLogger.h"
#include "calf/protobuf/calf_trace.pb.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>

TEST(ProtobufLoggerTest, WritesStreamableNestedTrace) {
    const std::filesystem::path root = std::filesystem::temp_directory_path() /
                                       ("calf-protobuf-tests-" + std::to_string(::getpid()));
    std::filesystem::remove_all(root);
    ASSERT_EQ(::setenv("CALF_LOG_DIR", root.string().c_str(), 1), 0);
    ASSERT_EQ(::setenv("CALF_LOG_PREFIX", "trace_", 1), 0);
    enable_logger = true;
    StlLogger::resetProtobufState();

    std::string path;
    {
        Logger outer("outer", "trace.cpp", 10, calf_current_tid(), "request=%d", 7);
        path = outer.getLogFileName();
        outer.log("started");
        {
            Logger inner("inner", "trace.cpp", 12, calf_current_tid(), "child");
            inner.log("value=%s", "ok");
        }
    }

    std::ifstream input(path, std::ios::binary);
    const std::string data((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    calf::proto::TraceFile trace;
    ASSERT_TRUE(trace.ParseFromString(data));

    ASSERT_EQ(trace.records_size(), 6);
    EXPECT_EQ(trace.records(0).kind(), calf::proto::TraceRecord::SCOPE_ENTER);
    EXPECT_EQ(trace.records(0).scope_id(), 1u);
    EXPECT_EQ(trace.records(0).parent_scope_id(), 0u);
    EXPECT_EQ(trace.records(0).invoker(), "outer");
    EXPECT_EQ(trace.records(0).file(), "trace.cpp");
    EXPECT_EQ(trace.records(0).line(), 10u);
    EXPECT_EQ(trace.records(0).args(), "request=7");
    EXPECT_EQ(trace.records(1).kind(), calf::proto::TraceRecord::EVENT);
    EXPECT_EQ(trace.records(1).scope_id(), 1u);
    EXPECT_EQ(trace.records(1).args(), "started");
    EXPECT_EQ(trace.records(2).scope_id(), 2u);
    EXPECT_EQ(trace.records(2).parent_scope_id(), 1u);
    EXPECT_EQ(trace.records(3).args(), "value=ok");
    EXPECT_EQ(trace.records(4).kind(), calf::proto::TraceRecord::SCOPE_EXIT);
    EXPECT_EQ(trace.records(4).scope_id(), 2u);
    EXPECT_EQ(trace.records(5).kind(), calf::proto::TraceRecord::SCOPE_EXIT);
    EXPECT_EQ(trace.records(5).scope_id(), 1u);
    EXPECT_FALSE(data.empty());
    EXPECT_EQ(std::filesystem::path(path).extension(), ".pb");

    std::filesystem::remove_all(root);
}
