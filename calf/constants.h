#ifndef CALF_CONSTANTS_H
#define CALF_CONSTANTS_H

#ifndef _CALF_COMPONENT_NAME
#define _CALF_COMPONENT_NAME "calf"
#endif

constexpr unsigned int CALF_LOG_MAX_MSG_LEN           = 4096;
constexpr char CALF_DEFAULT_LOG_FOLDER[]              = "./calf_logs";
constexpr char CALF_SYSCALL_DEFAULT_LOG_FILE_PREFIX[] = "syscall_";
constexpr char CALF_STL_DEFAULT_LOG_FILE_PREFIX[]     = "stl_";
constexpr char CALF_LOG_PRE_MSG[]                     = "%s";
constexpr char CALF_COMPONENT_NAME[]                  = _CALF_COMPONENT_NAME;

#endif // CALF_CONSTANTS_H