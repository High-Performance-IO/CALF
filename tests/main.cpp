#include "calf/StdOutLogger.h"
#include "calf/StlLogger.h"


int main() {
    CALF_PRINT_COLOR(CALF_CLI_LEVEL_INFO, "Hello world");
    START_LOG(gettid(), "call()");
}