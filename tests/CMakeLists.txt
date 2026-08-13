find_package(GTest CONFIG REQUIRED)

add_executable(sms_unit_result
  unit/result_test.cpp)

target_link_libraries(sms_unit_result PRIVATE
  sms_common
  GTest::gtest_main)

add_test(NAME result_test COMMAND sms_unit_result)