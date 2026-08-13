find_package(GTest CONFIG REQUIRED)

add_executable(sms_unit_result
  unit/result_test.cpp)

target_link_libraries(sms_unit_result PRIVATE
  sms_common
  GTest::gtest_main)

add_test(NAME result_test COMMAND sms_unit_result)

add_executable(sms_unit_client
  unit/client_test.cpp)

target_link_libraries(sms_unit_client PRIVATE
  sms_client
  GTest::gtest_main)

add_test(NAME client_test COMMAND sms_unit_client)