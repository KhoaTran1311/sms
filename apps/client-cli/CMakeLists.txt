add_executable(client-cli main.cpp)

target_link_libraries(client-cli PRIVATE sms_client)