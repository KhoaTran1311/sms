find_package(GTest CONFIG REQUIRED)

add_executable(sms_unit_log
  unit/log_test.cpp)

target_link_libraries(sms_unit_log PRIVATE GTest::gtest_main sms_common)

include(GoogleTest)
gtest_discover_tests(sms_unit_log TEST_PREFIX "log_test.")