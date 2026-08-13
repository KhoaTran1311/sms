#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace sms::common {

struct Args {
  std::map<std::string, std::string> values;  // "--flag" → value (last wins)
  bool show_help = false;
  bool version = false;
  bool error = false;
  std::string error_message;
};

// Flags declared via `allowed` (without the leading "--"); unknown "--x" → error.
Args ParseArgs(int argc, char** argv, std::vector<std::string_view> allowed);

}  // namespace sms::common