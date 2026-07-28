#ifndef CALF_LOGFORMAT_H
#define CALF_LOGFORMAT_H

#if defined(CALF_LOG_FORMAT_PROTOBUF)
#include "calf/format/ProtobufBaseLogger.h"
template <typename Derived> using CalfLogBase = ProtobufLogBase<Derived>;
inline constexpr char CALF_LOG_FILE_EXTENSION[] = ".pb";
inline constexpr bool CALF_LOG_FILE_BINARY = true;
#else
#include "JsonBaseLogger.h"
template <typename Derived> using CalfLogBase = JsonLogBase<Derived>;
inline constexpr char CALF_LOG_FILE_EXTENSION[] = ".log";
inline constexpr bool CALF_LOG_FILE_BINARY = false;
#endif

#endif // CALF_LOGFORMAT_H
