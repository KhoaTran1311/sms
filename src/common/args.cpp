#include "common/args.h"

#include <algorithm>

namespace sms::common {

Args ParseArgs(int argc, char** argv, std::vector<std::string_view> allowed) {
  Args args;
  auto is_allowed = [&allowed](std::string_view flag) {
    return std::find(allowed.begin(), allowed.end(), flag) != allowed.end();
  };

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (!arg.empty() && arg[0] == '-' && arg[1] == '-') {
      const std::string flag(arg.substr(2));
      if (flag == "help") {
        args.show_help = true;
        continue;
      }
      if (flag == "version") {
        args.version = true;
        continue;
      }
      if (!is_allowed(flag)) {
        args.error = true;
        args.error_message = "unknown flag: --" + flag;
        return args;
      }
      if (i + 1 >= argc) {
        args.error = true;
        args.error_message = "missing value for --" + flag;
        return args;
      }
      args.values[flag] = argv[++i];  // last wins
    } else {
      args.error = true;
      args.error_message = "unexpected positional argument: " + std::string(arg);
      return args;
    }
  }
  return args;
}

}  // namespace sms::common