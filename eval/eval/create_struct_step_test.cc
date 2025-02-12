#include "eval/eval/create_struct_step.h"

#include "google/api/expr/v1alpha1/syntax.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/str_cat.h"
#include "eval/eval/container_backed_list_impl.h"
#include "eval/eval/container_backed_map_impl.h"
#include "eval/eval/ident_step.h"
#include "eval/testutil/test_message.pb.h"
#include "testutil/util.h"

namespace google {
namespace api {
namespace expr {
namespace runtime {
namespace {

using ::google::protobuf::Message;
using ::google::protobuf::Arena;

using testing::Eq;
using testing::Not;
using testing::IsNull;
using testing::Pointwise;

using testutil::EqualsProto;

using google::api::expr::v1alpha1::Expr;

// Helper method. Creates simple pipeline containing CreateStruct step that
// builds message and runs it.
cel_base::StatusOr<CelValue> RunExpression(absl::string_view field,
                                       const CelValue& value,
                                       google::protobuf::Arena* arena) {
  ExecutionPath path;

  Expr expr0;
  Expr expr1;

  auto ident = expr0.mutable_ident_expr();
  ident->set_name("message");
  auto step0_status = CreateIdentStep(ident, expr0.id());

  auto create_struct = expr1.mutable_struct_expr();
  create_struct->set_message_name("google.api.expr.runtime.TestMessage");

  auto entry = create_struct->add_entries();
  entry->set_field_key(field.data());

  if (!step0_status.ok()) {
    return step0_status.status();
  }

  auto step1_status = CreateCreateStructStep(create_struct, expr1.id());

  if (!step1_status.ok()) {
    return step1_status.status();
  }

  path.push_back(std::move(step0_status.ValueOrDie()));
  path.push_back(std::move(step1_status.ValueOrDie()));

  CelExpressionFlatImpl cel_expr(&expr1, std::move(path));
  Activation activation;
  activation.InsertValue("message", value);

  return cel_expr.Evaluate(activation, arena);
}

void RunExpressionAndGetMessage(absl::string_view field, const CelValue& value,
                                google::protobuf::Arena* arena, TestMessage* test_msg) {
  auto status = RunExpression(field, value, arena);
  ASSERT_TRUE(status.ok());

  CelValue result = status.ValueOrDie();
  ASSERT_TRUE(result.IsMessage());

  const Message* msg = result.MessageOrDie();
  ASSERT_THAT(msg, Not(IsNull()));

  ASSERT_EQ(msg->GetDescriptor(), TestMessage::descriptor());
  test_msg->MergeFrom(*msg);
}

void RunExpressionAndGetMessage(absl::string_view field,
                                std::vector<CelValue> values,
                                google::protobuf::Arena* arena, TestMessage* test_msg) {
  ContainerBackedListImpl cel_list(std::move(values));

  CelValue value = CelValue::CreateList(&cel_list);

  auto status = RunExpression(field, value, arena);
  ASSERT_TRUE(status.ok());

  CelValue result = status.ValueOrDie();
  ASSERT_TRUE(result.IsMessage());

  const Message* msg = result.MessageOrDie();
  ASSERT_THAT(msg, Not(IsNull()));

  ASSERT_EQ(msg->GetDescriptor(), TestMessage::descriptor());
  test_msg->MergeFrom(*msg);
}

// Helper method. Creates simple pipeline containing CreateStruct step that
// builds Map and runs it.
cel_base::StatusOr<CelValue> RunCreateMapExpression(
    const std::vector<std::pair<CelValue, CelValue>> values,
    google::protobuf::Arena* arena) {
  ExecutionPath path;
  Activation activation;

  Expr expr0;
  Expr expr1;

  std::vector<Expr> exprs;
  int index = 0;

  auto create_struct = expr1.mutable_struct_expr();
  for (const auto& item : values) {
    Expr expr;
    std::string key_name = absl::StrCat("key", index);
    std::string value_name = absl::StrCat("value", index);

    auto key_ident = expr.mutable_ident_expr();
    key_ident->set_name(key_name);
    exprs.push_back(expr);
    auto step_key_status = CreateIdentStep(key_ident, exprs.back().id());
    if (!step_key_status.ok()) {
      return step_key_status.status();
    }

    expr.Clear();
    auto value_ident = expr.mutable_ident_expr();
    value_ident->set_name(value_name);
    exprs.push_back(expr);
    auto step_value_status = CreateIdentStep(value_ident, exprs.back().id());
    if (!step_value_status.ok()) {
      return step_value_status.status();
    }

    path.push_back(std::move(step_key_status.ValueOrDie()));
    path.push_back(std::move(step_value_status.ValueOrDie()));

    activation.InsertValue(key_name, item.first);
    activation.InsertValue(value_name, item.second);

    create_struct->add_entries();
    index++;
  }

  auto step1_status = CreateCreateStructStep(create_struct, expr1.id());

  if (!step1_status.ok()) {
    return step1_status.status();
  }

  path.push_back(std::move(step1_status.ValueOrDie()));

  CelExpressionFlatImpl cel_expr(&expr1, std::move(path));
  return cel_expr.Evaluate(activation, arena);
}

TEST(CreateCreateStructStepTest, TestEmptyMessageCreation) {
  ExecutionPath path;

  Expr expr1;

  auto create_struct = expr1.mutable_struct_expr();
  create_struct->set_message_name("google.api.expr.runtime.TestMessage");

  auto step_status = CreateCreateStructStep(create_struct, expr1.id());

  ASSERT_TRUE(step_status.ok());

  path.push_back(std::move(step_status.ValueOrDie()));

  CelExpressionFlatImpl cel_expr(&expr1, std::move(path));
  Activation activation;

  google::protobuf::Arena arena;

  auto status = cel_expr.Evaluate(activation, &arena);
  ASSERT_TRUE(status.ok());

  CelValue result = status.ValueOrDie();
  ASSERT_TRUE(result.IsMessage());

  const Message* msg = result.MessageOrDie();
  ASSERT_THAT(msg, Not(IsNull()));

  ASSERT_EQ(msg->GetDescriptor(), TestMessage::descriptor());
}

// Test that fields of type bool are set correctly
TEST(CreateCreateStructStepTest, TestSetBoolField) {
  Arena arena;
  TestMessage test_msg;

  ASSERT_NO_FATAL_FAILURE(RunExpressionAndGetMessage(
      "bool_value", CelValue::CreateBool(true), &arena, &test_msg));
  ASSERT_EQ(test_msg.bool_value(), true);
}

// Test that fields of type int32_t are set correctly
TEST(CreateCreateStructStepTest, TestSetInt32Field) {
  Arena arena;
  TestMessage test_msg;

  ASSERT_NO_FATAL_FAILURE(RunExpressionAndGetMessage(
      "int32_value", CelValue::CreateInt64(1), &arena, &test_msg));

  ASSERT_EQ(test_msg.int32_value(), 1);
}

// Test that fields of type uint32_t are set correctly.
TEST(CreateCreateStructStepTest, TestSetUInt32Field) {
  Arena arena;
  TestMessage test_msg;

  ASSERT_NO_FATAL_FAILURE(RunExpressionAndGetMessage(
      "uint32_value", CelValue::CreateUint64(1), &arena, &test_msg));

  ASSERT_EQ(test_msg.uint32_value(), 1);
}

// Test that fields of type int64_t are set correctly.
TEST(CreateCreateStructStepTest, TestSetInt64Field) {
  Arena arena;
  TestMessage test_msg;

  ASSERT_NO_FATAL_FAILURE(RunExpressionAndGetMessage(
      "int64_value", CelValue::CreateInt64(1), &arena, &test_msg));

  EXPECT_EQ(test_msg.int64_value(), 1);
}

// Test that fields of type uint64_t are set correctly.
TEST(CreateCreateStructStepTest, TestSetUInt64Field) {
  Arena arena;
  TestMessage test_msg;

  ASSERT_NO_FATAL_FAILURE(RunExpressionAndGetMessage(
      "uint64_value", CelValue::CreateUint64(1), &arena, &test_msg));

  EXPECT_EQ(test_msg.uint64_value(), 1);
}

// Test that fields of type float are set correctly
TEST(CreateCreateStructStepTest, TestSetFloatField) {
  Arena arena;
  TestMessage test_msg;

  ASSERT_NO_FATAL_FAILURE(RunExpressionAndGetMessage(
      "float_value", CelValue::CreateDouble(2.0), &arena, &test_msg));

  EXPECT_DOUBLE_EQ(test_msg.float_value(), 2.0);
}

// Test that fields of type double are set correctly
TEST(CreateCreateStructStepTest, TestSetDoubleField) {
  Arena arena;
  TestMessage test_msg;

  ASSERT_NO_FATAL_FAILURE(RunExpressionAndGetMessage(
      "double_value", CelValue::CreateDouble(2.0), &arena, &test_msg));
  EXPECT_DOUBLE_EQ(test_msg.double_value(), 2.0);
}

// Test that fields of type string are set correctly.
TEST(CreateCreateStructStepTest, TestSetStringField) {
  const std::string kTestStr = "test";

  Arena arena;
  TestMessage test_msg;

  ASSERT_NO_FATAL_FAILURE(RunExpressionAndGetMessage(
      "string_value", CelValue::CreateString(&kTestStr), &arena, &test_msg));
  EXPECT_EQ(test_msg.string_value(), kTestStr);
}


// Test that fields of type bytes are set correctly.
TEST(CreateCreateStructStepTest, TestSetBytesField) {
  Arena arena;

  const std::string kTestStr = "test";
  TestMessage test_msg;

  ASSERT_NO_FATAL_FAILURE(RunExpressionAndGetMessage(
      "bytes_value", CelValue::CreateBytes(&kTestStr), &arena, &test_msg));
  EXPECT_EQ(test_msg.bytes_value(), kTestStr);
}

// Test that fields of type Message are set correctly.
TEST(CreateCreateStructStepTest, TestSetMessageField) {
  Arena arena;

  // Create payload message and set some fields.
  TestMessage orig_msg;
  orig_msg.set_bool_value(true);
  orig_msg.set_string_value("test");

  TestMessage test_msg;

  ASSERT_NO_FATAL_FAILURE(RunExpressionAndGetMessage(
      "message_value", CelValue::CreateMessage(&orig_msg, &arena), &arena,
      &test_msg));
  EXPECT_THAT(test_msg.message_value(), EqualsProto(orig_msg));
}

// Test that fields of type Message are set correctly.
TEST(CreateCreateStructStepTest, TestSetEnumField) {
  Arena arena;
  TestMessage test_msg;

  ASSERT_NO_FATAL_FAILURE(RunExpressionAndGetMessage(
      "enum_value", CelValue::CreateInt64(TestMessage::TEST_ENUM_2), &arena,
      &test_msg));
  EXPECT_EQ(test_msg.enum_value(), TestMessage::TEST_ENUM_2);
}

// Test that fields of type bool are set correctly
TEST(CreateCreateStructStepTest, TestSetRepeatedBoolField) {
  Arena arena;
  TestMessage test_msg;

  std::vector<bool> kValues = {true, false};
  std::vector<CelValue> values;
  for (auto value : kValues) {
    values.push_back(CelValue::CreateBool(value));
  }

  ASSERT_NO_FATAL_FAILURE(
      RunExpressionAndGetMessage("bool_list", values, &arena, &test_msg));
  ASSERT_THAT(test_msg.bool_list(), Pointwise(Eq(), kValues));
}

// Test that repeated fields of type int32_t are set correctly
TEST(CreateCreateStructStepTest, TestSetRepeatedInt32Field) {
  Arena arena;
  TestMessage test_msg;

  std::vector<int32_t> kValues = {23, 12};
  std::vector<CelValue> values;
  for (auto value : kValues) {
    values.push_back(CelValue::CreateInt64(value));
  }

  ASSERT_NO_FATAL_FAILURE(
      RunExpressionAndGetMessage("int32_list", values, &arena, &test_msg));
  ASSERT_THAT(test_msg.int32_list(), Pointwise(Eq(), kValues));
}

// Test that repeated fields of type uint32_t are set correctly
TEST(CreateCreateStructStepTest, TestSetRepeatedUInt32Field) {
  Arena arena;
  TestMessage test_msg;

  std::vector<uint32_t> kValues = {23, 12};
  std::vector<CelValue> values;
  for (auto value : kValues) {
    values.push_back(CelValue::CreateUint64(value));
  }

  ASSERT_NO_FATAL_FAILURE(
      RunExpressionAndGetMessage("uint32_list", values, &arena, &test_msg));
  ASSERT_THAT(test_msg.uint32_list(), Pointwise(Eq(), kValues));
}

// Test that repeated fields of type int64_t are set correctly
TEST(CreateCreateStructStepTest, TestSetRepeatedInt64Field) {
  Arena arena;
  TestMessage test_msg;

  std::vector<int64_t> kValues = {23, 12};
  std::vector<CelValue> values;
  for (auto value : kValues) {
    values.push_back(CelValue::CreateInt64(value));
  }

  ASSERT_NO_FATAL_FAILURE(
      RunExpressionAndGetMessage("int64_list", values, &arena, &test_msg));
  ASSERT_THAT(test_msg.int64_list(), Pointwise(Eq(), kValues));
}

// Test that repeated fields of type uint64_t are set correctly
TEST(CreateCreateStructStepTest, TestSetRepeatedUInt64Field) {
  Arena arena;
  TestMessage test_msg;

  std::vector<uint64_t> kValues = {23, 12};
  std::vector<CelValue> values;
  for (auto value : kValues) {
    values.push_back(CelValue::CreateUint64(value));
  }

  ASSERT_NO_FATAL_FAILURE(
      RunExpressionAndGetMessage("uint64_list", values, &arena, &test_msg));
  ASSERT_THAT(test_msg.uint64_list(), Pointwise(Eq(), kValues));
}

// Test that repeated fields of type float are set correctly
TEST(CreateCreateStructStepTest, TestSetRepeatedFloatField) {
  Arena arena;
  TestMessage test_msg;

  std::vector<float> kValues = {23, 12};
  std::vector<CelValue> values;
  for (auto value : kValues) {
    values.push_back(CelValue::CreateDouble(value));
  }

  ASSERT_NO_FATAL_FAILURE(
      RunExpressionAndGetMessage("float_list", values, &arena, &test_msg));
  ASSERT_THAT(test_msg.float_list(), Pointwise(Eq(), kValues));
}

// Test that repeated fields of type uint32_t are set correctly
TEST(CreateCreateStructStepTest, TestSetRepeatedDoubleField) {
  Arena arena;
  TestMessage test_msg;

  std::vector<double> kValues = {23, 12};
  std::vector<CelValue> values;
  for (auto value : kValues) {
    values.push_back(CelValue::CreateDouble(value));
  }

  ASSERT_NO_FATAL_FAILURE(
      RunExpressionAndGetMessage("double_list", values, &arena, &test_msg));
  ASSERT_THAT(test_msg.double_list(), Pointwise(Eq(), kValues));
}

// Test that repeated fields of type String are set correctly
TEST(CreateCreateStructStepTest, TestSetRepeatedStringField) {
  Arena arena;
  TestMessage test_msg;

  std::vector<std::string> kValues = {"test1", "test2"};
  std::vector<CelValue> values;
  for (const auto& value : kValues) {
    values.push_back(CelValue::CreateString(&value));
  }

  ASSERT_NO_FATAL_FAILURE(
      RunExpressionAndGetMessage("string_list", values, &arena, &test_msg));
  ASSERT_THAT(test_msg.string_list(), Pointwise(Eq(), kValues));
}

// Test that repeated fields of type String are set correctly
TEST(CreateCreateStructStepTest, TestSetRepeatedBytesField) {
  Arena arena;
  TestMessage test_msg;

  std::vector<std::string> kValues = {"test1", "test2"};
  std::vector<CelValue> values;
  for (const auto& value : kValues) {
    values.push_back(CelValue::CreateBytes(&value));
  }

  ASSERT_NO_FATAL_FAILURE(
      RunExpressionAndGetMessage("bytes_list", values, &arena, &test_msg));
  ASSERT_THAT(test_msg.bytes_list(), Pointwise(Eq(), kValues));
}


// Test that repeated fields of type Message are set correctly
TEST(CreateCreateStructStepTest, TestSetRepeatedMessageField) {
  Arena arena;
  TestMessage test_msg;

  std::vector<TestMessage> kValues(2);
  kValues[0].set_string_value("test1");
  kValues[1].set_string_value("test2");
  std::vector<CelValue> values;
  for (const auto& value : kValues) {
    values.push_back(CelValue::CreateMessage(&value, &arena));
  }

  ASSERT_NO_FATAL_FAILURE(
      RunExpressionAndGetMessage("message_list", values, &arena, &test_msg));
  ASSERT_THAT(test_msg.message_list()[0], EqualsProto(kValues[0]));
  ASSERT_THAT(test_msg.message_list()[1], EqualsProto(kValues[1]));
}


// Test that fields of type map<string, ...> are set correctly
TEST(CreateCreateStructStepTest, TestSetStringMapField) {
  Arena arena;
  TestMessage test_msg;

  std::vector<std::pair<CelValue, CelValue>> entries;

  const std::vector<std::string> kKeys = {"test2", "test1"};

  entries.push_back(
      {CelValue::CreateString(&kKeys[0]), CelValue::CreateInt64(2)});
  entries.push_back(
      {CelValue::CreateString(&kKeys[1]), CelValue::CreateInt64(1)});

  auto cel_map =
      CreateContainerBackedMap(absl::Span<std::pair<CelValue, CelValue>>(
          entries.data(), entries.size()));

  ASSERT_NO_FATAL_FAILURE(RunExpressionAndGetMessage(
      "string_int32_map", CelValue::CreateMap(cel_map.get()), &arena,
      &test_msg));

  ASSERT_EQ(test_msg.string_int32_map().size(), 2);
  ASSERT_EQ(test_msg.string_int32_map().at(kKeys[0]), 2);
  ASSERT_EQ(test_msg.string_int32_map().at(kKeys[1]), 1);
}

// Test that fields of type map<int64_t, ...> are set correctly
TEST(CreateCreateStructStepTest, TestSetInt64MapField) {
  Arena arena;
  TestMessage test_msg;

  std::vector<std::pair<CelValue, CelValue>> entries;

  const std::vector<int64_t> kKeys = {3, 4};

  entries.push_back(
      {CelValue::CreateInt64(kKeys[0]), CelValue::CreateInt64(1)});
  entries.push_back(
      {CelValue::CreateInt64(kKeys[1]), CelValue::CreateInt64(2)});

  auto cel_map =
      CreateContainerBackedMap(absl::Span<std::pair<CelValue, CelValue>>(
          entries.data(), entries.size()));

  ASSERT_NO_FATAL_FAILURE(RunExpressionAndGetMessage(
      "int64_int32_map", CelValue::CreateMap(cel_map.get()), &arena,
      &test_msg));

  ASSERT_EQ(test_msg.int64_int32_map().size(), 2);
  ASSERT_EQ(test_msg.int64_int32_map().at(kKeys[0]), 1);
  ASSERT_EQ(test_msg.int64_int32_map().at(kKeys[1]), 2);
}

// Test that fields of type map<uint64_t, ...> are set correctly
TEST(CreateCreateStructStepTest, TestSetUInt64MapField) {
  Arena arena;
  TestMessage test_msg;

  std::vector<std::pair<CelValue, CelValue>> entries;

  const std::vector<uint64_t> kKeys = {3, 4};

  entries.push_back(
      {CelValue::CreateUint64(kKeys[0]), CelValue::CreateInt64(1)});
  entries.push_back(
      {CelValue::CreateUint64(kKeys[1]), CelValue::CreateInt64(2)});

  auto cel_map =
      CreateContainerBackedMap(absl::Span<std::pair<CelValue, CelValue>>(
          entries.data(), entries.size()));

  ASSERT_NO_FATAL_FAILURE(RunExpressionAndGetMessage(
      "uint64_int32_map", CelValue::CreateMap(cel_map.get()), &arena,
      &test_msg));

  ASSERT_EQ(test_msg.uint64_int32_map().size(), 2);
  ASSERT_EQ(test_msg.uint64_int32_map().at(kKeys[0]), 1);
  ASSERT_EQ(test_msg.uint64_int32_map().at(kKeys[1]), 2);
}

// Test that Empty Map is created successfully.
TEST(CreateCreateStructStepTest, TestCreateEmptyMap) {
  Arena arena;
  auto status = RunCreateMapExpression({}, &arena);

  ASSERT_TRUE(status.ok());

  CelValue result_value = status.ValueOrDie();
  ASSERT_TRUE(result_value.IsMap());

  const CelMap* cel_map = result_value.MapOrDie();
  ASSERT_EQ(cel_map->size(), 0);
}

// Test that String Map is created successfully.
TEST(CreateCreateStructStepTest, TestCreateStringMap) {
  Arena arena;

  std::vector<std::pair<CelValue, CelValue>> entries;

  std::vector<std::string> kKeys = {"test2", "test1"};

  entries.push_back(
      {CelValue::CreateString(&kKeys[0]), CelValue::CreateInt64(2)});
  entries.push_back(
      {CelValue::CreateString(&kKeys[1]), CelValue::CreateInt64(1)});

  auto status = RunCreateMapExpression(entries, &arena);

  ASSERT_TRUE(status.ok());

  CelValue result_value = status.ValueOrDie();
  ASSERT_TRUE(result_value.IsMap());

  const CelMap* cel_map = result_value.MapOrDie();
  ASSERT_EQ(cel_map->size(), 2);

  auto lookup0 = (*cel_map)[CelValue::CreateString(&kKeys[0])];
  ASSERT_TRUE(lookup0.has_value());
  ASSERT_TRUE(lookup0.value().IsInt64());
  EXPECT_EQ(lookup0.value().Int64OrDie(), 2);

  auto lookup1 = (*cel_map)[CelValue::CreateString(&kKeys[1])];
  ASSERT_TRUE(lookup1.has_value());
  ASSERT_TRUE(lookup1.value().IsInt64());
  EXPECT_EQ(lookup1.value().Int64OrDie(), 1);
}

}  // namespace

}  // namespace runtime
}  // namespace expr
}  // namespace api
}  // namespace google
