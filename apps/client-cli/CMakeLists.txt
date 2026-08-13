add_executable(client-cli main.cpp)

# CI (P0-06) expects build/ci/apps/client-cli/sms-cli as the artifact; keep the
# logical target name (client-cli) but emit the binary as sms-cli.
set_target_properties(client-cli PROPERTIES OUTPUT_NAME "sms-cli")

target_compile_definitions(client-cli PRIVATE SMS_CLI_VERSION="${PROJECT_VERSION}")

target_link_libraries(client-cli PRIVATE sms_client sms_common)
