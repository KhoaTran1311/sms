add_library(sms_client STATIC
  client.cpp)

target_include_directories(sms_client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
find_package(asio CONFIG REQUIRED)
target_link_libraries(sms_client PUBLIC sms_common sms_net asio::asio)