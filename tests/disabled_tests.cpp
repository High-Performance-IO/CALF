#include "calf/StlLogger.h"

#include <gtest/gtest.h>

static int sideEffect() {
    static int calls = 0;
    return ++calls;
}

TEST(DisabledLoggingTest, DoesNotEvaluateMacroArguments) {
    const int before = sideEffect();
    START_LOG(sideEffect(), "value=%d", sideEffect());
    LOG("value=%d", sideEffect());
    DBG(sideEffect(), sideEffect());
    EXPECT_EQ(sideEffect(), before + 1);
}
