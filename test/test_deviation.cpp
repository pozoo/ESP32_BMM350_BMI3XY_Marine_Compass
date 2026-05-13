#include <unity.h>
#include <iostream>
#include <cmath>

// Mock ArduinoLog for native testing
#ifdef NATIVE_TEST
#define Log MockLog
class MockLogClass {
public:
    template<typename... Args>
    void errorln(const char* format, Args... args) {
        printf("ERROR: ");
        printf(format, args...);
        printf("\n");
    }
};
static MockLogClass MockLog;
#endif

#include "../../src/common/Deviation.h"

void setUp(void) {
    // Set up before each test
}

void tearDown(void) {
    // Clean up after each test
}

void test_deviation_initialization() {
    Deviation dev;
    
    // Check that deviation table is initialized
    TEST_ASSERT_EQUAL(0, dev.deviationTable[0].compassHeading);
    TEST_ASSERT_EQUAL(30, dev.deviationTable[1].compassHeading);
    TEST_ASSERT_EQUAL(360, dev.deviationTable[12].compassHeading);
    
    // Check initial deviation values are zero
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, dev.deviationTable[0].deviation);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, dev.deviationTable[6].deviation);
}

void test_deviation_exact_match() {
    Deviation dev;
    
    // Set some test deviation values
    dev.deviationTable[0].deviation = -7.0f;   // 0°
    dev.deviationTable[1].deviation = -5.0f;   // 30°
    dev.deviationTable[2].deviation = -1.0f;   // 60°
    
    // Test exact match at 30 degrees
    float result = dev.binarySearchInterpolation(30.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -5.0f, result);
}

void test_deviation_interpolation() {
    Deviation dev;
    
    // Set test values
    dev.deviationTable[0].deviation = -7.0f;   // 0°
    dev.deviationTable[1].deviation = -5.0f;   // 30°
    
    // Test interpolation at 15 degrees (halfway between 0 and 30)
    // Expected: -7.0 + (-5.0 - -7.0) * (15 - 0) / (30 - 0) = -7.0 + 2.0 * 0.5 = -6.0
    float result = dev.binarySearchInterpolation(15.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -6.0f, result);
}

void test_deviation_interpolation_complex() {
    Deviation dev;
    
    // Set realistic deviation pattern
    dev.deviationTable[0].deviation = -7.0f;   // 0°
    dev.deviationTable[1].deviation = -5.0f;   // 30°
    dev.deviationTable[2].deviation = -1.0f;   // 60°
    dev.deviationTable[3].deviation = 1.0f;    // 90°
    dev.deviationTable[4].deviation = -1.0f;   // 120°
    
    // Test interpolation at 45 degrees (halfway between 30 and 60)
    // Expected: -5.0 + (-1.0 - -5.0) * (45 - 30) / (60 - 30) = -5.0 + 4.0 * 0.5 = -3.0
    float result = dev.binarySearchInterpolation(45.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -3.0f, result);
    
    // Test interpolation at 75 degrees (halfway between 60 and 90)
    // Expected: -1.0 + (1.0 - -1.0) * (75 - 60) / (90 - 60) = -1.0 + 2.0 * 0.5 = 0.0
    result = dev.binarySearchInterpolation(75.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, result);
}

void test_getCorrectedHeading() {
    Deviation dev;
    
    // Set deviation at 0 degrees
    dev.deviationTable[0].deviation = -5.0f;
    
    // Test corrected heading
    float correctedHeading = dev.getCorrectedHeading(0.0f);
    // Expected: 0.0 + (-5.0) = -5.0, then normalized: -5.0 + 360 = 355.0
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 355.0f, correctedHeading);
    
    // Test with positive deviation
    dev.deviationTable[3].deviation = 5.0f;  // 90°
    correctedHeading = dev.getCorrectedHeading(90.0f);
    // Expected: 90.0 + 5.0 = 95.0
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 95.0f, correctedHeading);
}

void test_getCorrectedHeading_wraparound() {
    Deviation dev;
    
    // Set deviation that will cause wraparound
    dev.deviationTable[12].deviation = 10.0f;  // 360°/0°
    
    float correctedHeading = dev.getCorrectedHeading(355.0f);
    // Result should wrap around if > 360
    TEST_ASSERT_TRUE(correctedHeading >= 0.0f && correctedHeading < 360.0f);
}

void test_json_output() {
    Deviation dev;
    char buf[1024];
    
    // Set some test values
    dev.deviationTable[0].deviation = -7.0f;
    dev.deviationTable[0].dataCollectionStatus = 1.0f;
    dev.deviationTable[1].deviation = -5.0f;
    dev.deviationTable[1].dataCollectionStatus = 0.5f;
    
    dev.getDeviationTableAsJson(buf, sizeof(buf));
    
    std::cout << "\n=== JSON Output ===\n" << buf << "\n==================\n" << std::endl;
    
    // Basic sanity checks
    TEST_ASSERT_TRUE(strlen(buf) > 0);
    TEST_ASSERT_TRUE(strstr(buf, "compassHeading") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "deviation") != NULL);
    TEST_ASSERT_TRUE(strstr(buf, "status") != NULL);
}

void test_compassDeviationAsString() {
    Deviation dev;
    char buf[200];
    
    // Set some test values
    dev.deviationTable[0].deviation = -7.5f;
    dev.deviationTable[1].deviation = -5.2f;
    dev.deviationTable[2].deviation = 0.0f;
    
    dev.compassDeviationAsString(buf, sizeof(buf));
    
    std::cout << "\n=== String Output ===\n" << buf << "\n====================\n" << std::endl;
    
    TEST_ASSERT_TRUE(strlen(buf) > 0);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    
    RUN_TEST(test_deviation_initialization);
    RUN_TEST(test_deviation_exact_match);
    RUN_TEST(test_deviation_interpolation);
    RUN_TEST(test_deviation_interpolation_complex);
    RUN_TEST(test_getCorrectedHeading);
    RUN_TEST(test_getCorrectedHeading_wraparound);
    RUN_TEST(test_json_output);
    RUN_TEST(test_compassDeviationAsString);
    
    return UNITY_END();
}
