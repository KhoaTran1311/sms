find_package(GTest CONFIG REQUIRED)

add_executable(sms_unit_result
  unit/result_test.cpp)

target_link_libraries(sms_unit_result PRIVATE
  sms_common
  GTest::gtest_main)

add_test(NAME result_test COMMAND sms_unit_result)

add_executable(sms_unit_session
  unit/session_test.cpp)

target_link_libraries(sms_unit_session PRIVATE
  sms_server
  sms_net
  sms_common
  GTest::gtest_main)

target_include_directories(sms_unit_session PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME session_test COMMAND sms_unit_session)

add_executable(sms_unit_log
  unit/log_test.cpp)

target_link_libraries(sms_unit_log PRIVATE GTest::gtest_main sms_common)

include(GoogleTest)
gtest_discover_tests(sms_unit_log TEST_PREFIX "log_test.")

add_executable(sms_unit_server_start
  unit/server_start_test.cpp)

target_link_libraries(sms_unit_server_start PRIVATE
  sms_server
  sms_net
  sms_common
  GTest::gtest_main)

target_include_directories(sms_unit_server_start PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME server_start_test COMMAND sms_unit_server_start)