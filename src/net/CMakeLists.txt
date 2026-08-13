add_library(sms_net STATIC
  net.cpp
  frame_codec.cpp)

target_include_directories(sms_net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(sms_net PUBLIC sms_common)