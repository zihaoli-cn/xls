// Copyright 2020 The XLS Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "xls/scheduling/pipeline_schedule.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "xls/common/status/matchers.h"
#include "xls/delay_model/analyze_critical_path.h"
#include "xls/delay_model/delay_estimator.h"
#include "xls/delay_model/delay_estimators.h"
#include "xls/examples/sample_packages.h"
#include "xls/ir/bits.h"
#include "xls/ir/function_builder.h"
#include "xls/ir/ir_matcher.h"
#include "xls/ir/ir_test_base.h"
#include "xls/ir/node.h"
#include "xls/ir/op.h"
#include "xls/ir/type.h"

namespace m = ::xls::op_matchers;

namespace xls {
namespace {

using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::UnorderedElementsAre;
using xls::status_testing::StatusIs;

class TestDelayEstimator : public DelayEstimator {
 public:
  TestDelayEstimator() : DelayEstimator("test") {}

  absl::StatusOr<int64_t> GetOperationDelayInPs(Node* node) const override {
    switch (node->op()) {
      case Op::kAfterAll:
      case Op::kBitSlice:
      case Op::kConcat:
      case Op::kLiteral:
      case Op::kParam:
      case Op::kReceive:
      case Op::kSend:
      case Op::kTupleIndex:
        return 0;
      case Op::kUDiv:
      case Op::kSDiv:
        return 2;
      default:
        return 1;
    }
  }
};

class PipelineScheduleTest : public IrTestBase {};

TEST_F(PipelineScheduleTest, SelectsEntry) {
  auto p = CreatePackage();
  FunctionBuilder fb(TestName(), p.get());
  fb.Param("x", p->GetBitsType(32));
  XLS_ASSERT_OK_AND_ASSIGN(Function * f, fb.Build());

  SchedulingOptions options(SchedulingStrategy::ASAP);
  options.clock_period_ps(2);

  XLS_ASSERT_OK_AND_ASSIGN(
      PipelineSchedule schedule,
      PipelineSchedule::Run(f, TestDelayEstimator(), options));
  EXPECT_EQ(schedule.length(), 1);
  EXPECT_THAT(schedule.nodes_in_cycle(0), UnorderedElementsAre(m::Param()));

  FunctionBuilder fb_2("other_fn", p.get());
  fb_2.Literal(Value(UBits(16, 16)));
  XLS_ASSERT_OK_AND_ASSIGN(Function * f_2, fb_2.Build());

  XLS_ASSERT_OK_AND_ASSIGN(
      schedule, PipelineSchedule::Run(f_2, TestDelayEstimator(), options));
  EXPECT_EQ(schedule.length(), 1);
  EXPECT_THAT(schedule.nodes_in_cycle(0), UnorderedElementsAre(m::Literal()));
}

TEST_F(PipelineScheduleTest, AsapScheduleTrivial) {
  auto p = CreatePackage();
  FunctionBuilder fb(TestName(), p.get());
  fb.Param("x", p->GetBitsType(32));
  XLS_ASSERT_OK_AND_ASSIGN(Function * f, fb.Build());

  XLS_ASSERT_OK_AND_ASSIGN(
      PipelineSchedule schedule,
      PipelineSchedule::Run(
          f, TestDelayEstimator(),
          SchedulingOptions(SchedulingStrategy::ASAP).clock_period_ps(2)));

  EXPECT_EQ(schedule.length(), 1);
  EXPECT_THAT(schedule.nodes_in_cycle(0), UnorderedElementsAre(m::Param()));
}

TEST_F(PipelineScheduleTest, OutrightInfeasibleSchedule) {
  // Create a schedule in which the critical path doesn't even fit in the
  // requested clock_period * stages.
  auto p = CreatePackage();
  FunctionBuilder fb(TestName(), p.get());
  BValue x = fb.Param("x", p->GetBitsType(32));
  fb.Not(fb.Not(fb.Not(fb.Not(x))));
  XLS_ASSERT_OK_AND_ASSIGN(Function * f, fb.Build());

  ASSERT_THAT(
      PipelineSchedule::Run(
          f, TestDelayEstimator(),
          SchedulingOptions(SchedulingStrategy::MINIMIZE_REGISTERS)
              .clock_period_ps(1)
              .pipeline_stages(2))
          .status(),
      StatusIs(
          absl::StatusCode::kResourceExhausted,
          HasSubstr(
              "Cannot be scheduled in 2 stages. Computed lower bound is 4.")));
}

TEST_F(PipelineScheduleTest, InfeasiableScheduleWithBinPacking) {
  // Create a schedule in which the critical path fits in the requested
  // clock_period * stages, but there is no way to bin pack the instructions
  // into the stages such that the schedule is met.
  auto p = CreatePackage();
  FunctionBuilder fb(TestName(), p.get());
  BValue x = fb.Param("x", p->GetBitsType(32));
  fb.Not(fb.UDiv(fb.Not(x), fb.Not(x)));
  XLS_ASSERT_OK_AND_ASSIGN(Function * f, fb.Build());

  ASSERT_THAT(
      PipelineSchedule::Run(
          f, TestDelayEstimator(),
          SchedulingOptions(SchedulingStrategy::MINIMIZE_REGISTERS)
              .clock_period_ps(2)
              .pipeline_stages(2))
          .status(),
      StatusIs(
          absl::StatusCode::kResourceExhausted,
          HasSubstr(
              "Cannot be scheduled in 2 stages. Computed lower bound is 3.")));
}

TEST_F(PipelineScheduleTest, InfeasiableScheduleWithReturnValueUsers) {
  // Create function which has users of the return value node such that the
  // return value cannot be scheduled in the final cycle.
  auto p = CreatePackage();
  FunctionBuilder fb(TestName(), p.get());
  BValue x = fb.Param("x", p->GetBitsType(32));
  BValue ret_value = fb.Not(x, absl::nullopt, "ret_value");
  fb.Negate(ret_value);
  XLS_ASSERT_OK_AND_ASSIGN(Function * f, fb.BuildWithReturnValue(ret_value));

  ASSERT_THAT(
      PipelineSchedule::Run(
          f, TestDelayEstimator(),
          SchedulingOptions(SchedulingStrategy::MINIMIZE_REGISTERS)
              .clock_period_ps(1)
              .pipeline_stages(2))
          .status(),
      StatusIs(
          absl::StatusCode::kResourceExhausted,
          HasSubstr(
              "the following node(s) must be scheduled in the final cycle but "
              "that is impossible due to users of these node(s): ret_value")));
}

TEST_F(PipelineScheduleTest, AsapScheduleNoParameters) {
  auto p = CreatePackage();
  FunctionBuilder fb(TestName(), p.get());
  fb.Negate(fb.Add(fb.Literal(UBits(42, 8)), fb.Literal(UBits(100, 8))));
  XLS_ASSERT_OK_AND_ASSIGN(Function * f, fb.Build());

  XLS_ASSERT_OK_AND_ASSIGN(
      PipelineSchedule schedule,
      PipelineSchedule::Run(
          f, TestDelayEstimator(),
          SchedulingOptions(SchedulingStrategy::ASAP).clock_period_ps(1)));

  EXPECT_EQ(schedule.length(), 2);
  EXPECT_THAT(schedule.nodes_in_cycle(0),
              UnorderedElementsAre(m::Add(), m::Literal(42), m::Literal(100)));
  EXPECT_THAT(schedule.nodes_in_cycle(1), UnorderedElementsAre(m::Neg()));
}

TEST_F(PipelineScheduleTest, AsapScheduleIncrementChain) {
  auto p = CreatePackage();
  FunctionBuilder fb(TestName(), p.get());
  auto x = fb.Param("x", p->GetBitsType(32));
  fb.Add(fb.Add(fb.Add(x, fb.Literal(UBits(1, 32))), fb.Literal(UBits(1, 32))),
         fb.Literal(UBits(1, 32)));
  XLS_ASSERT_OK_AND_ASSIGN(Function * f, fb.Build());

  XLS_ASSERT_OK_AND_ASSIGN(
      PipelineSchedule schedule,
      PipelineSchedule::Run(
          f, TestDelayEstimator(),
          SchedulingOptions(SchedulingStrategy::ASAP).clock_period_ps(1)));

  EXPECT_EQ(schedule.length(), 3);
  EXPECT_THAT(schedule.nodes_in_cycle(0),
              UnorderedElementsAre(m::Param("x"), m::Add(), m::Literal(1),
                                   m::Literal(1), m::Literal(1)));
  EXPECT_THAT(schedule.nodes_in_cycle(1), UnorderedElementsAre(m::Add()));
  EXPECT_THAT(schedule.nodes_in_cycle(2), UnorderedElementsAre(m::Add()));
}

TEST_F(PipelineScheduleTest, MinimizeRegisterBitslices) {
  // When minimizing registers, bit-slices should be hoisted in the schedule if
  // their operand is not otherwise live, and sunk in the schedule if their
  // operand *is* otherwise live.
  auto p = CreatePackage();
  FunctionBuilder fb(TestName(), p.get());
  auto x = fb.Param("x", p->GetBitsType(32));
  auto y = fb.Param("y", p->GetBitsType(32));
  auto x_slice = fb.BitSlice(x, /*start=*/8, /*width=*/8);
  auto y_slice = fb.BitSlice(y, /*start=*/8, /*width=*/8);
  auto neg_neg_y = fb.Negate(fb.Negate(y));
  // 'x' is live throughout the function, 'y' is not.
  fb.Concat({x, x_slice, y_slice, neg_neg_y});

  XLS_ASSERT_OK_AND_ASSIGN(Function * f, fb.Build());

  XLS_ASSERT_OK_AND_ASSIGN(
      PipelineSchedule schedule,
      PipelineSchedule::Run(f, TestDelayEstimator(),
                            SchedulingOptions().clock_period_ps(1)));

  EXPECT_EQ(schedule.length(), 2);
  EXPECT_THAT(schedule.nodes_in_cycle(0),
              UnorderedElementsAre(m::Param("x"), m::Param("y"),
                                   m::BitSlice(m::Param("y")), m::Neg()));
  EXPECT_THAT(
      schedule.nodes_in_cycle(1),
      UnorderedElementsAre(m::BitSlice(m::Param("x")), m::Neg(), m::Concat()));
}

TEST_F(PipelineScheduleTest, AsapScheduleComplex) {
  auto p = CreatePackage();
  FunctionBuilder fb(TestName(), p.get());
  Type* u32 = p->GetBitsType(32);
  auto x = fb.Param("x", u32);
  auto y = fb.Param("y", u32);
  auto z = fb.Param("z", u32);
  fb.Negate(fb.Concat({(fb.Not(x | y) - z) * x, z + z}));

  XLS_ASSERT_OK_AND_ASSIGN(Function * f, fb.Build());

  XLS_ASSERT_OK_AND_ASSIGN(
      PipelineSchedule schedule,
      PipelineSchedule::Run(
          f, TestDelayEstimator(),
          SchedulingOptions(SchedulingStrategy::ASAP).clock_period_ps(2)));

  EXPECT_EQ(schedule.length(), 3);
  EXPECT_THAT(schedule.nodes_in_cycle(0),
              UnorderedElementsAre(m::Param("x"), m::Param("y"), m::Param("z"),
                                   m::Or(), m::Not(), m::Add()));
  EXPECT_THAT(schedule.nodes_in_cycle(1),
              UnorderedElementsAre(m::Concat(), m::Sub(), m::UMul()));
  EXPECT_THAT(schedule.nodes_in_cycle(2), UnorderedElementsAre(m::Neg()));
}

TEST_F(PipelineScheduleTest, JustClockPeriodGiven) {
  auto p = CreatePackage();
  FunctionBuilder fb(TestName(), p.get());
  Type* u32 = p->GetBitsType(32);
  auto x = fb.Param("x", u32);
  auto y = fb.Param("y", u32);
  auto z = fb.Param("z", u32);
  fb.Negate(fb.Concat({(fb.Not(x | y) - z) * x, z + z}));

  XLS_ASSERT_OK_AND_ASSIGN(Function * func, fb.Build());

  XLS_ASSERT_OK_AND_ASSIGN(
      PipelineSchedule schedule,
      PipelineSchedule::Run(func, TestDelayEstimator(),
                            SchedulingOptions().clock_period_ps(2)));

  // Returns the unique scheduled Ops in the given cycle.
  auto scheduled_ops = [&](int64_t cycle) {
    absl::flat_hash_set<Op> ops;
    for (const auto& node : schedule.nodes_in_cycle(cycle)) {
      ops.insert(node->op());
    }
    return ops;
  };

  EXPECT_EQ(schedule.length(), 3);
  EXPECT_THAT(scheduled_ops(0),
              UnorderedElementsAre(Op::kParam, Op::kOr, Op::kNot));
  EXPECT_THAT(scheduled_ops(1), UnorderedElementsAre(Op::kUMul, Op::kSub));
  EXPECT_THAT(scheduled_ops(2),
              UnorderedElementsAre(Op::kAdd, Op::kNeg, Op::kConcat));
  EXPECT_THAT(scheduled_ops(3), UnorderedElementsAre());
}

TEST_F(PipelineScheduleTest, TestVerifyTiming) {
  auto p = CreatePackage();
  FunctionBuilder fb(TestName(), p.get());
  Type* u32 = p->GetBitsType(32);
  auto x = fb.Param("x", u32);
  auto y = fb.Param("y", u32);
  auto x_plus_y = fb.Add(x, y);
  fb.Subtract(x_plus_y, fb.Negate(x_plus_y));

  XLS_ASSERT_OK_AND_ASSIGN(Function * func, fb.Build());

  XLS_ASSERT_OK_AND_ASSIGN(
      PipelineSchedule schedule,
      PipelineSchedule::Run(func, TestDelayEstimator(),
                            SchedulingOptions().clock_period_ps(5)));

  EXPECT_EQ(schedule.length(), 1);
  XLS_EXPECT_OK(
      schedule.VerifyTiming(/*clock_period_ps=*/5, TestDelayEstimator()));
  EXPECT_THAT(
      schedule.VerifyTiming(/*clock_period_ps=*/1, TestDelayEstimator()),
      status_testing::StatusIs(
          absl::StatusCode::kInternal,
          ::testing::HasSubstr(
              "Schedule does not meet timing (1ps). Longest failing path "
              "(3ps): add.3 (1ps) -> neg.4 (1ps) -> sub.5 (1ps)")));
}

TEST_F(PipelineScheduleTest, ClockPeriodAndPipelineLengthGiven) {
  auto p = CreatePackage();
  FunctionBuilder fb(TestName(), p.get());
  Type* u32 = p->GetBitsType(32);
  auto x = fb.Param("x", u32);
  auto y = fb.Param("y", u32);
  auto z = fb.Param("z", u32);
  fb.Negate(fb.Concat({(fb.Not(fb.Negate(x | y)) - z) * x, z + z}));

  XLS_ASSERT_OK_AND_ASSIGN(Function * func, fb.Build());

  XLS_ASSERT_OK_AND_ASSIGN(
      PipelineSchedule schedule,
      PipelineSchedule::Run(
          func, TestDelayEstimator(),
          SchedulingOptions().clock_period_ps(2).pipeline_stages(4)));

  // Returns the unique scheduled Ops in the given cycle.
  auto scheduled_ops = [&](int64_t cycle) {
    absl::flat_hash_set<Op> ops;
    for (const auto& node : schedule.nodes_in_cycle(cycle)) {
      ops.insert(node->op());
    }
    return ops;
  };

  EXPECT_EQ(schedule.length(), 4);
  EXPECT_THAT(scheduled_ops(0),
              UnorderedElementsAre(Op::kParam, Op::kOr, Op::kNeg));
  EXPECT_THAT(scheduled_ops(1), UnorderedElementsAre(Op::kNot, Op::kSub));
  EXPECT_THAT(scheduled_ops(2), UnorderedElementsAre(Op::kUMul));
  EXPECT_THAT(scheduled_ops(3),
              UnorderedElementsAre(Op::kConcat, Op::kNeg, Op::kAdd));
}

TEST_F(PipelineScheduleTest, JustPipelineLengthGiven) {
  auto p = CreatePackage();
  FunctionBuilder fb(TestName(), p.get());
  Type* u32 = p->GetBitsType(32);
  auto x = fb.Param("x", u32);
  auto y = fb.Param("y", u32);
  auto z = fb.Param("z", u32);
  fb.Negate(fb.Concat({(fb.Not(fb.Negate(x | y)) - z) * x, z + z}));

  XLS_ASSERT_OK_AND_ASSIGN(Function * func, fb.Build());

  XLS_ASSERT_OK_AND_ASSIGN(
      PipelineSchedule schedule,
      PipelineSchedule::Run(func, TestDelayEstimator(),
                            SchedulingOptions().pipeline_stages(6)));

  // Returns the unique scheduled Ops in the given cycle.
  auto scheduled_ops = [&](int64_t cycle) {
    absl::flat_hash_set<Op> ops;
    for (const auto& node : schedule.nodes_in_cycle(cycle)) {
      ops.insert(node->op());
    }
    return ops;
  };

  EXPECT_EQ(schedule.length(), 6);

  // The maximum delay of any stage should be minimum feasible value. In this
  // case it is 1ps, which means there should be no dependent instructions in
  // single cycle.
  EXPECT_THAT(scheduled_ops(0), UnorderedElementsAre(Op::kParam, Op::kOr));
  EXPECT_THAT(scheduled_ops(1), UnorderedElementsAre(Op::kNeg));
  EXPECT_THAT(scheduled_ops(2), UnorderedElementsAre(Op::kNot));
  EXPECT_THAT(scheduled_ops(3), UnorderedElementsAre(Op::kSub));
  EXPECT_THAT(scheduled_ops(4), UnorderedElementsAre(Op::kUMul, Op::kAdd));
  EXPECT_THAT(scheduled_ops(5), UnorderedElementsAre(Op::kConcat, Op::kNeg));
}

TEST_F(PipelineScheduleTest, LongPipelineLength) {
  // Generate an absurdly long pipeline schedule. Most stages are empty, but it
  // should not crash.
  auto p = CreatePackage();
  FunctionBuilder fb(TestName(), p.get());
  Type* u32 = p->GetBitsType(32);
  auto x = fb.Param("x", u32);
  auto bitslice = fb.BitSlice(x, /*start=*/7, /*width=*/20);
  auto zext = fb.ZeroExtend(bitslice, /*new_bit_count=*/32);

  XLS_ASSERT_OK_AND_ASSIGN(Function * func, fb.Build());

  XLS_ASSERT_OK_AND_ASSIGN(
      PipelineSchedule schedule,
      PipelineSchedule::Run(func, TestDelayEstimator(),
                            SchedulingOptions().pipeline_stages(100)));

  EXPECT_EQ(schedule.length(), 100);
  // Most stages should be empty.
  EXPECT_THAT(schedule.nodes_in_cycle(0),
              UnorderedElementsAre(x.node(), bitslice.node()));
  // The bitslice is the narrowest among the chain of operations so it should
  // precede the long chain of empty stages.
  for (int64_t i = 1; i < 99; ++i) {
    EXPECT_THAT(schedule.nodes_in_cycle(i), UnorderedElementsAre());
  }
  EXPECT_THAT(schedule.nodes_in_cycle(99), UnorderedElementsAre(zext.node()));
}

TEST_F(PipelineScheduleTest, ClockPeriodMargin) {
  auto p = CreatePackage();
  FunctionBuilder fb(TestName(), p.get());
  Type* u32 = p->GetBitsType(32);
  auto x = fb.Param("x", u32);
  // Chain of six negates.
  fb.Negate(fb.Negate(fb.Negate(fb.Negate(fb.Negate(fb.Negate(x))))));

  XLS_ASSERT_OK_AND_ASSIGN(Function * func, fb.Build());

  XLS_ASSERT_OK_AND_ASSIGN(
      PipelineSchedule schedule,
      PipelineSchedule::Run(func, TestDelayEstimator(),
                            SchedulingOptions().clock_period_ps(3)));
  EXPECT_EQ(schedule.length(), 2);

  {
    XLS_ASSERT_OK_AND_ASSIGN(
        PipelineSchedule schedule,
        PipelineSchedule::Run(
            func, TestDelayEstimator(),
            SchedulingOptions().clock_period_ps(3).clock_margin_percent(0)));
    EXPECT_EQ(schedule.length(), 2);
  }
  {
    XLS_ASSERT_OK_AND_ASSIGN(
        PipelineSchedule schedule,
        PipelineSchedule::Run(
            func, TestDelayEstimator(),
            SchedulingOptions().clock_period_ps(3).clock_margin_percent(33)));
    EXPECT_EQ(schedule.length(), 3);
  }
  {
    XLS_ASSERT_OK_AND_ASSIGN(
        PipelineSchedule schedule,
        PipelineSchedule::Run(
            func, TestDelayEstimator(),
            SchedulingOptions().clock_period_ps(3).clock_margin_percent(66)));
    EXPECT_EQ(schedule.length(), 6);
  }
  EXPECT_THAT(
      PipelineSchedule::Run(
          func, TestDelayEstimator(),
          SchedulingOptions().clock_period_ps(3).clock_margin_percent(200))
          .status(),
      status_testing::StatusIs(
          absl::StatusCode::kInvalidArgument,
          ::testing::HasSubstr(
              "Clock period non-positive (-3ps) after adjusting for margin. "
              "Original clock period: 3ps, clock margin: 200%")));
}

TEST_F(PipelineScheduleTest, PeriodRelaxation) {
  auto p = CreatePackage();
  FunctionBuilder fb(TestName(), p.get());
  Type* u32 = p->GetBitsType(32);
  auto x = fb.Param("x", u32);

  // Fanout
  auto x1 = fb.Negate(x);
  auto x11 = fb.Negate(x1);
  auto x21 = fb.Negate(x1);
  auto x111 = fb.Negate(x11);
  auto x211 = fb.Negate(x11);
  auto x121 = fb.Negate(x21);
  auto x221 = fb.Negate(x21);

  // Fanin
  auto y11 = fb.Or(x111, x211);
  auto y21 = fb.Or(x121, x221);
  auto y1 = fb.Or(y11, y21);
  fb.Negate(y1);

  XLS_ASSERT_OK_AND_ASSIGN(Function * func, fb.Build());

  XLS_ASSERT_OK_AND_ASSIGN(
      PipelineSchedule schedule,
      PipelineSchedule::Run(func, TestDelayEstimator(),
                            SchedulingOptions().pipeline_stages(2)));
  EXPECT_EQ(schedule.length(), 2);
  int64_t reg_count_default = schedule.CountFinalInteriorPipelineRegisters();

  for (int64_t relax_percent : std::vector{50, 100}) {
    XLS_ASSERT_OK_AND_ASSIGN(
        PipelineSchedule schedule,
        PipelineSchedule::Run(
            func, TestDelayEstimator(),
            SchedulingOptions().pipeline_stages(2).period_relaxation_percent(
                relax_percent)));
    EXPECT_EQ(schedule.length(), 2);
    int64_t reg_count_relaxed = schedule.CountFinalInteriorPipelineRegisters();
    EXPECT_LT(reg_count_relaxed, reg_count_default);
  }
}

TEST_F(PipelineScheduleTest, MinCutCycleOrders) {
  EXPECT_THAT(GetMinCutCycleOrders(0), ElementsAre(std::vector<int64_t>()));
  EXPECT_THAT(GetMinCutCycleOrders(1), ElementsAre(std::vector<int64_t>({0})));
  EXPECT_THAT(
      GetMinCutCycleOrders(2),
      ElementsAre(std::vector<int64_t>({0, 1}), std::vector<int64_t>({1, 0})));
  EXPECT_THAT(GetMinCutCycleOrders(3),
              ElementsAre(std::vector<int64_t>({0, 1, 2}),
                          std::vector<int64_t>({2, 1, 0}),
                          std::vector<int64_t>({1, 0, 2})));
  EXPECT_THAT(GetMinCutCycleOrders(4),
              ElementsAre(std::vector<int64_t>({0, 1, 2, 3}),
                          std::vector<int64_t>({3, 2, 1, 0}),
                          std::vector<int64_t>({1, 0, 2, 3})));
  EXPECT_THAT(GetMinCutCycleOrders(5),
              ElementsAre(std::vector<int64_t>({0, 1, 2, 3, 4}),
                          std::vector<int64_t>({4, 3, 2, 1, 0}),
                          std::vector<int64_t>({2, 0, 1, 3, 4})));
  EXPECT_THAT(GetMinCutCycleOrders(8),
              ElementsAre(std::vector<int64_t>({0, 1, 2, 3, 4, 5, 6, 7}),
                          std::vector<int64_t>({7, 6, 5, 4, 3, 2, 1, 0}),
                          std::vector<int64_t>({3, 1, 0, 2, 5, 4, 6, 7})));
}

TEST_F(PipelineScheduleTest, SerializeAndDeserialize) {
  auto p = CreatePackage();
  FunctionBuilder fb(TestName(), p.get());
  Type* u32 = p->GetBitsType(32);
  auto x = fb.Param("x", u32);
  auto y = fb.Param("y", u32);
  auto z = fb.Param("z", u32);
  fb.Negate(fb.Concat({(fb.Not(fb.Negate(x | y)) - z) * x, z + z}));
  XLS_ASSERT_OK_AND_ASSIGN(Function * func, fb.Build());
  XLS_ASSERT_OK_AND_ASSIGN(
      PipelineSchedule schedule,
      PipelineSchedule::Run(func, TestDelayEstimator(),
                            SchedulingOptions().pipeline_stages(3)));

  PipelineScheduleProto proto = schedule.ToProto();
  XLS_ASSERT_OK_AND_ASSIGN(PipelineSchedule clone,
                           PipelineSchedule::FromProto(func, proto));
  for (const Node* node : func->nodes()) {
    EXPECT_EQ(schedule.cycle(node), clone.cycle(node));
  }
}

TEST_F(PipelineScheduleTest, ProcSchedule) {
  Package p("p");
  Type* u16 = p.GetBitsType(16);
  XLS_ASSERT_OK_AND_ASSIGN(
      Channel * in_ch,
      p.CreateStreamingChannel("in", ChannelOps::kReceiveOnly, u16));
  XLS_ASSERT_OK_AND_ASSIGN(
      Channel * out_ch,
      p.CreateStreamingChannel("out", ChannelOps::kSendOnly, u16));
  TokenlessProcBuilder pb("the_proc", Value(UBits(42, 16)), "tkn", "st", &p);
  BValue rcv = pb.Receive(in_ch);
  BValue out = pb.Negate(pb.Not(pb.Negate(rcv)));
  BValue send = pb.Send(out_ch, out);
  XLS_ASSERT_OK_AND_ASSIGN(Proc * proc, pb.Build(pb.GetStateParam(0)));

  XLS_ASSERT_OK_AND_ASSIGN(
      PipelineSchedule schedule,
      PipelineSchedule::Run(proc, TestDelayEstimator(),
                            SchedulingOptions().clock_period_ps(1)));

  EXPECT_EQ(schedule.length(), 3);

  EXPECT_EQ(schedule.cycle(rcv.node()), 0);
  EXPECT_EQ(schedule.cycle(send.node()), 2);
}

TEST_F(PipelineScheduleTest, StatelessProcSchedule) {
  Package p("p");
  Type* u16 = p.GetBitsType(16);
  XLS_ASSERT_OK_AND_ASSIGN(
      Channel * in_ch,
      p.CreateStreamingChannel("in", ChannelOps::kReceiveOnly, u16));
  XLS_ASSERT_OK_AND_ASSIGN(
      Channel * out_ch,
      p.CreateStreamingChannel("out", ChannelOps::kSendOnly, u16));
  TokenlessProcBuilder pb("the_proc", "tkn", &p);
  BValue rcv = pb.Receive(in_ch);
  BValue out = pb.Negate(pb.Not(pb.Negate(rcv)));
  BValue send = pb.Send(out_ch, out);
  XLS_ASSERT_OK_AND_ASSIGN(Proc * proc, pb.Build(std::vector<BValue>({})));

  XLS_ASSERT_OK_AND_ASSIGN(
      PipelineSchedule schedule,
      PipelineSchedule::Run(proc, TestDelayEstimator(),
                            SchedulingOptions().clock_period_ps(1)));

  EXPECT_EQ(schedule.length(), 3);

  EXPECT_EQ(schedule.cycle(rcv.node()), 0);
  EXPECT_EQ(schedule.cycle(send.node()), 2);
}

TEST_F(PipelineScheduleTest, MultistateProcSchedule) {
  Package p("p");
  Type* u16 = p.GetBitsType(16);
  XLS_ASSERT_OK_AND_ASSIGN(
      Channel * in_ch,
      p.CreateStreamingChannel("in", ChannelOps::kReceiveOnly, u16));
  XLS_ASSERT_OK_AND_ASSIGN(
      Channel * out_ch,
      p.CreateStreamingChannel("out", ChannelOps::kSendOnly, u16));
  TokenlessProcBuilder pb("the_proc", "tkn", &p);
  BValue st0 = pb.StateElement("st0", Value(UBits(0, 16)));
  BValue st1 = pb.StateElement("st1", Value(UBits(0, 16)));
  BValue rcv = pb.Receive(in_ch);
  BValue out = pb.Negate(pb.Not(pb.Negate(rcv)));
  BValue send = pb.Send(out_ch, out);
  XLS_ASSERT_OK_AND_ASSIGN(Proc * proc,
                           pb.Build({pb.Add(st0, rcv), pb.Subtract(st1, rcv)}));

  XLS_ASSERT_OK_AND_ASSIGN(
      PipelineSchedule schedule,
      PipelineSchedule::Run(proc, TestDelayEstimator(),
                            SchedulingOptions().clock_period_ps(1)));

  EXPECT_EQ(schedule.length(), 3);

  EXPECT_EQ(schedule.cycle(st0.node()), 0);
  EXPECT_EQ(schedule.cycle(st1.node()), 0);
  EXPECT_EQ(schedule.cycle(rcv.node()), 0);
  EXPECT_EQ(schedule.cycle(send.node()), 2);
}

TEST_F(PipelineScheduleTest, ProcWithConditionalReceive) {
  // Test a proc with a conditional receive. The receive condition must be
  // scheduled in cycle 0 along with the receive.
  Package p("p");
  Type* u16 = p.GetBitsType(16);
  XLS_ASSERT_OK_AND_ASSIGN(
      Channel * in_ch,
      p.CreateStreamingChannel("in", ChannelOps::kReceiveOnly, u16));
  XLS_ASSERT_OK_AND_ASSIGN(
      Channel * out_ch,
      p.CreateStreamingChannel("out", ChannelOps::kSendOnly, u16));
  TokenlessProcBuilder pb("the_proc", Value(UBits(42, 16)), "tkn", "st", &p);
  BValue cond = pb.Literal(UBits(0, 1));
  BValue rcv = pb.ReceiveIf(in_ch, cond);
  BValue out = pb.Negate(pb.Not(pb.Negate(rcv)));
  BValue send = pb.Send(out_ch, out);
  XLS_ASSERT_OK_AND_ASSIGN(Proc * proc, pb.Build(pb.GetStateParam(0)));

  XLS_ASSERT_OK_AND_ASSIGN(
      PipelineSchedule schedule,
      PipelineSchedule::Run(proc, TestDelayEstimator(),
                            SchedulingOptions().clock_period_ps(1)));

  EXPECT_EQ(schedule.length(), 3);

  EXPECT_EQ(schedule.cycle(rcv.node()), 0);
  EXPECT_EQ(schedule.cycle(cond.node()), 0);
  EXPECT_EQ(schedule.cycle(send.node()), 2);
}

TEST_F(PipelineScheduleTest, ProcWithConditionalReceiveError) {
  // Test a proc with a conditional receive. The receive condition takes too
  // long to compute which prevents the receive from being scheduled in the
  // first cycle which results in an error.
  Package p("p");
  Type* u16 = p.GetBitsType(16);
  XLS_ASSERT_OK_AND_ASSIGN(
      Channel * in_ch,
      p.CreateStreamingChannel("in", ChannelOps::kReceiveOnly, u16));
  XLS_ASSERT_OK_AND_ASSIGN(
      Channel * out_ch,
      p.CreateStreamingChannel("out", ChannelOps::kSendOnly, u16));
  TokenlessProcBuilder pb("the_proc", Value(UBits(42, 16)), "tkn", "st", &p);
  BValue cond = pb.Not(pb.Not(pb.Literal(UBits(0, 1))));
  BValue rcv = pb.ReceiveIf(in_ch, cond, absl::nullopt, "rcv");
  BValue out = pb.Negate(pb.Not(pb.Negate(rcv)));
  pb.Send(out_ch, out);
  XLS_ASSERT_OK_AND_ASSIGN(Proc * proc, pb.Build(pb.GetStateParam(0)));

  ASSERT_THAT(
      PipelineSchedule::Run(proc, TestDelayEstimator(),
                            SchedulingOptions().clock_period_ps(1))
          .status(),
      StatusIs(absl::StatusCode::kResourceExhausted,
               HasSubstr("node `rcv` must be scheduled in the first cycle but "
                         "that is impossible due to the node's operand(s)")));
}

TEST_F(PipelineScheduleTest, ReceiveFollowedBySend) {
  Package package = Package(TestName());

  Type* u32 = package.GetBitsType(32);
  XLS_ASSERT_OK_AND_ASSIGN(
      Channel * ch_in,
      package.CreateStreamingChannel("in", ChannelOps::kReceiveOnly, u32));
  XLS_ASSERT_OK_AND_ASSIGN(
      Channel * ch_out,
      package.CreateStreamingChannel("out", ChannelOps::kSendOnly, u32));

  ProcBuilder pb(TestName(), /*init_value=*/Value::Tuple({}),
                 /*token_name=*/"tkn", /*state_name=*/"st", &package);

  BValue rcv = pb.Receive(ch_in, pb.GetTokenParam());
  BValue send = pb.Send(ch_out, /*token=*/pb.TupleIndex(rcv, 0),
                        /*data=*/pb.TupleIndex(rcv, 1));
  XLS_ASSERT_OK_AND_ASSIGN(Proc * proc, pb.Build(send, pb.GetStateParam(0)));

  XLS_ASSERT_OK_AND_ASSIGN(const DelayEstimator* delay_estimator,
                           GetDelayEstimator("unit"));
  XLS_ASSERT_OK_AND_ASSIGN(
      PipelineSchedule schedule,
      PipelineSchedule::Run(proc, *delay_estimator,
                            SchedulingOptions().pipeline_stages(5)));
  EXPECT_EQ(schedule.length(), 5);
  EXPECT_EQ(schedule.cycle(rcv.node()), 0);
  EXPECT_EQ(schedule.cycle(send.node()), 4);
}

TEST_F(PipelineScheduleTest, ProcScheduleWithInputDelay) {
  Package p("p");

  Type* u16 = p.GetBitsType(16);

  XLS_ASSERT_OK_AND_ASSIGN(
      Channel * in_ch,
      p.CreateStreamingChannel("in", ChannelOps::kReceiveOnly, u16));
  XLS_ASSERT_OK_AND_ASSIGN(
      Channel * out_ch,
      p.CreateStreamingChannel("out", ChannelOps::kSendOnly, u16));

  TokenlessProcBuilder pb("the_proc", Value::Tuple({}), "tkn", "st", &p);

  BValue rcv = pb.Receive(in_ch);
  BValue out = pb.Negate(pb.Not(pb.Negate(pb.Not(pb.Negate(rcv)))));
  BValue send = pb.Send(out_ch, out);

  XLS_ASSERT_OK_AND_ASSIGN(Proc * proc, pb.Build(pb.GetStateParam(0)));

  XLS_ASSERT_OK_AND_ASSIGN(
      PipelineSchedule schedule,
      PipelineSchedule::Run(proc, TestDelayEstimator(),
                            SchedulingOptions().pipeline_stages(2)));
  EXPECT_EQ(schedule.length(), 2);
  EXPECT_EQ(schedule.cycle(rcv.node()), 0);
  EXPECT_EQ(schedule.cycle(send.node()), 1);

  int64_t nodes_in_first_cycle = schedule.nodes_in_cycle(0).size();
  for (int64_t input_delay : std::vector{2, 5, 10}) {
    XLS_ASSERT_OK_AND_ASSIGN(
        PipelineSchedule schedule_with_input_delay,
        PipelineSchedule::Run(
            proc, TestDelayEstimator(),
            SchedulingOptions().pipeline_stages(2).additional_input_delay_ps(
                input_delay)));
    int64_t updated_nodes_in_first_cycle =
        schedule_with_input_delay.nodes_in_cycle(0).size();

    // Nodes in first cycle will always decrease
    EXPECT_LE(updated_nodes_in_first_cycle, nodes_in_first_cycle);

    if (input_delay >= 5) {
      // With a large enough input delay the only thing that will
      // be scheduled in the first cycle is the receive plus
      // token and state
      //
      // tkn: token = param(tkn, id=1)
      // receive.3: (token, bits[16]) = receive(tkn, channel_id=0, id=3)
      // st: () = param(st, id=2)
      EXPECT_EQ(updated_nodes_in_first_cycle, 3);
    }
  }
}

TEST_F(PipelineScheduleTest, BenchmarkTest) {
  // Compute the correctness of SDC
  XLS_ASSERT_OK_AND_ASSIGN(std::vector<std::string> benchmark_names,
                           sample_packages::GetBenchmarkNames());
  for (const std::string& benchmark_name : benchmark_names) {
    XLS_ASSERT_OK_AND_ASSIGN(
        std::unique_ptr<Package> p,
        sample_packages::GetBenchmark(benchmark_name, /*optimized=*/true));

    absl::StatusOr<Function*> f_status = p->GetTopAsFunction();
    if (!f_status.ok()) {
      // Skip packages which need the entry to be specified explicitly.
      continue;
    }
    Function * f = f_status.value();

    XLS_ASSERT_OK_AND_ASSIGN(std::vector<CriticalPathEntry> critical_path, AnalyzeCriticalPath(f, absl::nullopt, TestDelayEstimator()));
    int64_t max_delay = critical_path.front().path_delay_ps;

    for(int64_t clk = 3; clk <= std::min((int64_t)30, max_delay/2 - 1); ++clk){
      SchedulingOptions sdc_exact_options(SchedulingStrategy::MINIMIZE_REGISTERS_SDC);
      SchedulingOptions cut_options(SchedulingStrategy::MINIMIZE_REGISTERS);
      cut_options.clock_period_ps(clk);
      sdc_exact_options.clock_period_ps(clk);

      XLS_ASSERT_OK_AND_ASSIGN(
          PipelineSchedule sdc_exact_schedule,
          PipelineSchedule::Run(f, TestDelayEstimator(), sdc_exact_options));
      XLS_EXPECT_OK(sdc_exact_schedule.Verify());
      XLS_EXPECT_OK(sdc_exact_schedule.VerifyTiming(clk, TestDelayEstimator()));

      XLS_ASSERT_OK_AND_ASSIGN(
          PipelineSchedule cut_schedule,
          PipelineSchedule::Run(f, TestDelayEstimator(), cut_options));
    }
  }
}

}  // namespace
}  // namespace xls
