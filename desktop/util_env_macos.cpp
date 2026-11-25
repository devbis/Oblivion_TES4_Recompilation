#include "util_env.h"

#include <array>
#include <filesystem>
#include <mach-o/dyld.h>
#include <pthread.h>
#include <limits.h>
#include <stdlib.h>

namespace dxvk::env {

  std::string getExePath() {
    std::array<char, PATH_MAX> path = {};
    uint32_t size = path.size();

    if (_NSGetExecutablePath(path.data(), &size) != 0) {
      std::vector<char> longPath(size);
      if (_NSGetExecutablePath(longPath.data(), &size) != 0)
        return {};
      char* resolved = realpath(longPath.data(), nullptr);
      if (!resolved)
        return longPath.data();
      std::string result(resolved);
      free(resolved);
      return result;
    }

    char* resolved = realpath(path.data(), nullptr);
    if (!resolved)
      return path.data();
    std::string result(resolved);
    free(resolved);
    return result;
  }

  void setThreadName(const std::string& name) {
    pthread_setname_np(name.c_str());
  }

  bool createDirectory(const std::string& path) {
    return std::filesystem::create_directories(path);
  }

}
