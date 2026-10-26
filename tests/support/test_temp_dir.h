#pragma once

#include <unistd.h>

#include <atomic>
#include <filesystem>
#include <string>
#include <system_error>

namespace streamsight::test {

class TempDir {
 public:
  TempDir() {
    static std::atomic<int> counter{0};
    path_ = std::filesystem::temp_directory_path() /
            ("streamsight_test_" + std::to_string(::getpid()) + "_" +
             std::to_string(counter.fetch_add(1)));
    std::filesystem::create_directories(path_);
  }

  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }

  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;

  std::string Path(const std::string& name) const {
    return (path_ / name).string();
  }

  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

}  // namespace streamsight::test
