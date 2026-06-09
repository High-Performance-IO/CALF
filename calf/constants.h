#ifndef CALF_CONSTANTS_H
#define CALF_CONSTANTS_H

#ifndef HOST_NAME_MAX
#if defined(_POSIX_HOST_NAME_MAX)
#define HOST_NAME_MAX _POSIX_HOST_NAME_MAX
#elif defined(MAXHOSTNAMELEN)
#define HOST_NAME_MAX MAXHOSTNAMELEN
#else
#define HOST_NAME_MAX 255
#endif
#endif

constexpr unsigned int CALF_LOG_MAX_MSG_LEN           = 4096;
constexpr char CALF_DEFAULT_LOG_FOLDER[]              = "./calf_logs";
constexpr char CALF_SYSCALL_DEFAULT_LOG_FILE_PREFIX[] = "syscall_";
constexpr char CALF_STL_DEFAULT_LOG_FILE_PREFIX[]     = "stl_";
constexpr char CALF_LOG_PRE_MSG[]                     = "%s";

#endif // CALF_CONSTANTS_H