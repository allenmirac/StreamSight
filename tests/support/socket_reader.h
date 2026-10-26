#pragma once

#include <sys/socket.h>
#include <unistd.h>

#include <cstdlib>
#include <string>

#include "net/BufferReader.h"

namespace streamsight::test {

// Feeds request bytes into a BufferReader through a socketpair.
// Intended for small protocol messages (< 4096 bytes per Feed call).
class SocketReader {
 public:
  SocketReader() {
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds_) != 0) std::abort();
  }

  ~SocketReader() {
    ::close(fds_[0]);
    ::close(fds_[1]);
  }

  SocketReader(const SocketReader&) = delete;
  SocketReader& operator=(const SocketReader&) = delete;

  streamsight::net::BufferReader& reader() { return reader_; }

  void Feed(const std::string& data) {
    size_t off = 0;
    while (off < data.size()) {
      ssize_t n = ::write(fds_[0], data.data() + off, data.size() - off);
      if (n <= 0) std::abort();
      off += static_cast<size_t>(n);
    }
    if (reader_.Read(fds_[1]) < 0) std::abort();
  }

 private:
  int fds_[2]{-1, -1};
  streamsight::net::BufferReader reader_;
};

}  // namespace streamsight::test
