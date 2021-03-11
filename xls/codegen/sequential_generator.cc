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

#include "xls/codegen/sequential_generator.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "xls/codegen/finite_state_machine.h"
#include "xls/codegen/module_builder.h"
#include "xls/codegen/module_signature.h"
#include "xls/codegen/module_signature.pb.h"
#include "xls/codegen/pipeline_generator.h"
#include "xls/codegen/vast.h"
#include "xls/common/logging/logging.h"
#include "xls/common/status/ret_check.h"
#include "xls/common/status/status_macros.h"
#include "xls/delay_model/delay_estimator.h"
#include "xls/delay_model/delay_estimators.h"
#include "xls/ir/function.h"
#include "xls/ir/type.h"
#include "xls/passes/passes.h"
#include "xls/scheduling/pipeline_schedule.h"

namespace xls {
namespace verilog {

using Register = ModuleBuilder::Register;

absl::Status SequentialModuleBuilder::AddFsm(
    int64_t pipeline_latency, LogicRef* index_holds_max_inclusive_value,
    LogicRef* last_pipeline_cycle_wire) {
  // Configure reset options.
  const absl::optional<ResetProto>* reset_options =
      &sequential_options_.reset();
  if (!reset_options->has_value()) {
    return absl::InvalidArgumentError(
        "Tried to create FSM without specifying reset in SequentialOptions.");
  }
  XLS_RET_CHECK(port_references_.reset.has_value());
  XLS_RET_CHECK(reset_options->value().has_asynchronous());
  XLS_RET_CHECK(reset_options->value().has_active_low());
  Reset reset = {port_references_.reset.value(),
                 reset_options->value().asynchronous(),
                 reset_options->value().active_low()};

  // Create states.
  FsmBuilder fsm("sequential_fsm", module(), port_references_.clk,
                 sequential_options_.use_system_verilog(), reset);
  // Null state is used as the initial state to force a state transition to
  // Ready state when the FSM is reset. Without this, FSM outputs may not
  // simulate correctly in the ready state (b/159519643). The null state is not
  // reachable (and does not transition to other states), does not affect
  // outputs, and does not increase the number of bits needed to represent the
  // state.  Therefore, the effect of including null state on the produced
  // hardware should be negligible (1's of gates), due only to different state
  // encodings. This is a little clunky (hardware choice based on simulation
  // considerations), but is prefered to massaging initial output values to
  // match ready state values or forcing state transitions in tests before
  // checking output because these approaches could lead to undetected
  // differences in simulation vs. hardware behavior if not done correctly. Note
  // that we require the sequential module to be reset before initial use as a
  // consequence of this choice, but this was already the expected use case.
  fsm.AddState("Null");
  FsmState* ready_state = fsm.AddState("Ready");
  fsm.SetResetState(ready_state);
  FsmState* running_state = fsm.AddState("Running");
  FsmState* done_state = fsm.AddState("Done");

  // Create output signals.
  // Note: The FSM builder is ModuleBuilder-oblivious and emits its code at the
  // end of the module. This means code generated by the module builder cannot
  // direclty reference FSM outputs. Therefore, we declare wires via the
  // ModuleBuilder that will appear at the top of the emitted code and drive
  // these wires with the FSM outputs at the end of the module. We postpone
  // emitting assignments where the rhs is an FSM output until after the FSM is
  // built. Otherwise, uses would appear before declarations.
  absl::flat_hash_map<LogicRef*, LogicRef*> wire_reg_assignments;
  auto add_output_and_drive_wire =
      [&fsm, &wire_reg_assignments](
          absl::string_view name, int64_t default_value,
          LogicRef* driven_wire) -> absl::StatusOr<FsmOutput*> {
    FsmOutput* fsm_out = fsm.AddOutput1(name, default_value);
    if (wire_reg_assignments.contains(driven_wire)) {
      return absl::InternalError(
          "SequentialModuleBuilder::AddFsm: Tried to "
          "assign more than one value to wire.");
    }
    wire_reg_assignments[driven_wire] = fsm_out->logic_ref;
    return fsm_out;
  };
  XLS_ASSIGN_OR_RETURN(
      FsmOutput * fsm_ready_in,
      add_output_and_drive_wire("fsm_ready_in", /*default=*/0,
                                port_references_.ready_in.value()));
  XLS_ASSIGN_OR_RETURN(
      FsmOutput * fsm_valid_out,
      add_output_and_drive_wire("fsm_valid_out", /*default=*/0,
                                port_references_.valid_out.value()));
  XLS_ASSIGN_OR_RETURN(
      FsmOutput * fsm_last_pipeline_cycle,
      add_output_and_drive_wire("fsm_last_pipeline_cycle", /*default=*/0,
                                last_pipeline_cycle_wire));

  // Set state logic.
  ready_state->SetOutput(fsm_ready_in, 1)
      .OnCondition(port_references_.valid_in.value())
      .NextState(running_state);
  running_state
      ->OnCondition(file_.BitwiseAnd(index_holds_max_inclusive_value,
                                     fsm_last_pipeline_cycle->logic_ref))
      .NextState(done_state);
  done_state->SetOutput(fsm_valid_out, 1)
      .OnCondition(port_references_.ready_out.value())
      .NextState(ready_state);

  // Add counter for multi-stage loop body pipeline.
  if (pipeline_latency == 0) {
    running_state->SetOutput(fsm_last_pipeline_cycle, 1);
  } else {
    FsmCounter* pipeline_counter = fsm.AddDownCounter(
        "pipeline_counter", Bits::MinBitCountUnsigned(pipeline_latency));
    ready_state->SetCounter(pipeline_counter, pipeline_latency);
    running_state->OnCounterIsZero(pipeline_counter)
        .SetOutput(fsm_last_pipeline_cycle, 1)
        .SetCounter(pipeline_counter, pipeline_latency);
  }

  // Build and connect.
  XLS_RETURN_IF_ERROR(fsm.Build());
  module()->Add<BlankLine>();
  module()->Add<Comment>("FSM driven wires.");
  for (const auto& wire_reg : wire_reg_assignments) {
    module()->Add<ContinuousAssignment>(wire_reg.first, wire_reg.second);
  }
  return absl::OkStatus();
}

absl::Status SequentialModuleBuilder::AddSequentialLogic() {
  // Declare variables / wires.
  LogicRef* last_pipeline_cycle =
      module_builder_->DeclareVariable("last_pipeline_cycle", 1);
  XLS_CHECK_EQ(port_references_.data_out.size(), 1);
  LogicRef* pipeline_output = module_builder_->DeclareVariable(
      "pipeline_output", module_signature()->data_outputs().at(0).width());

  // Add index counter.
  LogicRef* ready_in = port_references_.ready_in.value();
  XLS_ASSIGN_OR_RETURN(
      StridedCounterReferences index_references,
      AddStaticStridedCounter("index_counter", loop_->stride(),
                              loop_->stride() * loop_->trip_count(),
                              port_references_.clk, ready_in,
                              last_pipeline_cycle));

  // Add FSM.
  int64_t pipeline_latency =
      loop_body_pipeline_result_->signature.proto().pipeline().latency();
  XLS_RETURN_IF_ERROR(AddFsm(pipeline_latency,
                             index_references.holds_max_inclusive_value,
                             last_pipeline_cycle));

  auto make_register = [&](Expression* next, const PortProto* port_proto) {
    return module_builder_->DeclareRegister(port_proto->name() + "_register",
                                            port_proto->width(), next);
  };
  // Add accumulator register.
  XLS_RET_CHECK(port_references_.ready_in.has_value());
  XLS_ASSIGN_OR_RETURN(
      Register accumulator_register,
      make_register(file_.Ternary(ready_in, port_references_.data_in.at(0),
                                  pipeline_output),
                    &module_signature_->data_outputs().at(0)));
  XLS_RETURN_IF_ERROR(module_builder_->AssignRegisters(
      {accumulator_register}, file_.BitwiseOr(ready_in, last_pipeline_cycle)));

  // Add registers for invariants.
  int64_t num_inputs = port_references_.data_in.size();
  std::vector<Register> invariant_registers;
  invariant_registers.resize(num_inputs - 1);
  for (int64_t input_idx = 1; input_idx < num_inputs; ++input_idx) {
    XLS_ASSIGN_OR_RETURN(
        invariant_registers.at(input_idx - 1),
        make_register(port_references_.data_in.at(input_idx),
                      &module_signature_->data_inputs().at(input_idx)));
  }
  XLS_RETURN_IF_ERROR(
      module_builder_->AssignRegisters(invariant_registers, ready_in));

  // Add loop body pipeline.
  XLS_RETURN_IF_ERROR(
      InstantiateLoopBody(index_references.value, accumulator_register,
                          invariant_registers, pipeline_output));

  // Drive output.
  AddContinuousAssignment(port_references_.data_out.at(0),
                          accumulator_register.ref);

  return absl::OkStatus();
}

absl::StatusOr<SequentialModuleBuilder::StridedCounterReferences>
SequentialModuleBuilder::AddStaticStridedCounter(
    std::string name, int64_t stride, int64_t value_limit_exclusive,
    LogicRef* clk, LogicRef* set_zero_arg, LogicRef* increment_arg) {
  // Create references.
  StridedCounterReferences refs;
  refs.set_zero = set_zero_arg;
  refs.increment = increment_arg;

  // Check if specification is valid.
  if (value_limit_exclusive <= 0) {
    return absl::UnimplementedError(
        "Tried to generate static strided counter with non-positive "
        "value_limit_exlusive - not currently supported.");
  }
  if (stride <= 0) {
    return absl::UnimplementedError(
        "Tried to generate static strided counter with non-positive stride - "
        "not currently supported.");
  }

  // Pretty print verilog.
  module_builder_->declaration_section()->Add<BlankLine>();
  module_builder_->declaration_section()->Add<Comment>(
      "Declarations for counter " + name);
  module_builder_->assignment_section()->Add<BlankLine>();
  module_builder_->assignment_section()->Add<Comment>(
      "Assignments for counter " + name);

  // Determine counter value limit and the number of bits needed to represent
  // this number.
  int64_t value_limit_exclusive_minus = value_limit_exclusive - 1;
  int64_t max_inclusive_value =
      value_limit_exclusive_minus - ((value_limit_exclusive_minus) % stride);
  int64_t num_counter_bits = Bits::MinBitCountUnsigned(max_inclusive_value);
  XLS_CHECK_GT(num_counter_bits, 0);

  // Create the counter.
  // Note: Need to "forward-declare" counter_wire so that we can compute
  // counter + stride before calling DeclareRegister, which gives us the counter
  // register.
  LogicRef* counter_wire =
      module_builder_->DeclareVariable(name + "_wire", num_counter_bits);
  Expression* counter_next =
      file_.Ternary(refs.set_zero, file_.PlainLiteral(0),
                    file_.Add(counter_wire, file_.PlainLiteral(stride)));
  XLS_ASSIGN_OR_RETURN(
      ModuleBuilder::Register counter_register,
      module_builder_->DeclareRegister(name, num_counter_bits, counter_next));
  AddContinuousAssignment(counter_wire, counter_register.ref);
  refs.value = counter_register.ref;

  // Add counter always-block.
  Expression* load_enable = file_.BitwiseOr(refs.increment, refs.set_zero);
  XLS_RETURN_IF_ERROR(
      module_builder_->AssignRegisters({counter_register}, load_enable));

  // Compare counter value to maximum value.
  refs.holds_max_inclusive_value = DeclareVariableAndAssign(
      name + "_holds_max_inclusive_value",
      file_.Equals(refs.value, file_.PlainLiteral(max_inclusive_value)),
      num_counter_bits);

  return refs;
}

absl::StatusOr<ModuleGeneratorResult> SequentialModuleBuilder::Build() {
  // Generate the loop body module.
  absl::StatusOr<std::unique_ptr<ModuleGeneratorResult>> loop_body_status =
      GenerateLoopBodyPipeline();
  XLS_RETURN_IF_ERROR(loop_body_status.status());
  loop_body_pipeline_result_ = std::move(loop_body_status.value());
  XLS_RET_CHECK(loop_body_pipeline_result_->signature.proto().has_pipeline());
  XLS_CHECK_EQ(loop_body_pipeline_result_->signature.proto()
                   .pipeline()
                   .initiation_interval(),
               1);

  // Get the module signature.
  absl::StatusOr<std::unique_ptr<ModuleSignature>> signature_result =
      GenerateModuleSignature();
  XLS_RETURN_IF_ERROR(signature_result.status());
  module_signature_ = std::move(signature_result.value());

  // Initialize module builder.
  XLS_RETURN_IF_ERROR(InitializeModuleBuilder(*module_signature_.get()));

  // Add internal logic.
  XLS_RETURN_IF_ERROR(AddSequentialLogic());

  // Create result.
  ModuleGeneratorResult result;
  result.signature = *module_signature();
  result.verilog_text.append(loop_body_pipeline_result_->verilog_text);
  result.verilog_text.append("\n");
  result.verilog_text.append(module_builder_->module()->Emit());

  return result;
}

absl::StatusOr<std::unique_ptr<ModuleSignature>>
SequentialModuleBuilder::GenerateModuleSignature() {
  std::string module_name = sequential_options_.module_name().has_value()
                                ? sequential_options_.module_name().value()
                                : loop_->GetName() + "_sequential_module";
  ModuleSignatureBuilder sig_builder(SanitizeIdentifier(module_name));

  // Default Inputs.
  // We conservatively apply SanitizeIdentifier to all names, including
  // constants, so that signature names match verilog names.
  sig_builder.WithClock(SanitizeIdentifier("clk"));
  for (const Node* op_in : loop_->operands()) {
    sig_builder.AddDataInput(SanitizeIdentifier(op_in->GetName() + "_in"),
                             op_in->GetType()->GetFlatBitCount());
  }

  // Default Outputs.
  sig_builder.AddDataOutput(SanitizeIdentifier(loop_->GetName() + "_out"),
                            loop_->GetType()->GetFlatBitCount());

  // Reset.
  if (sequential_options_.reset().has_value()) {
    sig_builder.WithReset(
        SanitizeIdentifier(sequential_options_.reset()->name()),
        sequential_options_.reset()->asynchronous(),
        sequential_options_.reset()->active_low());
  }

  // Function type.
  FunctionType* body_type = loop_->body()->GetType();
  FunctionType module_function_type(body_type->parameters().subspan(1),
                                    body_type->return_type());
  sig_builder.WithFunctionType(&module_function_type);

  // TODO(jbaileyhandle): Add options for other interfaces.
  std::string ready_in_name = SanitizeIdentifier("ready_in");
  std::string valid_in_name = SanitizeIdentifier("valid_in");
  std::string ready_out_name = SanitizeIdentifier("ready_out");
  std::string valid_out_name = SanitizeIdentifier("valid_out");
  sig_builder.WithReadyValidInterface(ready_in_name, valid_in_name,
                                      ready_out_name, valid_out_name);

  // Build signature.
  std::unique_ptr<ModuleSignature> signature =
      absl::make_unique<ModuleSignature>();
  XLS_ASSIGN_OR_RETURN(*signature, sig_builder.Build());
  return std::move(signature);
}

absl::StatusOr<std::unique_ptr<ModuleGeneratorResult>>
SequentialModuleBuilder::GenerateLoopBodyPipeline() {
  // Set pipeline options.
  PipelineOptions pipeline_options;
  pipeline_options.flop_inputs(false).flop_outputs(false).use_system_verilog(
      sequential_options_.use_system_verilog());
  if (sequential_options_.reset().has_value()) {
    pipeline_options.reset(sequential_options_.reset().value());
  }

  // Get schedule.
  Function* loop_body_function = loop_->body();
  XLS_ASSIGN_OR_RETURN(
      PipelineSchedule schedule,
      PipelineSchedule::Run(loop_body_function,
                            *sequential_options_.delay_estimator(),
                            sequential_options_.pipeline_scheduling_options()));
  XLS_RETURN_IF_ERROR(schedule.Verify());

  std::unique_ptr<ModuleGeneratorResult> result =
      absl::make_unique<ModuleGeneratorResult>();
  XLS_ASSIGN_OR_RETURN(
      *result,
      ToPipelineModuleText(schedule, loop_body_function, pipeline_options));
  return std::move(result);
}

absl::Status SequentialModuleBuilder::InitializeModuleBuilder(
    const ModuleSignature& signature) {
  // Make builder.
  XLS_RET_CHECK(signature.proto().has_module_name());
  module_builder_ = absl::make_unique<ModuleBuilder>(
      signature.proto().module_name(), &file_,
      sequential_options_.use_system_verilog(),
      /*clk_name=*/"clk",
      signature.proto().has_reset()
          ? absl::optional<ResetProto>(signature.proto().reset())
          : absl::nullopt);

  auto add_input_port = [&](absl::string_view name, int64_t num_bits) {
    return module_builder_->AddInputPort(SanitizeIdentifier(name), num_bits);
  };
  auto add_output_port = [&](absl::string_view name, int64_t num_bits) {
    return module_builder_->module()->AddOutput(SanitizeIdentifier(name),
                                                file_.BitVectorType(num_bits));
  };

  // Clock.
  XLS_RET_CHECK(signature.proto().has_clock_name());
  port_references_.clk = module_builder_->clock();

  // Reset.
  if (signature.proto().has_reset()) {
    XLS_RET_CHECK(signature.proto().reset().has_name());
    port_references_.reset = module_builder_->reset()->signal;
  }

  // Ready-valid interface.
  if (signature.proto().has_ready_valid()) {
    const ReadyValidInterface& rv_interface = signature.proto().ready_valid();
    XLS_RET_CHECK(rv_interface.has_input_ready());
    XLS_RET_CHECK(rv_interface.has_input_valid());
    XLS_RET_CHECK(rv_interface.has_output_ready());
    XLS_RET_CHECK(rv_interface.has_output_valid());
    port_references_.ready_in = add_output_port(rv_interface.input_ready(), 1);
    port_references_.valid_in = add_input_port(rv_interface.input_valid(), 1);
    port_references_.ready_out = add_input_port(rv_interface.output_ready(), 1);
    port_references_.valid_out =
        add_output_port(rv_interface.output_valid(), 1);
  }

  // Data I/O.
  for (const PortProto& in_port : signature.data_inputs()) {
    XLS_RET_CHECK(in_port.has_direction());
    XLS_RET_CHECK_EQ(in_port.direction(), DIRECTION_INPUT);
    XLS_RET_CHECK(in_port.has_name());
    XLS_RET_CHECK(in_port.has_width());
    port_references_.data_in.push_back(
        add_input_port(in_port.name(), in_port.width()));
  }
  for (const PortProto& out_port : signature.data_outputs()) {
    XLS_RET_CHECK(out_port.has_direction());
    XLS_RET_CHECK_EQ(out_port.direction(), DIRECTION_OUTPUT);
    XLS_RET_CHECK(out_port.has_name());
    XLS_RET_CHECK(out_port.has_width());
    port_references_.data_out.push_back(
        add_output_port(out_port.name(), out_port.width()));
  }

  return absl::OkStatus();
}

absl::Status SequentialModuleBuilder::InstantiateLoopBody(
    LogicRef* index_value, const ModuleBuilder::Register& accumulator_reg,
    absl::Span<const ModuleBuilder::Register> invariant_registers,
    LogicRef* pipeline_output) {
  // Collect input names.
  std::vector<std::string> loop_in_names;
  for (const auto& input_port :
       loop_body_pipeline_result_->signature.data_inputs()) {
    loop_in_names.push_back(input_port.name());
  }

  // Collect connections.
  std::vector<Connection> loop_connections;
  // Index
  XLS_RET_CHECK_GE(loop_in_names.size(), 2);
  loop_connections.push_back({loop_in_names.at(0), index_value});
  // Accumulator
  loop_connections.push_back({loop_in_names.at(1), accumulator_reg.ref});
  // Invariants
  for (int64_t input_idx = 2; input_idx < loop_in_names.size(); ++input_idx) {
    loop_connections.push_back({loop_in_names.at(input_idx),
                                invariant_registers.at(input_idx - 2).ref});
  }
  // Reset
  XLS_RET_CHECK(sequential_options_.reset().has_value());
  XLS_RET_CHECK(loop_body_pipeline_result_->signature.proto().has_reset());
  loop_connections.push_back(
      {loop_body_pipeline_result_->signature.proto().reset().name(),
       port_references_.reset.value()});
  // Clk
  loop_connections.push_back(
      {loop_body_pipeline_result_->signature.proto().clock_name(),
       port_references_.clk});
  // Output
  loop_connections.push_back(
      {loop_body_pipeline_result_->signature.data_outputs().at(0).name(),
       pipeline_output});

  // Instantiate loop body.
  module_builder_->assignment_section()->Add<Instantiation>(
      /*module_name=*/loop_body_pipeline_result_->signature.module_name(),
      /*instance_name=*/"loop_body",
      /*parameters=*/std::vector<Connection>(),
      /*connections=*/loop_connections);

  return absl::OkStatus();
}

absl::StatusOr<ModuleGeneratorResult> ToSequentialModuleText(Function* func) {
  return absl::UnimplementedError(
      "Sequential generator does not yet support arbitrary functions.");
}

// Emits the given CountedFor as a verilog module which reuses the same hardware
// over time to executed loop iterations.
absl::StatusOr<ModuleGeneratorResult> ToSequentialModuleText(
    const SequentialOptions& options, const CountedFor* loop) {
  SequentialModuleBuilder builder(options, loop);
  return builder.Build();
}

}  // namespace verilog
}  // namespace xls
