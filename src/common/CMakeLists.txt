add_library(sms_common STATIC
  common.cpp
  common.h
  error.h
  result.h
  log.cpp
  args.cpp)

target_include_directories(sms_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

find_package(spdlog CONFIG REQUIRED)
target_link_libraries(sms_common PUBLIC spdlog::spdlog)