#include <gtest/gtest.h>

#include <cmath>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "ai/FaceDatabase.h"
#include "support/test_temp_dir.h"

using streamsight::ai::FaceDatabase;
using streamsight::test::TempDir;

TEST(FaceDatabase, RegistersAndQueriesExactMatch) {
  TempDir dir;
  FaceDatabase db(dir.Path("faces.json"));

  db.Register("alice", {1.0f, 0.0f});
  EXPECT_EQ(db.Size(), 1u);

  const auto match = db.Query({1.0f, 0.0f});
  EXPECT_TRUE(match.matched);
  EXPECT_EQ(match.name, "alice");
  EXPECT_FLOAT_EQ(match.similarity, 1.0f);
}

TEST(FaceDatabase, MatchesExactlyAtThreshold) {
  TempDir dir;
  FaceDatabase db(dir.Path("faces.json"), 0.5f);
  db.Register("alice", {1.0f, 0.0f});

  const float y = std::sqrt(0.75f);
  const auto match = db.Query({0.5f, y});
  EXPECT_TRUE(match.matched);
  EXPECT_NEAR(match.similarity, 0.5f, 1e-6);
}

TEST(FaceDatabase, UnknownEmbeddingDoesNotMatch) {
  TempDir dir;
  FaceDatabase db(dir.Path("faces.json"));
  db.Register("alice", {1.0f, 0.0f});

  const auto match = db.Query({0.0f, 1.0f});
  EXPECT_FALSE(match.matched);
  EXPECT_EQ(match.name, "unknown");
}

TEST(FaceDatabase, RejectsDimensionMismatch) {
  TempDir dir;
  FaceDatabase db(dir.Path("faces.json"));
  db.Register("alice", {1.0f, 0.0f});

  const auto match = db.Query({1.0f});
  EXPECT_FALSE(match.matched);
  EXPECT_EQ(match.name, "unknown");
  EXPECT_FLOAT_EQ(match.similarity, -1.0f);
}

TEST(FaceDatabase, OverwritesDuplicateName) {
  TempDir dir;
  FaceDatabase db(dir.Path("faces.json"));

  db.Register("alice", {1.0f, 0.0f});
  db.Register("alice", {0.0f, 1.0f});

  EXPECT_EQ(db.Size(), 1u);
  EXPECT_TRUE(db.Query({0.0f, 1.0f}).matched);
  EXPECT_FALSE(db.Query({1.0f, 0.0f}).matched);
}

TEST(FaceDatabase, RemovesAndListsNames) {
  TempDir dir;
  FaceDatabase db(dir.Path("faces.json"));
  db.Register("alice", {1.0f, 0.0f});
  db.Register("bob", {0.0f, 1.0f});

  const auto names = db.ListNames();
  EXPECT_EQ(names.size(), 2u);
  EXPECT_TRUE(db.Remove("alice"));
  EXPECT_FALSE(db.Remove("alice"));
  EXPECT_EQ(db.Size(), 1u);
  EXPECT_EQ(db.ListNames().front(), "bob");
}

TEST(FaceDatabase, SavesAndLoads) {
  TempDir dir;
  const std::string path = dir.Path("faces.json");

  {
    FaceDatabase db(path);
    db.Register("alice", {1.0f, 0.0f});
    db.Register("bob", {0.0f, 1.0f});
    ASSERT_TRUE(db.Save());
  }

  FaceDatabase loaded(path);
  EXPECT_EQ(loaded.Load(), 2);
  EXPECT_EQ(loaded.Size(), 2u);
  EXPECT_TRUE(loaded.Query({1.0f, 0.0f}).matched);
  EXPECT_EQ(loaded.Query({1.0f, 0.0f}).name, "alice");
}

TEST(FaceDatabase, MissingFileLoadsZero) {
  TempDir dir;
  FaceDatabase db(dir.Path("missing.json"));
  EXPECT_EQ(db.Load(), 0);
  EXPECT_EQ(db.Size(), 0u);
}

TEST(FaceDatabase, CorruptFileDoesNotCrash) {
  TempDir dir;
  const std::string path = dir.Path("corrupt.json");
  {
    std::ofstream out(path);
    out << "{ this is not valid json";
  }

  FaceDatabase db(path);
  EXPECT_EQ(db.Load(), 0);
  EXPECT_EQ(db.Size(), 0u);
}

TEST(FaceDatabase, ConcurrentRegistrationIsSafe) {
  TempDir dir;
  FaceDatabase db(dir.Path("faces.json"));

  std::vector<std::thread> threads;
  for (int t = 0; t < 4; ++t) {
    threads.emplace_back([&db, t]() {
      for (int i = 0; i < 100; ++i) {
        db.Register("p" + std::to_string(t) + "_" + std::to_string(i),
                    {1.0f, 0.0f});
        // Query concurrently with registrations; the result is intentionally
        // not asserted here, only that the interleaving is crash-free.
        (void)db.Query({1.0f, 0.0f});
      }
    });
  }
  for (auto& thread : threads) thread.join();

  EXPECT_EQ(db.Size(), 400u);
  const auto match = db.Query({1.0f, 0.0f});
  EXPECT_TRUE(match.matched);
  EXPECT_EQ(match.name.rfind("p", 0), 0u);
}
