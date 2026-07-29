#ifndef CALF_PROTOBUFBASELOGGER_H
#define CALF_PROTOBUFBASELOGGER_H

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unistd.h>

#include "calf/utils/constants.h"

template <typename Derived> struct ProtobufLogBase {
    enum class EventType : std::uint64_t { SliceBegin = 1, SliceEnd = 2, Instant = 3 };

    bool scopeOpen{false};
    inline static thread_local bool descriptorWritten = false;
    inline static thread_local pid_t descriptorPid = 0;

    void writeOpening(unsigned long timestamp, const char *invoker, const char *file, int line,
                      const char *messageFormat, va_list args) {
        char message[CALF_LOG_MAX_MSG_LEN]{};
        expandMessage(messageFormat, args, message, sizeof(message));

        ensureTrackDescriptor();
        scopeOpen = true;
        writeEvent(timestamp, EventType::SliceBegin, invoker, file, line, message);
    }

    static void printFormatted(unsigned long timestamp, const char *invoker, const char *file,
                               int line, const char * /*outputTemplate*/,
                               const char *messageFormat, va_list args) {
        char message[CALF_LOG_MAX_MSG_LEN]{};
        expandMessage(messageFormat, args, message, sizeof(message));
        ensureTrackDescriptor();
        writeEvent(timestamp, EventType::Instant, invoker, file, line, message);
    }

    void writeEpilogue(unsigned long timestamp) {
        if (!scopeOpen) {
            return;
        }
        writeEvent(timestamp, EventType::SliceEnd, nullptr, nullptr, 0, nullptr);
        scopeOpen = false;
        Derived::flush();
    }

    static void resetProtobufState() {
        descriptorWritten = false;
        descriptorPid = 0;
    }

  private:
    static void expandMessage(const char *format, va_list args, char *output,
                              std::size_t outputSize) {
        va_list copy;
        va_copy(copy, args);
        ::vsnprintf(output, outputSize, format, copy);
        va_end(copy);
    }

    static std::uint64_t trackUuid() {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(Derived::processId()))
                << 32U) |
               static_cast<std::uint32_t>(Derived::threadId());
    }

    static void ensureTrackDescriptor() {
        const auto pid = Derived::processId();
        if (descriptorWritten && descriptorPid == pid) {
            return;
        }

        char threadBuffer[128];
        char *thread = threadBuffer;
        appendUInt64(thread, 1, static_cast<std::uint32_t>(pid));
        appendUInt64(thread, 2, static_cast<std::uint64_t>(Derived::threadId()));
        appendString(thread, 5, "CALF thread", 11);

        char descriptorBuffer[256];
        char *descriptor = descriptorBuffer;
        appendUInt64(descriptor, 1, trackUuid());
        appendString(descriptor, 2, "CALF", 4);
        appendMessage(descriptor, 4, threadBuffer, thread);

        char packetBuffer[320];
        char *packet = packetBuffer;
        appendUInt64(packet, 10, sequenceId());
        appendMessage(packet, 60, descriptorBuffer, descriptor);
        writePacket(packetBuffer, packet);
        descriptorWritten = true;
        descriptorPid = pid;
    }

    static void writeEvent(unsigned long timestamp, EventType type, const char *invoker,
                           const char *file, int line, const char *message) {
        char eventBuffer[CALF_LOG_MAX_MSG_LEN + 1024];
        char *event = eventBuffer;
        appendUInt64(event, 9, static_cast<std::uint64_t>(type));
        appendUInt64(event, 11, trackUuid());
        if (type != EventType::SliceEnd) {
            appendString(event, 22, "calf", 4);
        }
        if (invoker != nullptr) {
            appendString(event, 23, invoker, ::strnlen(invoker, 255));
        }
        if (message != nullptr) {
            char annotationBuffer[CALF_LOG_MAX_MSG_LEN + 32];
            char *annotation = annotationBuffer;
            appendString(annotation, 10, "args", 4);
            appendString(annotation, 6, message, ::strnlen(message, CALF_LOG_MAX_MSG_LEN - 1));
            appendMessage(event, 4, annotationBuffer, annotation);
        }
        if (file != nullptr) {
            char locationBuffer[768];
            char *location = locationBuffer;
            appendString(location, 2, file, ::strnlen(file, 255));
            if (invoker != nullptr) {
                appendString(location, 3, invoker, ::strnlen(invoker, 255));
            }
            if (line > 0) {
                appendUInt64(location, 4, static_cast<std::uint32_t>(line));
            }
            appendMessage(event, 33, locationBuffer, location);
        }

        char packetBuffer[sizeof(eventBuffer) + 64];
        char *packet = packetBuffer;
        appendUInt64(packet, 8, timestamp);
        appendUInt64(packet, 10, sequenceId());
        appendMessage(packet, 11, eventBuffer, event);
        writePacket(packetBuffer, packet);
    }

    static std::uint32_t sequenceId() {
        const auto pid = static_cast<std::uint32_t>(Derived::processId());
        const auto tid = static_cast<std::uint32_t>(Derived::threadId());
        const auto id = (pid * 2246822519U) ^ tid;
        return id == 0 ? 1 : id;
    }

    static void writePacket(const char *begin, const char *end) {
        char outputBuffer[CALF_LOG_MAX_MSG_LEN + 1120];
        char *output = outputBuffer;
        appendMessage(output, 1, begin, end);
        Derived::rawWriteBytes(outputBuffer, static_cast<int>(output - outputBuffer));
    }

    static void appendVarint(char *&output, std::uint64_t value) {
        while (value >= 0x80U) {
            *output++ = static_cast<char>((value & 0x7fU) | 0x80U);
            value >>= 7U;
        }
        *output++ = static_cast<char>(value);
    }

    static void appendUInt64(char *&output, std::uint32_t field, std::uint64_t value) {
        appendVarint(output, static_cast<std::uint64_t>(field << 3U));
        appendVarint(output, value);
    }

    static void appendString(char *&output, std::uint32_t field, const char *value,
                              std::size_t length) {
        appendVarint(output, static_cast<std::uint64_t>((field << 3U) | 2U));
        appendVarint(output, length);
        ::memcpy(output, value, length);
        output += length;
    }

    static void appendMessage(char *&output, std::uint32_t field, const char *begin,
                              const char *end) {
        appendString(output, field, begin, static_cast<std::size_t>(end - begin));
    }
};

#endif // CALF_PROTOBUFBASELOGGER_H
