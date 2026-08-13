add_executable(serverd main.cpp)

target_link_libraries(serverd PRIVATE sms_server)