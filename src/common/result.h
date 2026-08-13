#pragma once

#include "common/error.h"

#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace sms::common {

// Minimal std::expected-style Result. C++20 placeholder; swap for
// std::expected when the project moves to C++23 (API mirrors it:
// ok/value/error). Domain operations return Result<T> and never throw;
// value()/TakeValue() throw std::logic_error only on misuse (accessing an
// error Result as if it held a value).
template <typename T>
class Result {
 public:
  static Result Ok(T value) {
    return Result(std::variant<T, Error>(std::in_place_index<0>, std::move(value)));
  }

  static Result Err(Error error) {
    return Result(std::variant<T, Error>(std::in_place_index<1>, std::move(error)));
  }

  bool ok() const { return data_.index() == 0; }
  bool has_value() const { return ok(); }

  const T& value() const {
    if (!ok()) {
      throw std::logic_error("Result::value() called on an error Result");
    }
    return std::get<0>(data_);
  }

  T&& TakeValue() {
    if (!ok()) {
      throw std::logic_error("Result::TakeValue() called on an error Result");
    }
    return std::get<0>(std::move(data_));
  }

  const Error& error() const {
    if (ok()) {
      throw std::logic_error("Result::error() called on a value Result");
    }
    return std::get<1>(data_);
  }

 private:
  explicit Result(std::variant<T, Error> data) : data_(std::move(data)) {}

  std::variant<T, Error> data_;
};

// Result<void> specialization storing only the Error; Ok() is the kNone
// success sentinel.
template <>
class Result<void> {
 public:
  static Result Ok() { return Result(Error{ErrorCode::kNone, {}}); }

  static Result Err(Error error) { return Result(std::move(error)); }

  bool ok() const { return error_.code == ErrorCode::kNone; }
  bool has_value() const { return ok(); }

  void value() const {
    if (!ok()) {
      throw std::logic_error("Result<void>::value() called on an error Result");
    }
  }

  const Error& error() const {
    if (ok()) {
      throw std::logic_error("Result<void>::error() called on a value Result");
    }
    return error_;
  }

 private:
  explicit Result(Error error) : error_(std::move(error)) {}

  Error error_;
};

}  // namespace sms::common
