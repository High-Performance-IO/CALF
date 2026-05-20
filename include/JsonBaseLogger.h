#ifndef CAPTURA_JSONBASELOGGER_H
#define CAPTURA_JSONBASELOGGER_H

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

#include "constants.h"

/**
 * CRTP mixin that implements the full structured-JSON adapter interface
 * required by TemplateLogger.  Derived adapters supply two I/O primitives:
 *
 *   static void rawWriteBytes(const char *buf, int len);
 *   static void rawWriteStr (const char *buf);   // NUL-terminated
 *
 * All JSON structure, indentation, pending-comma handling, and string
 * escaping live here, shared by SyscallLogger and StlLogger.
 *
 * This file is header-only because every method is a template instantiation
 * and the compiler needs the full definitions at each use site.
 */
template <typename Derived> struct JsonLogBase {

    // ------------------------------------------------------------------ //
    //  Per-thread JSON state
    // ------------------------------------------------------------------ //

    static thread_local __attribute__((tls_model("initial-exec"))) int nestingDepth;
    static thread_local __attribute__((tls_model("initial-exec"))) bool rootArrayOpen;

    // Holds the last-written event line WITHOUT its trailing newline.
    // Flushed with ",\n" when a sibling follows, "\n" when the array closes.
    // This deferred-write approach is the only way to avoid trailing commas
    // on an append-only stream without seeking back.
    static thread_local __attribute__((tls_model("initial-exec"))) int pendingLen;
    static thread_local
        __attribute__((tls_model("initial-exec"))) char pendingBuf[CAPIO_LOG_MAX_MSG_LEN * 6 + 256];

    // ------------------------------------------------------------------ //
    //  TemplateLogger adapter interface
    // ------------------------------------------------------------------ //

    static void write(const char * /*legacy*/, const size_t /*legacy*/) {
        // ts_exit is emitted by writeEpilogue; nothing to do here.
    }

    /**
     * Called for every LOG(...) inside a scope.
     * Flushes the previous pending entry with a comma, then stores this
     * event object in pendingBuf WITHOUT a trailing newline.
     */
    static void printFormatted(unsigned long int timestamp, const char *invoker, const char *file,
                               int line, const char * /*output_template*/,
                               const char *message_format, va_list args) {
        char escaped_args[CAPIO_LOG_MAX_MSG_LEN * 6];
        expandAndEscape(message_format, args, escaped_args, static_cast<int>(sizeof(escaped_args)));

        char escaped_invoker[512];
        jsonEscape(invoker, static_cast<int>(::strlen(invoker)), escaped_invoker,
                   static_cast<int>(sizeof(escaped_invoker)));

        char escaped_file[512];
        jsonEscape(file, static_cast<int>(::strlen(file)), escaped_file,
                   static_cast<int>(sizeof(escaped_file)));

        flushPending(true);

        const int indent = indentSize();
        char *p          = JsonLogBase<Derived>::pendingBuf;
        for (int i = 0; i < indent; ++i) {
            *p++ = ' ';
        }
        p += ::snprintf(p,
                        sizeof(JsonLogBase<Derived>::pendingBuf) - static_cast<size_t>(indent) - 1,
                        "{ \"ts\": %lu, \"invoker\": \"%s\","
                        " \"file\": \"%s\", \"line\": %d, \"args\": \"%s\" }",
                        timestamp, escaped_invoker, escaped_file, line, escaped_args);
        JsonLogBase<Derived>::pendingLen = static_cast<int>(p - JsonLogBase<Derived>::pendingBuf);
    }

    /**
     * Called by TemplateLogger constructor (current_log_level == 1).
     * Opens one JSON object in the root array:
     *
     *   {
     *     "invoker":  "...",
     *     "file":     "...",
     *     "line":     N,
     *     "ts_enter": T,
     *     "args":     "...",
     *     "events": [
     */
    static void writeOpening(unsigned long int timestamp, const char *invoker, const char *file,
                             int line, const char *message_format, va_list args) {
        if (!JsonLogBase<Derived>::rootArrayOpen) {
            Derived::rawWriteStr("[\n");
            JsonLogBase<Derived>::rootArrayOpen = true;
            JsonLogBase<Derived>::nestingDepth  = 1;
        }

        char escaped_args[CAPIO_LOG_MAX_MSG_LEN * 6];
        expandAndEscape(message_format, args, escaped_args, static_cast<int>(sizeof(escaped_args)));

        char escaped_invoker[512];
        jsonEscape(invoker, static_cast<int>(::strlen(invoker)), escaped_invoker,
                   static_cast<int>(sizeof(escaped_invoker)));

        char escaped_file[512];
        jsonEscape(file, static_cast<int>(::strlen(file)), escaped_file,
                   static_cast<int>(sizeof(escaped_file)));

        flushPending(true); // close previous sibling object with ","

        writeImmediate("{");
        JsonLogBase<Derived>::nestingDepth++;

        writeField("\"invoker\"", "\"%s\",", escaped_invoker);
        writeField("\"file\"", "\"%s\",", escaped_file);
        writeFieldInt("\"line\"", "%d,", line);
        writeFieldUL("\"ts_enter\"", "%lu,", timestamp);
        writeField("\"args\"", "\"%s\",", escaped_args);

        writeImmediate("\"events\": [");
        JsonLogBase<Derived>::nestingDepth++;

        JsonLogBase<Derived>::pendingLen = 0;
    }

    /**
     * Called by TemplateLogger destructor (current_log_level == 1).
     * Closes the "events" array, emits "ts_exit", and stores the closing
     * "}" as a pending line so the next sibling gets a leading comma.
     */
    static void writeEpilogue(unsigned long int timestamp) {
        if (JsonLogBase<Derived>::nestingDepth < 2) {
            return;
        }

        flushPending(false); // last event — no trailing comma

        JsonLogBase<Derived>::nestingDepth--;
        writeImmediate("],"); // close "events", comma before "ts_exit"

        {
            char buf[64];
            ::snprintf(buf, sizeof(buf), "\"ts_exit\": %lu", timestamp);
            writeImmediate(buf);
        }

        // Store closing "}" as pending so the next writeOpening can
        // prepend "," to it when a sibling object follows.
        JsonLogBase<Derived>::nestingDepth--;
        char *p          = JsonLogBase<Derived>::pendingBuf;
        const int indent = indentSize();
        for (int i = 0; i < indent; ++i) {
            *p++ = ' ';
        }
        *p++                             = '}';
        JsonLogBase<Derived>::pendingLen = static_cast<int>(p - JsonLogBase<Derived>::pendingBuf);
    }

  protected:
    // ------------------------------------------------------------------ //
    //  Internal helpers
    // ------------------------------------------------------------------ //

    static void flushPending(bool withComma) {
        if (JsonLogBase<Derived>::pendingLen <= 0) {
            return;
        }
        Derived::rawWriteBytes(JsonLogBase<Derived>::pendingBuf, JsonLogBase<Derived>::pendingLen);
        Derived::rawWriteStr(withComma ? ",\n" : "\n");
        JsonLogBase<Derived>::pendingLen = 0;
    }

    static void writeImmediate(const char *buf, int len = -1) {
        if (len < 0) {
            len = static_cast<int>(::strlen(buf));
        }
        const int indent = indentSize();
        char spaces[65]  = {0};
        for (int i = 0; i < indent; ++i) {
            spaces[i] = ' ';
        }
        Derived::rawWriteBytes(spaces, indent);
        Derived::rawWriteBytes(buf, len);
        Derived::rawWriteStr("\n");
    }

    static void writeField(const char *key, const char *fmt, const char *val) {
        char tmp[768];
        ::snprintf(tmp, sizeof(tmp), fmt, val);
        char buf[1024];
        ::snprintf(buf, sizeof(buf), "%s: %s", key, tmp);
        writeImmediate(buf);
    }
    static void writeFieldInt(const char *key, const char *fmt, int val) {
        char tmp[64];
        ::snprintf(tmp, sizeof(tmp), fmt, val);
        char buf[128];
        ::snprintf(buf, sizeof(buf), "%s: %s", key, tmp);
        writeImmediate(buf);
    }
    static void writeFieldUL(const char *key, const char *fmt, unsigned long val) {
        char tmp[64];
        ::snprintf(tmp, sizeof(tmp), fmt, val);
        char buf[128];
        ::snprintf(buf, sizeof(buf), "%s: %s", key, tmp);
        writeImmediate(buf);
    }

    static int indentSize() {
        const int n = JsonLogBase<Derived>::nestingDepth * 2;
        return n < 64 ? n : 64;
    }

    static void expandAndEscape(const char *fmt, va_list args, char *dst, int dst_size) {
        va_list copy;
        va_copy(copy, args);
        const int raw_len = ::vsnprintf(nullptr, 0, fmt, copy);
        va_end(copy);

        std::string raw(static_cast<size_t>(raw_len) + 1, '\0');
        va_copy(copy, args);
        ::vsnprintf(&raw[0], static_cast<size_t>(raw_len) + 1, fmt, copy);
        va_end(copy);

        jsonEscape(raw.c_str(), raw_len, dst, dst_size);
    }

    static void jsonEscape(const char *src, int src_len, char *dst, int dst_size) {
        int di = 0;
        for (int si = 0; si < src_len && di + 7 < dst_size; ++si) {
            const unsigned char c = static_cast<unsigned char>(src[si]);
            if (c == '"' || c == '\\') {
                dst[di++] = '\\';
                dst[di++] = static_cast<char>(c);
            } else if (c == '\n') {
                dst[di++] = '\\';
                dst[di++] = 'n';
            } else if (c == '\r') {
                dst[di++] = '\\';
                dst[di++] = 'r';
            } else if (c == '\t') {
                dst[di++] = '\\';
                dst[di++] = 't';
            } else if (c < 0x20 || c == 0x7f) {
                dst[di++] = '\\';
                dst[di++] = 'u';
                dst[di++] = '0';
                dst[di++] = '0';
                dst[di++] = hexChar(c >> 4);
                dst[di++] = hexChar(c & 0x0fu);
            } else {
                dst[di++] = static_cast<char>(c);
            }
        }
        dst[di] = '\0';
    }

    static char hexChar(unsigned int n) {
        return static_cast<char>(n < 10u ? '0' + n : 'a' + n - 10u);
    }
};

// Out-of-class thread_local definitions.
// Template statics are implicitly inline in C++17, so these are safe in a
// header — each concrete instantiation (SyscallLogger, StlLogger, …) gets
// its own independent per-thread storage.
template <typename D>
thread_local __attribute__((tls_model("initial-exec"))) int JsonLogBase<D>::nestingDepth = 0;
template <typename D>
thread_local __attribute__((tls_model("initial-exec"))) bool JsonLogBase<D>::rootArrayOpen = false;
template <typename D>
thread_local __attribute__((tls_model("initial-exec"))) int JsonLogBase<D>::pendingLen = 0;
template <typename D>
thread_local __attribute__((tls_model(
    "initial-exec"))) char JsonLogBase<D>::pendingBuf[CAPIO_LOG_MAX_MSG_LEN * 6 + 256] = {'\0'};

#endif // CAPTURA_JSONBASELOGGER_H