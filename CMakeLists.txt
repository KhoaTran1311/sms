cmake_minimum_required(VERSION 3.27)
project(sms VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SMS_BUILD_TESTS "Build unit and integration tests" ON)

# vcpkg toolchain: prefer the vendored submodule when present, so `cmake -S . -B build`
# works on dev machines without extra flags.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND EXISTS
   "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake")
  set(CMAKE_TOOLCHAIN_FILE
      "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake"
      CACHE FILEPATH "vcpkg toolchain file")
endif()

add_subdirectory(src/common)
add_subdirectory(src/net)
add_subdirectory(src/server)
add_subdirectory(src/client)
add_subdirectory(apps/serverd)
add_subdirectory(apps/client-cli)

if(SMS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()