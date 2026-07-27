#ifndef CALF_PROTOBUFBASELOGGER_H
#define CALF_PROTOBUFBASELOGGER_H

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

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
        // LD_PRELOAD users can still be intercepted while thread-local destructors run.
#if defined(__CALF_POSIX)
        // Keep these process-lifetime buffers alive to avoid allocator syscalls during teardown.
        static thread_local auto *chunk   = new calf::proto::TraceFile;
        static thread_local auto *encoded = new std::string;
#else
        static thread_local calf::proto::TraceFile chunkStorage;
        static thread_local std::string encodedStorage;
        auto *chunk   = &chunkStorage;
        auto *encoded = &encodedStorage;
#endif
        chunk->Clear();
        calf::proto::TraceRecord *record = chunk->add_records();
        record->set_timestamp_ms(timestamp);
        record->set_scope_id(recordScopeId);
        if (recordParentScopeId != 0) {
            record->set_parent_scope_id(recordParentScopeId);
        }
        record->set_kind(kind);
        if (invoker != nullptr) {
            record->set_invoker(invoker, ::strnlen(invoker, 255));
        }
        if (file != nullptr) {
            record->set_file(file, ::strnlen(file, 255));
        }
        if (line > 0) {
            record->set_line(static_cast<std::uint32_t>(line));
        }
        if (message != nullptr) {
            record->set_args(message, ::strnlen(message, CALF_LOG_MAX_MSG_LEN - 1));
        }
        if (chunk->SerializeToString(encoded)) {
            Derived::rawWriteBytes(encoded->data(), static_cast<int>(encoded->size()));
        }
    }
};

#endif // CALF_PROTOBUFBASELOGGER_H
