#include <gtest/gtest.h>
#include <sys/socket.h>
#include <unistd.h>

#include <csignal>
#include <cstdint>
#include <string>

#include "net/BufferReader.h"
#include "net/BufferWriter.h"
#include "support/socket_reader.h"

using streamsight::net::BufferReader;
using streamsight::net::BufferWriter;
using streamsight::net::ReadUint16BE;
using streamsight::net::ReadUint16LE;
using streamsight::net::ReadUint24BE;
using streamsight::net::ReadUint24LE;
using streamsight::net::ReadUint32BE;
using streamsight::net::ReadUint32LE;
using streamsight::net::WriteUint16BE;
using streamsight::net::WriteUint16LE;
using streamsight::net::WriteUint24BE;
using streamsight::net::WriteUint24LE;
using streamsight::net::WriteUint32BE;
using streamsight::net::WriteUint32LE;
using streamsight::test::SocketReader;

TEST(BufferByteOrder, RoundTripsAllWidths) {
  char buf[8] = {0};

  WriteUint32BE(buf, 0x01020304u);
  EXPECT_EQ(ReadUint32BE(buf), 0x01020304u);
  WriteUint32LE(buf, 0x01020304u);
  EXPECT_EQ(ReadUint32LE(buf), 0x01020304u);

  WriteUint24BE(buf, 0x010203u);
  EXPECT_EQ(ReadUint24BE(buf), 0x010203u);
  WriteUint24LE(buf, 0x010203u);
  EXPECT_EQ(ReadUint24LE(buf), 0x010203u);

  WriteUint16BE(buf, 0x0102u);
  EXPECT_EQ(ReadUint16BE(buf), 0x0102u);
  WriteUint16LE(buf, 0x0102u);
  EXPECT_EQ(ReadUint16LE(buf), 0x0102u);
}

TEST(BufferReader, FindsCrlfPositions) {
  SocketReader sr;
  sr.Feed("line1\r\nline2\r\n\r\n");
  BufferReader& reader = sr.reader();

  const char* first = reader.FindFirstCrlf();
  ASSERT_NE(first, nullptr);
  EXPECT_EQ(first - reader.Peek(), 5);

  const char* last = reader.FindLastCrlf();
  ASSERT_NE(last, nullptr);
  EXPECT_EQ(last - reader.Peek(), 14);

  const char* last_double = reader.FindLastCrlfCrlf();
  ASSERT_NE(last_double, nullptr);
  EXPECT_EQ(last_double - reader.Peek(), 12);
}

TEST(BufferReader, RetrieveUntilAndReadAll) {
  SocketReader sr;
  sr.Feed("line1\r\nline2\r\n\r\n");
  BufferReader& reader = sr.reader();

  const char* first = reader.FindFirstCrlf();
  ASSERT_NE(first, nullptr);
  reader.RetrieveUntil(first + 2);
  EXPECT_EQ(reader.ReadableBytes(), 9u);

  std::string rest;
  EXPECT_EQ(reader.ReadAll(rest), 9u);
  EXPECT_EQ(rest, "line2\r\n\r\n");
  EXPECT_EQ(reader.ReadableBytes(), 0u);
}

TEST(BufferReader, GrowsBufferWhenPayloadExceedsInitialCapacity) {
  SocketReader sr;
  BufferReader& reader = sr.reader();
  const uint32_t initial_size = reader.Size();
  ASSERT_EQ(reader.ReadableBytes(), 0u);

  // Two 1500-byte feeds total 3000 bytes, more than the initial buffer.
  // BufferReader::Read must resize so the full payload remains readable.
  std::string half;
  half.reserve(1500);
  for (int i = 0; i < 1500; ++i) {
    half.push_back(static_cast<char>('a' + i % 26));
  }
  sr.Feed(half);
  sr.Feed(half);

  EXPECT_GT(reader.Size(), initial_size);
  EXPECT_EQ(reader.ReadableBytes(), 3000u);

  std::string all;
  EXPECT_EQ(reader.ReadAll(all), 3000u);
  EXPECT_EQ(all, half + half);
  EXPECT_EQ(reader.ReadableBytes(), 0u);
}

TEST(BufferReader, RetrieveBeyondReadableClearsBuffer) {
  SocketReader sr;
  sr.Feed("abc");
  BufferReader& reader = sr.reader();

  reader.Retrieve(100);
  EXPECT_EQ(reader.ReadableBytes(), 0u);
}

TEST(BufferReader, ReadUntilCrlf) {
  SocketReader sr;
  sr.Feed("abc\r\nrest");
  BufferReader& reader = sr.reader();

  std::string line;
  EXPECT_EQ(reader.ReadUntilCrlf(line), 5u);
  EXPECT_EQ(line, "abc\r\n");

  std::string rest;
  EXPECT_EQ(reader.ReadAll(rest), 4u);
  EXPECT_EQ(rest, "rest");
}

TEST(BufferWriter, AppendRespectsCapacity) {
  BufferWriter writer(2);
  EXPECT_TRUE(writer.IsEmpty());

  EXPECT_TRUE(writer.Append("a", 1));
  EXPECT_EQ(writer.Size(), 1u);
  EXPECT_TRUE(writer.Append("b", 1));
  EXPECT_EQ(writer.Size(), 2u);
  EXPECT_TRUE(writer.IsFull());
  EXPECT_FALSE(writer.Append("c", 1));
}

TEST(BufferWriter, AppendRejectsInvalidIndex) {
  BufferWriter writer(4);
  EXPECT_FALSE(writer.Append("abc", 3, 3));
  EXPECT_FALSE(writer.Append("abc", 3, 4));
  EXPECT_TRUE(writer.IsEmpty());
}

TEST(BufferWriter, SendsAllQueuedBytes) {
  int fds[2] = {-1, -1};
  ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

  BufferWriter writer(4);
  ASSERT_TRUE(writer.Append("hello", 5));
  // Send() returns the last ::send() result, but after the final packet is
  // popped the loop runs once more and the empty-queue branch returns 0.
  // Verify the observable contract below: the queue drains and bytes arrive.
  EXPECT_EQ(writer.Send(fds[0]), 0);
  EXPECT_TRUE(writer.IsEmpty());

  char buf[16] = {0};
  EXPECT_EQ(::recv(fds[1], buf, sizeof(buf), 0), 5);
  EXPECT_EQ(std::string(buf, 5), "hello");

  ::close(fds[0]);
  ::close(fds[1]);
}

TEST(BufferWriter, DestroysCopiedPayloadWithArrayDelete) {
  // Regression for an alloc/dealloc mismatch: Append(const char*) owns a
  // new[] buffer, so dropping the queued packet must use delete[].
  // ASan reports this on destruction.
  {
    BufferWriter writer(2);
    ASSERT_TRUE(writer.Append("payload", 7));
    ASSERT_TRUE(writer.Append("another", 7));
    EXPECT_TRUE(writer.IsFull());
  }

  {
    BufferWriter writer(2);
    ASSERT_TRUE(writer.Append("payload", 7));
  }
}

TEST(BufferWriter, FailedSendKeepsPacketQueued) {
  std::signal(SIGPIPE, SIG_IGN);

  int fds[2] = {-1, -1};
  ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  ::close(fds[1]);

  BufferWriter writer(4);
  ASSERT_TRUE(writer.Append("x", 1));
  EXPECT_LT(writer.Send(fds[0], 100), 0);
  EXPECT_FALSE(writer.IsEmpty());

  ::close(fds[0]);
}
