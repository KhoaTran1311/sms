find_package(asio CONFIG REQUIRED)

add_library(sms_server STATIC
  server.cpp
  session.cpp)

target_include_directories(sms_server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(sms_server PUBLIC sms_common sms_net asio::asio)
