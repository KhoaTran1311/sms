find_package(GTest CONFIG REQUIRED)

# add_sms_unit_test(name sources...) — registers a deterministic, socket-free
# gtest unit test target: links sms_net + sms_common + GTest::gtest_main and
# adds a ctest entry (label `unit`) that runs the binary from its build dir.
function(add_sms_unit_test name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE sms_net sms_common GTest::gtest_main)
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES
    LABELS unit
    WORKING_DIRECTORY $<TARGET_FILE_DIR:${name}>)
endfunction()

add_sms_unit_test(sms_unit_frame_codec_test unit/frame_codec_test.cpp)
