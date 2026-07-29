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
    perfetto::protos::Trace trace;
    ASSERT_TRUE(trace.ParseFromString(data));
    ASSERT_EQ(trace.packet_size(), 4);
    EXPECT_TRUE(trace.packet(0).has_track_descriptor());
    EXPECT_EQ(trace.packet(1).track_event().type(),
              perfetto::protos::TrackEvent::TYPE_SLICE_BEGIN);
    EXPECT_EQ(trace.packet(1).track_event().debug_annotations(0).string_value(), "scope=2");
    EXPECT_EQ(trace.packet(2).track_event().type(),
              perfetto::protos::TrackEvent::TYPE_INSTANT);
    EXPECT_EQ(trace.packet(2).track_event().debug_annotations(0).string_value(), "event=written");
    EXPECT_EQ(trace.packet(3).track_event().type(),
              perfetto::protos::TrackEvent::TYPE_SLICE_END);
    EXPECT_EQ(std::filesystem::path(path).extension(), ".perfetto-trace");

    std::filesystem::remove_all(root);
}
