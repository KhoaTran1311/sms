#include "common/result.h"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>

namespace sms::common {
namespace {

TEST(ResultTest, Ok_ProvidesValue) {
  Result<int> r = Result<int>::Ok(42);
  EXPECT_TRUE(r.ok());
  EXPECT_TRUE(r.has_value());
  EXPECT_EQ(42, r.value());
}

TEST(ResultTest, Err_ReportsCodeAndMessage) {
  Result<int> r = Result<int>::Err(Error::Make(ErrorCode::kIoError, "socket gone"));
  EXPECT_FALSE(r.ok());
  EXPECT_FALSE(r.has_value());
  EXPECT_EQ(ErrorCode::kIoError, r.error().code);
  EXPECT_EQ("socket gone", r.error().message);
}

TEST(ResultTest, Value_OnError_ThrowsLogicError) {
  Result<int> r = Result<int>::Err(Error::Make(ErrorCode::kInternal, "boom"));
  EXPECT_THROW(r.value(), std::logic_error);
}

TEST(ResultTest, TakeValue_Moves) {
  Result<std::unique_ptr<int>> r =
      Result<std::unique_ptr<int>>::Ok(std::make_unique<int>(7));
  std::unique_ptr<int> moved = r.TakeValue();
  EXPECT_EQ(7, *moved);
}

TEST(ResultTest, ResultVoid_Ok) {
  Result<void> r = Result<void>::Ok();
  EXPECT_TRUE(r.ok());
  EXPECT_TRUE(r.has_value());
  EXPECT_NO_THROW(r.value());
}

TEST(ResultTest, ResultVoid_Err) {
  Result<void> r = Result<void>::Err(Error::Make(ErrorCode::kDisconnected, "peer left"));
  EXPECT_FALSE(r.ok());
  EXPECT_EQ(ErrorCode::kDisconnected, r.error().code);
  EXPECT_THROW(r.value(), std::logic_error);
}

TEST(ResultTest, Error_Make_Helper) {
  Error e = Error::Make(ErrorCode::kIoError, "x");
  EXPECT_EQ(ErrorCode::kIoError, e.code);
  EXPECT_EQ("x", e.message);
}

}  // namespace
}  // namespace sms::common