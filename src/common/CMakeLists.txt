add_library(sms_common STATIC
  common.cpp)

target_include_directories(sms_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)