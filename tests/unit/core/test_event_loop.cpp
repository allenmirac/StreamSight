#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "net/BufferReader.h"
#include "net/EventLoop.h"
#include "net/TcpConnection.h"
#include "net/TcpServer.h"
#include "support/free_port.h"

using streamsight::net::BufferReader;
using streamsight::net::EventLoop;
using streamsight::net::TcpConnection;
using streamsight::net::TcpServer;
using streamsight::test::FreeTcpPort;

namespace {

template <typename Predicate>
bool WaitFor(Predicate predicate,
             std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return predicate();
}

class EchoServer : public TcpServer {
 public:
  explicit EchoServer(EventLoop* loop) : TcpServer(loop) {}

  std::atomic<int> accepted{0};
  std::atomic<int> disconnected{0};

 protected:
  TcpConnection::Ptr OnConnect(SOCKET sockfd) override {
    auto conn = TcpServer::OnConnect(sockfd);
    ++accepted;
    conn->SetReadCallback(
        [](TcpConnection::Ptr connection, BufferReader& buffer) {
          std::string data(buffer.Peek(), buffer.ReadableBytes());
          buffer.RetrieveAll();
          connection->Send(data.data(),
                           static_cast<uint32_t>(data.size()));
          return true;
        });
    return conn;
  }

  // TcpServer installs its disconnect callback after OnConnect returns, and
  // SetDisconnectCallback is private to TcpServer's friend scope. Counting at
  // the virtual removal hook still proves the closed client was dispatched
  // through that callback and removed from the server.
  void RemoveConnection(SOCKET sockfd) override {
    ++disconnected;
    TcpServer::RemoveConnection(sockfd);
  }
};

int ConnectTo(uint16_t port) {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;

  timeval tv{2, 0};
  ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    ::close(fd);
    return -1;
  }
  return fd;
}

}  // namespace

TEST(EventLoop, TriggerEventRunsOnAnotherThread) {
  EventLoop loop(1);
  std::promise<std::thread::id> promise;
  auto future = promise.get_future();

  ASSERT_TRUE(loop.AddTriggerEvent(
      [&]() { promise.set_value(std::this_thread::get_id()); }));

  ASSERT_EQ(future.wait_for(std::chrono::seconds(2)),
            std::future_status::ready);
  EXPECT_NE(future.get(), std::this_thread::get_id());
}

TEST(EventLoop, TriggerEventsRunInSubmissionOrder) {
  EventLoop loop(1);
  std::mutex mutex;
  std::vector<int> order;
  std::promise<void> done;
  auto future = done.get_future();

  for (int i = 0; i < 10; ++i) {
    ASSERT_TRUE(loop.AddTriggerEvent([&, i]() {
      std::lock_guard<std::mutex> lock(mutex);
      order.push_back(i);
      if (order.size() == 10) done.set_value();
    }));
  }

  ASSERT_EQ(future.wait_for(std::chrono::seconds(2)),
            std::future_status::ready);
  EXPECT_EQ(order, (std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9}));
}

TEST(EventLoop, QuitWakesIdleLoop) {
  EventLoop loop(1);
  std::promise<void> done;
  auto future = done.get_future();

  std::thread helper([&]() {
    loop.Quit();
    done.set_value();
  });

  ASSERT_EQ(future.wait_for(std::chrono::seconds(2)),
            std::future_status::ready);
  helper.join();
}

TEST(EventLoop, TimerFires) {
  EventLoop loop(1);
  std::promise<void> done;
  auto future = done.get_future();

  loop.AddTimer(
      [&]() {
        done.set_value();
        return false;
      },
      10);

  ASSERT_EQ(future.wait_for(std::chrono::seconds(2)),
            std::future_status::ready);
}

TEST(TcpServer, EchoesDataAndStops) {
  EventLoop loop(1);
  EchoServer server(&loop);

  const uint16_t port = FreeTcpPort();
  ASSERT_NE(port, 0);
  ASSERT_TRUE(server.Start("127.0.0.1", port));

  const int fd = ConnectTo(port);
  ASSERT_GE(fd, 0);
  ASSERT_TRUE(WaitFor([&]() { return server.accepted.load() == 1; }));

  const std::string message = "ping";
  ASSERT_EQ(::send(fd, message.data(), message.size(), 0),
            static_cast<ssize_t>(message.size()));

  std::string received;
  ASSERT_TRUE(WaitFor([&]() {
    char chunk[16] = {0};
    const ssize_t n = ::recv(fd, chunk, sizeof(chunk), MSG_DONTWAIT);
    if (n > 0) received.append(chunk, static_cast<size_t>(n));
    return received.size() >= message.size();
  }));
  EXPECT_EQ(received, message);

  ::close(fd);
  EXPECT_TRUE(WaitFor([&]() { return server.disconnected.load() == 1; }));
  server.Stop();
}
