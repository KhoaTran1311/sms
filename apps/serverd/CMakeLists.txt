add_executable(serverd main.cpp)

# P0-10: daemon entry point. Links the server relay (P0-09) and the shared
# CLI-parsing/logging helpers (P0-02, pre-seeded args).
target_link_libraries(serverd PRIVATE sms_server sms_common)

# --version prints "serverd 0.1.0"; keep in sync with the project version.
target_compile_definitions(serverd PRIVATE SMS_VERSION="${PROJECT_VERSION}")
