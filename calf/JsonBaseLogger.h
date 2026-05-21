#ifndef CALF_JSONBASELOGGER_H
#define CALF_JSONBASELOGGER_H

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

#include "constants.h"

template <typename Derived> struct JsonLogBase {

    static thread_local __attribute__((tls_model("initial-exec"))) int nestingDepth;
    static thread_local __attribute__((tls_model("initial-exec"))) bool rootArrayOpen;
    static thread_local __attribute__((tls_model("initial-exec"))) int pendingLen;
    static thread_local
        __attribute__((tls_model("initial-exec"))) char pendingBuf[CALF_LOG_MAX_MSG_LEN * 6 + 256];

    static void printFormatted(unsigned long int timestamp, const char *invoker, const char *file,
                               int line, const char * /*output_template*/,
                               const char *message_format, va_list args) {
        char escaped_args[CALF_LOG_MAX_MSG_LEN * 6];
        expandAndEscape(message_format, args, escaped_args, sizeof(escaped_args));

        char escaped_invoker[512];
        jsonEscape(invoker, static_cast<int>(::strlen(invoker)), escaped_invoker,
                   sizeof(escaped_invoker));

        char escaped_file[512];
        jsonEscape(file, static_cast<int>(::strlen(file)), escaped_file, sizeof(escaped_file));

        flushPending(true);

        const int indent = indentSize();
        char *p          = pendingBuf;
        for (int i = 0; i < indent; ++i) {
            *p++ = ' ';
        }
        p += ::snprintf(p, sizeof(pendingBuf) - static_cast<size_t>(indent) - 1,
                        "{ \"ts\": %lu, \"invoker\": \"%s\","
                        " \"file\": \"%s\", \"line\": %d, \"args\": \"%s\" }",
                        timestamp, escaped_invoker, escaped_file, line, escaped_args);
        pendingLen = p - pendingBuf;
    }

    static void writeOpening(unsigned long int timestamp, const char *invoker, const char *file,
                             int line, const char *message_format, va_list args) {
        if (!rootArrayOpen) {
            Derived::rawWriteStr("[\n");
            rootArrayOpen = true;
            nestingDepth  = 1;
        }

        char escaped_args[CALF_LOG_MAX_MSG_LEN * 6];
        expandAndEscape(message_format, args, escaped_args, sizeof(escaped_args));

        char escaped_invoker[512];
        jsonEscape(invoker, static_cast<int>(::strlen(invoker)), escaped_invoker,
                   sizeof(escaped_invoker));

        char escaped_file[512];
        jsonEscape(file, static_cast<int>(::strlen(file)), escaped_file, sizeof(escaped_file));

        flushPending(true);

        writeImmediate("{");
        nestingDepth++;

        writeField("\"invoker\"", "\"%s\",", escaped_invoker);
        writeField("\"file\"", "\"%s\",", escaped_file);
        writeFieldInt("\"line\"", "%d,", line);
        writeFieldUL("\"ts_enter\"", "%lu,", timestamp);
        writeField("\"args\"", "\"%s\",", escaped_args);

        writeImmediate("\"events\": [");
        nestingDepth++;

        pendingLen = 0;
    }

    static void writeEpilogue(unsigned long int timestamp) {
        if (nestingDepth < 2) {
            return;
        }

        flushPending(false);

        nestingDepth--;
        writeImmediate("],");

        {
            char buf[64];
            ::snprintf(buf, sizeof(buf), "\"ts_exit\": %lu", timestamp);
            writeImmediate(buf);
        }

        nestingDepth--;
        char *p          = pendingBuf;
        const int indent = indentSize();
        for (int i = 0; i < indent; ++i) {
            *p++ = ' ';
        }
        *p++       = '}';
        pendingLen = p - pendingBuf;
    }

  protected:
    static void flushPending(bool withComma) {
        if (pendingLen <= 0) {
            return;
        }
        Derived::rawWriteBytes(pendingBuf, pendingLen);
        Derived::rawWriteStr(withComma ? ",\n" : "\n");
        pendingLen = 0;
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
        const int n = nestingDepth * 2;
        return n < 64 ? n : 64;
    }

    static void expandAndEscape(const char *fmt, va_list args, char *dst, int dst_size) {
        char raw[CALF_LOG_MAX_MSG_LEN]{0};

        va_list copy;
        va_copy(copy, args);
        const int raw_len = ::vsnprintf(raw, sizeof(raw), fmt, copy);
        va_end(copy);

        if (raw_len <= 0) {
            dst[0] = '\0';
            return;
        }

        const int written =
            raw_len < static_cast<int>(sizeof(raw)) ? raw_len : static_cast<int>(sizeof(raw)) - 1;

        jsonEscape(raw, written, dst, dst_size);
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

template <typename D>
thread_local __attribute__((tls_model("initial-exec"))) int JsonLogBase<D>::nestingDepth = 0;
template <typename D>
thread_local __attribute__((tls_model("initial-exec"))) bool JsonLogBase<D>::rootArrayOpen = false;
template <typename D>
thread_local __attribute__((tls_model("initial-exec"))) int JsonLogBase<D>::pendingLen = 0;
template <typename D>
thread_local __attribute__((tls_model(
    "initial-exec"))) char JsonLogBase<D>::pendingBuf[CALF_LOG_MAX_MSG_LEN * 6 + 256] = {'\0'};

#endif