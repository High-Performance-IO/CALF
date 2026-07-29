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
    perfetto::protos::Trace trace;
    ASSERT_TRUE(trace.ParseFromString(data));

    ASSERT_EQ(trace.packet_size(), 7);
    ASSERT_TRUE(trace.packet(0).has_track_descriptor());
    EXPECT_EQ(trace.packet(0).track_descriptor().thread().pid(), ::getpid());

    const auto &outerBegin = trace.packet(1).track_event();
    EXPECT_EQ(trace.packet(1).timestamp_clock_id(), 3u);
    EXPECT_EQ(outerBegin.type(), perfetto::protos::TrackEvent::TYPE_SLICE_BEGIN);
    EXPECT_EQ(outerBegin.name(), "outer");
    EXPECT_EQ(outerBegin.source_location().file_name(), "trace.cpp");
    EXPECT_EQ(outerBegin.source_location().line_number(), 10u);
    ASSERT_EQ(outerBegin.debug_annotations_size(), 1);
    EXPECT_EQ(outerBegin.debug_annotations(0).name(), "args");
    EXPECT_EQ(outerBegin.debug_annotations(0).string_value(), "request=7");

    EXPECT_EQ(trace.packet(2).track_event().type(),
              perfetto::protos::TrackEvent::TYPE_INSTANT);
    EXPECT_EQ(trace.packet(2).track_event().debug_annotations(0).string_value(), "started");
    EXPECT_EQ(trace.packet(3).track_event().type(),
              perfetto::protos::TrackEvent::TYPE_SLICE_BEGIN);
    EXPECT_EQ(trace.packet(4).track_event().debug_annotations(0).string_value(), "value=ok");
    EXPECT_EQ(trace.packet(5).track_event().type(),
              perfetto::protos::TrackEvent::TYPE_SLICE_END);
    EXPECT_EQ(trace.packet(6).track_event().type(),
              perfetto::protos::TrackEvent::TYPE_SLICE_END);
    EXPECT_EQ(outerBegin.track_uuid(), trace.packet(0).track_descriptor().uuid());
    EXPECT_FALSE(data.empty());
    EXPECT_EQ(std::filesystem::path(path).extension(), ".perfetto-trace");

    std::filesystem::remove_all(root);
}
