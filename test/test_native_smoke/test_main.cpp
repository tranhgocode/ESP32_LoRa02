#include <unity.h>

void testNativeRunnerExecutesTests()
{
    TEST_ASSERT_TRUE(true);
}

void setUp()
{
}

void tearDown()
{
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(testNativeRunnerExecutesTests);
    return UNITY_END();
}
