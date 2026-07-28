#ifndef CALF_PROTOBUFBASELOGGER_H
#define CALF_PROTOBUFBASELOGGER_H

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "calf/utils/constants.h"
#include "calf/protobuf/calf_trace.pb.h"

template <typename Derived> struct ProtobufLogBase {
    using RecordKind = calf::proto::TraceRecord::Kind;

    std::uint64_t scopeId{0};
    std::uint64_t parentScopeId{0};

    inline static thread_local std::uint64_t currentScopeId = 0;
    inline static thread_local std::uint64_t nextScopeId    = 1;

    void writeOpening(unsigned long timestamp, const char *invoker, const char *file, int line,
                      const char *messageFormat, va_list args) {
        char message[CALF_LOG_MAX_MSG_LEN]{};
        expandMessage(messageFormat, args, message, sizeof(message));

        parentScopeId = currentScopeId;
        scopeId       = nextScopeId++;
        currentScopeId = scopeId;
        writeRecord(timestamp, scopeId, parentScopeId, calf::proto::TraceRecord::SCOPE_ENTER,
                    invoker, file, line, message);
    }

    static void printFormatted(unsigned long timestamp, const char *invoker, const char *file,
                               int line, const char * /*outputTemplate*/,
                               const char *messageFormat, va_list args) {
        char message[CALF_LOG_MAX_MSG_LEN]{};
        expandMessage(messageFormat, args, message, sizeof(message));
        writeRecord(timestamp, currentScopeId, 0, calf::proto::TraceRecord::EVENT, invoker, file,
                    line, message);
    }

    void writeEpilogue(unsigned long timestamp) {
        if (scopeId == 0) {
            return;
        }
        writeRecord(timestamp, scopeId, parentScopeId, calf::proto::TraceRecord::SCOPE_EXIT,
                    nullptr, nullptr, 0, nullptr);
        currentScopeId = parentScopeId;
        if (currentScopeId == 0) {
            Derived::flush();
        }
    }

    static void resetProtobufState() {
        currentScopeId = 0;
        nextScopeId    = 1;
    }

  private:
    static void expandMessage(const char *format, va_list args, char *output,
                              std::size_t outputSize) {
        va_list copy;
        va_copy(copy, args);
        ::vsnprintf(output, outputSize, format, copy);
        va_end(copy);
    }

    static void writeRecord(unsigned long timestamp, std::uint64_t recordScopeId,
                            std::uint64_t recordParentScopeId, RecordKind kind,
                            const char *invoker, const char *file, int line, const char *message) {
        // Concatenating serialized messages is valid protobuf: repeated records
        // from each TraceFile chunk are merged when the complete file is parsed.
        char recordBuffer[CALF_LOG_MAX_MSG_LEN + 1024];
        char *record = recordBuffer;
        appendUInt64(record, 1, timestamp);
        appendUInt64(record, 2, recordScopeId);
        if (recordParentScopeId != 0) {
            appendUInt64(record, 3, recordParentScopeId);
        }
        if (kind != calf::proto::TraceRecord::SCOPE_ENTER) {
            appendUInt64(record, 4, static_cast<std::uint64_t>(kind));
        }
        if (invoker != nullptr) {
            appendString(record, 5, invoker, ::strnlen(invoker, 255));
        }
        if (file != nullptr) {
            appendString(record, 6, file, ::strnlen(file, 255));
        }
        if (line > 0) {
            appendUInt64(record, 7, static_cast<std::uint32_t>(line));
        }
        if (message != nullptr) {
            appendString(record, 8, message, ::strnlen(message, CALF_LOG_MAX_MSG_LEN - 1));
        }

        char outputBuffer[sizeof(recordBuffer) + 16];
        char *output = outputBuffer;
        *output++ = static_cast<char>((1U << 3U) | 2U);
        appendVarint(output, static_cast<std::uint64_t>(record - recordBuffer));
        ::memcpy(output, recordBuffer, static_cast<std::size_t>(record - recordBuffer));
        output += record - recordBuffer;
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
};

#endif // CALF_PROTOBUFBASELOGGER_H
