#include "calf/SyscallLogger.h"
#include "calf/protobuf/calf_trace.pb.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <array>
#include <atomic>
#include <cstdarg>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <sys/syscall.h>
#include <unistd.h>

namespace {
std::atomic<int> handledGetpid{0};
std::atomic<int> handledGettid{0};
std::atomic<int> handledClockGettime{0};

long recordingSyscall(long number, ...) {
    va_list args;
    va_start(args, number);
    long result = -1;
    switch (number) {
    case SYS_getpid:
        ++handledGetpid;
        result = ::syscall(number);
        break;
    case SYS_gettid:
        ++handledGettid;
        result = ::syscall(number);
        break;
    case SYS_clock_gettime: {
        ++handledClockGettime;
        const auto clock = va_arg(args, long);
        auto *time = reinterpret_cast<timespec *>(va_arg(args, long));
        result = ::syscall(number, clock, time);
        break;
    }
    case SYS_mkdirat: {
        const auto dirfd = va_arg(args, long);
        const auto *path = reinterpret_cast<const char *>(va_arg(args, long));
        const auto mode = va_arg(args, long);
        result = ::syscall(number, dirfd, path, mode);
        break;
    }
    case SYS_openat: {
        const auto dirfd = va_arg(args, long);
        const auto *path = reinterpret_cast<const char *>(va_arg(args, long));
        const auto flags = va_arg(args, long);
        const auto mode = va_arg(args, long);
        result = ::syscall(number, dirfd, path, flags, mode);
        break;
    }
    case SYS_write: {
        const auto fd = va_arg(args, long);
        const auto *buffer = reinterpret_cast<const void *>(va_arg(args, long));
        const auto size = va_arg(args, long);
        result = ::syscall(number, fd, buffer, size);
        break;
    }
    case SYS_flock: {
        const auto fd = va_arg(args, long);
        const auto operation = va_arg(args, long);
        result = ::syscall(number, fd, operation);
        break;
    }
    case SYS_close: {
        const auto fd = va_arg(args, long);
        result = ::syscall(number, fd);
        break;
    }
    case SYS_sched_yield:
        result = ::syscall(number);
        break;
    default:
        break;
    }
    va_end(args);
    return result;
}
} // namespace

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

    std::array<std::string, 2> threadPaths;
    std::array<std::thread, 2> threads;
    for (std::size_t index = 0; index < threads.size(); ++index) {
        threads[index] = std::thread([index, &threadPaths]() {
            ENABLE_LOGGER();
            Logger logger("worker", "trace.cpp", 20, ::syscall(SYS_gettid), "worker=%zu", index);
            threadPaths[index] = logger.getLogFileName();
        });
    }
    for (auto &thread : threads) {
        thread.join();
    }
    EXPECT_EQ(threadPaths[0], path);
    EXPECT_EQ(threadPaths[1], path);

    std::ifstream input(path, std::ios::binary);
    const std::string data((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    perfetto::protos::Trace trace;
    ASSERT_TRUE(trace.ParseFromString(data));
    ASSERT_EQ(trace.packet_size(), 10);
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
    EXPECT_EQ(std::filesystem::path(path).filename(),
              "calf_" + std::to_string(::getpid()) + ".perfetto-trace");
    EXPECT_EQ(std::filesystem::path(path).parent_path().parent_path(), root);

    std::filesystem::remove_all(root);
}

TEST(SyscallProtobufLoggerTest, RoutesPerfettoSystemCallsThroughConfiguredHandler) {
    handledGetpid = 0;
    handledGettid = 0;
    handledClockGettime = 0;
    SET_CALF_SYSCALL_HANDLER(recordingSyscall);
    ENABLE_LOGGER();
    SyscallLogger::resetProtobufState();

    {
        SyscallLoggingSuspender suspended;
        Logger logger("disabled", "trace.cpp", 79, ::syscall(SYS_gettid), "ignored");
    }
    EXPECT_EQ(handledGetpid.load(), 0);
    EXPECT_EQ(handledGettid.load(), 0);
    EXPECT_EQ(handledClockGettime.load(), 0);

    {
        Logger logger("hook", "trace.cpp", 80, ::syscall(SYS_gettid), "scope");
        logger.log("event");
    }

    EXPECT_GT(handledGetpid.load(), 0);
    EXPECT_GT(handledGettid.load(), 0);
    EXPECT_EQ(handledClockGettime.load(), 3);
    SET_CALF_SYSCALL_HANDLER(::syscall);
}
