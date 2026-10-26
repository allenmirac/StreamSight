#include <gtest/gtest.h>

#include <string>

#include "api/JsonUtil.h"

using streamsight::api::EscJson;
using streamsight::api::JsonGetBool;
using streamsight::api::JsonGetInt;
using streamsight::api::JsonGetString;
using streamsight::api::JsonGetValue;

TEST(JsonUtil, ExtractsStringValue) {
  EXPECT_EQ(JsonGetString(R"({"name":"Alice"})", "name"), "Alice");
}

TEST(JsonUtil, HandlesWhitespace) {
  EXPECT_EQ(JsonGetString(R"( { "name" :  "Alice" } )", "name"), "Alice");
}

TEST(JsonUtil, MissingKeyReturnsEmpty) {
  EXPECT_EQ(JsonGetString(R"({"name":"Alice"})", "age"), "");
}

TEST(JsonUtil, NestedKeyDoesNotShadowTopLevel) {
  EXPECT_EQ(
      JsonGetString(R"({"nested":{"name":"inner"},"name":"outer"})", "name"),
      "outer");
}

TEST(JsonUtil, KeyOnlyInNestedObjectIsMissing) {
  EXPECT_EQ(JsonGetString(R"({"nested":{"name":"inner"}})", "name"), "");
}

TEST(JsonUtil, ExtractsNestedObjectAndArray) {
  EXPECT_EQ(JsonGetValue(R"({"fx":{"a":1},"ids":[1,2,3]})", "fx"),
            R"({"a":1})");
  EXPECT_EQ(JsonGetValue(R"({"fx":{"a":1},"ids":[1,2,3]})", "ids"), "[1,2,3]");
}

TEST(JsonUtil, PreservesEscapedQuotesInStrings) {
  EXPECT_EQ(JsonGetString(R"({"name":"Ali\"ce"})", "name"), R"(Ali\"ce)");
}

TEST(JsonUtil, ParsesIntegersWithDefault) {
  EXPECT_EQ(JsonGetInt(R"({"fps":25})", "fps", 5), 25);
  EXPECT_EQ(JsonGetInt(R"({"fps":-3})", "fps", 5), -3);
  EXPECT_EQ(JsonGetInt(R"({"fps":"fast"})", "fps", 5), 5);
  EXPECT_EQ(JsonGetInt(R"({"fps":25})", "missing", 5), 5);
}

TEST(JsonUtil, ParsesBooleansWithDefault) {
  EXPECT_TRUE(JsonGetBool(R"({"on":true})", "on", false));
  EXPECT_FALSE(JsonGetBool(R"({"on":false})", "on", true));
  EXPECT_FALSE(JsonGetBool(R"({"on":TRUE})", "on", false));
  EXPECT_FALSE(JsonGetBool(R"({"on":"true"})", "on", false));
}

TEST(JsonUtil, MalformedInputReturnsDefaults) {
  EXPECT_EQ(JsonGetString("not json", "name"), "");
  EXPECT_EQ(JsonGetInt(R"({"fps" 25})", "fps", 7), 7);
  EXPECT_EQ(JsonGetValue("{", "name"), "");
}

TEST(JsonUtil, EscapesJsonSpecialCharacters) {
  EXPECT_EQ(EscJson("a\"b\\c\n"), "a\\\"b\\\\c\\n");
}
