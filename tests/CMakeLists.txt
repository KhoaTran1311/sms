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

add_executable(sms_unit_client
  unit/client_test.cpp)

target_link_libraries(sms_unit_client PRIVATE
  sms_client
  GTest::gtest_main)

add_test(NAME client_test COMMAND sms_unit_client)

add_executable(sms_unit_args
  unit/args_test.cpp)

target_link_libraries(sms_unit_args PRIVATE
  sms_common
  GTest::gtest_main)

add_test(NAME args_test COMMAND sms_unit_args)

add_executable(sms_integration_server
  integration/server_relay_test.cpp)

target_link_libraries(sms_integration_server PRIVATE
  sms_server
  sms_net
  sms_common
  GTest::gtest_main)

target_include_directories(sms_integration_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME sms_integration_server COMMAND sms_integration_server)
set_tests_properties(sms_integration_server PROPERTIES LABELS "integration")

# P0-14: end-to-end tests that spawn the real binaries. The ctest entry
# exports the built paths via SMS_SERVERD_BIN / SMS_CLI_BIN (genexes are
# expanded at test time), and the explicit dependency makes sure both
# binaries exist before the test target builds.
add_executable(sms_integration_client
  integration/client_e2e_test.cpp)

target_link_libraries(sms_integration_client PRIVATE
  sms_client
  sms_net
  sms_common
  GTest::gtest_main)

target_include_directories(sms_integration_client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME sms_integration_client COMMAND sms_integration_client)
set_tests_properties(sms_integration_client PROPERTIES
  LABELS "integration"
  ENVIRONMENT "SMS_SERVERD_BIN=$<TARGET_FILE:serverd>;SMS_CLI_BIN=$<TARGET_FILE:client-cli>")

add_dependencies(sms_integration_client serverd client-cli)
