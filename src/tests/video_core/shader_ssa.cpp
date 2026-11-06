// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/frontend/ir/ir_emitter.h"
#include "shader_recompiler/ir_opt/passes.h"

namespace {
namespace IR = Shader::IR;

std::array<IR::Value, 8> ReadVariables(IR::IREmitter& ir) {
    return {ir.GetReg(IR::Reg::R0), ir.GetPred(IR::Pred::P0), ir.GetGotoVariable(3),
            ir.GetIndirectBranchVariable(), ir.GetZFlag(), ir.GetSFlag(), ir.GetCFlag(),
            ir.GetOFlag()};
}

void WriteVariables(IR::IREmitter& ir, u32 value, bool flag) {
    ir.SetReg(IR::Reg::R0, ir.Imm32(value));
    ir.SetPred(IR::Pred::P0, ir.Imm1(flag));
    ir.SetGotoVariable(3, ir.Imm1(flag));
    ir.SetIndirectBranchVariable(ir.Imm32(value));
    ir.SetZFlag(ir.Imm1(flag));
    ir.SetSFlag(ir.Imm1(flag));
    ir.SetCFlag(ir.Imm1(flag));
    ir.SetOFlag(ir.Imm1(flag));
}
} // namespace

TEST_CASE("SSA reads preserve local definitions and subsequent writes",
          "[video_core][shader_recompiler][ssa]") {
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    IR::IREmitter ir{block};
    const auto undefined_first = ir.GetReg(IR::Reg::R1);
    const auto undefined_second = ir.GetReg(IR::Reg::R1);
    WriteVariables(ir, 17, true);
    const auto first = ReadVariables(ir);
    const auto repeated = ReadVariables(ir);
    WriteVariables(ir, 29, false);
    const auto overwritten = ReadVariables(ir);
    IR::Program program;
    program.blocks = {&block};
    program.post_order_blocks = {&block};

    Shader::Optimization::SsaRewritePass(program);

    CHECK(undefined_first.Resolve() == undefined_second.Resolve());
    REQUIRE_FALSE(undefined_first.Resolve().IsImmediate());
    CHECK(undefined_first.Resolve().Inst()->GetOpcode() == IR::Opcode::UndefU32);
    for (size_t index = 0; index < first.size(); ++index) {
        const bool integer = index == 0 || index == 3;
        CHECK(first[index].Resolve() == (integer ? IR::Value{u32{17}} : IR::Value{true}));
        CHECK(repeated[index].Resolve() == first[index].Resolve());
        CHECK(overwritten[index].Resolve() == (integer ? IR::Value{u32{29}} : IR::Value{false}));
    }
}

TEST_CASE("SSA repeated reads retain branch phi operands",
          "[video_core][shader_recompiler][ssa]") {
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block entry{pool}, left{pool}, right{pool}, join{pool};
    entry.AddBranch(&left);
    entry.AddBranch(&right);
    left.AddBranch(&join);
    right.AddBranch(&join);
    entry.SetOrder(0);
    left.SetOrder(1);
    right.SetOrder(2);
    join.SetOrder(3);
    IR::IREmitter left_ir{left}, right_ir{right}, join_ir{join};
    WriteVariables(left_ir, 11, true);
    WriteVariables(right_ir, 22, false);
    const auto first = ReadVariables(join_ir);
    const auto repeated = ReadVariables(join_ir);
    IR::Program program;
    program.blocks = {&entry, &left, &right, &join};
    program.post_order_blocks = {&join, &right, &left, &entry};

    Shader::Optimization::SsaRewritePass(program);

    for (size_t index = 0; index < first.size(); ++index) {
        const auto value = first[index].Resolve();
        REQUIRE(value.IsPhi());
        CHECK(repeated[index].Resolve() == value);
        const auto* phi = value.Inst();
        REQUIRE(phi->NumArgs() == 2);
        CHECK(phi->PhiBlock(0) == &left);
        CHECK(phi->PhiBlock(1) == &right);
        const bool integer = index == 0 || index == 3;
        CHECK(phi->Arg(0).Resolve() == (integer ? IR::Value{u32{11}} : IR::Value{true}));
        CHECK(phi->Arg(1).Resolve() == (integer ? IR::Value{u32{22}} : IR::Value{false}));
    }
}

TEST_CASE("SSA repeated reads preserve an incomplete loop phi",
          "[video_core][shader_recompiler][ssa]") {
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block entry{pool}, header{pool}, body{pool};
    entry.AddBranch(&header);
    header.AddBranch(&body);
    body.AddBranch(&header);
    entry.SetOrder(0);
    header.SetOrder(1);
    body.SetOrder(2);
    IR::IREmitter entry_ir{entry}, header_ir{header}, body_ir{body};
    entry_ir.SetReg(IR::Reg::R0, entry_ir.Imm32(u32{7}));
    const auto first = header_ir.GetReg(IR::Reg::R0);
    const auto repeated = header_ir.GetReg(IR::Reg::R0);
    const auto backedge = body_ir.GetReg(IR::Reg::R0);
    IR::Program program;
    program.blocks = {&entry, &header, &body};
    program.post_order_blocks = {&body, &header, &entry};

    Shader::Optimization::SsaRewritePass(program);

    const auto value = first.Resolve();
    REQUIRE(value.IsPhi());
    CHECK(repeated.Resolve() == value);
    CHECK(backedge.Resolve() == value);
    const auto* phi = value.Inst();
    REQUIRE(phi->NumArgs() == 2);
    CHECK(phi->PhiBlock(0) == &entry);
    CHECK(phi->Arg(0).Resolve() == IR::Value{u32{7}});
    CHECK(phi->PhiBlock(1) == &body);
    CHECK(phi->Arg(1).Resolve() == value);
}

TEST_CASE("SSA copy chains remain shallow and preserve live uses",
          "[video_core][shader_recompiler][ssa]") {
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    IR::IREmitter ir{block};
    const auto source = ir.GetCbuf(ir.Imm32(u32{0}), ir.Imm32(u32{0}));
    ir.SetReg(IR::Reg::R0, source);
    constexpr size_t Copies = 2048;
    std::vector<IR::Value> reads;
    reads.reserve(Copies + 1);
    for (size_t copy = 0; copy < Copies; ++copy) {
        const auto value = ir.GetReg(IR::Reg::R0);
        reads.push_back(value);
        ir.SetReg(IR::Reg::R0, value);
    }
    const auto output = ir.GetReg(IR::Reg::R0);
    reads.push_back(output);
    ir.Reference(output);
    IR::Program program;
    program.blocks = {&block};
    program.post_order_blocks = {&block};

    Shader::Optimization::SsaRewritePass(program);

    size_t total_hops{};
    size_t longest_chain{};
    for (auto value : reads) {
        size_t hops{};
        while (value.IsIdentity()) {
            REQUIRE(hops <= Copies);
            value = value.Inst()->Arg(0);
            ++hops;
        }
        CHECK(value == source);
        total_hops += hops;
        if (hops > longest_chain) {
            longest_chain = hops;
        }
    }
    INFO("Identity hops: total=" << total_hops << ", longest=" << longest_chain);
    CHECK(longest_chain == 1);
    CHECK(total_hops == Copies + 1);
    CHECK(output.Resolve() == source);

    Shader::Optimization::DeadCodeEliminationPass(program);

    CHECK(output.Resolve() == source);
    CHECK(source.Inst()->UseCount() == 1);
    CHECK(output.Inst()->UseCount() == 1);
    CHECK(block.Instructions().size() == 3); // source, final Identity, side-effecting Reference
}

TEST_CASE("SSA copied definitions follow late phi resolution without following later writes",
          "[video_core][shader_recompiler][ssa]") {
    bool trivial{};
    SECTION("Distinct predecessors preserve a live phi") {
        trivial = false;
    }
    SECTION("Equal predecessors resolve the phi after local copies") {
        trivial = true;
    }
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block entry{pool}, left{pool}, right{pool}, join{pool};
    entry.AddBranch(&left);
    entry.AddBranch(&right);
    left.AddBranch(&join);
    right.AddBranch(&join);
    entry.SetOrder(0);
    left.SetOrder(1);
    right.SetOrder(2);
    join.SetOrder(3);
    IR::IREmitter left_ir{left}, right_ir{right}, join_ir{join};
    left_ir.SetReg(IR::Reg::R0, left_ir.Imm32(u32{11}));
    right_ir.SetReg(IR::Reg::R0, right_ir.Imm32(trivial ? u32{11} : u32{22}));
    const auto first = join_ir.GetReg(IR::Reg::R0);
    join_ir.SetReg(IR::Reg::R1, first);
    const auto copied = join_ir.GetReg(IR::Reg::R1);
    join_ir.SetReg(IR::Reg::R0, join_ir.Imm32(u32{99}));
    const auto overwritten = join_ir.GetReg(IR::Reg::R0);
    const auto copied_after_write = join_ir.GetReg(IR::Reg::R1);
    IR::Program program;
    program.blocks = {&entry, &left, &right, &join};
    program.post_order_blocks = {&join, &right, &left, &entry};

    Shader::Optimization::SsaRewritePass(program);

    const auto value = first.Resolve();
    CHECK(copied.Resolve() == value);
    CHECK(copied_after_write.Resolve() == value);
    CHECK(overwritten.Resolve() == IR::Value{u32{99}});
    if (trivial) {
        CHECK(value == IR::Value{u32{11}});
    } else {
        REQUIRE(value.IsPhi());
        const auto* phi = value.Inst();
        REQUIRE(phi->NumArgs() == 2);
        CHECK(phi->PhiBlock(0) == &left);
        CHECK(phi->PhiBlock(1) == &right);
        CHECK(phi->Arg(0).Resolve() == IR::Value{u32{11}});
        CHECK(phi->Arg(1).Resolve() == IR::Value{u32{22}});
    }
}

TEST_CASE("SSA predecessor worklist resets across deep reads of different variable kinds",
          "[video_core][shader_recompiler][ssa]") {
    constexpr size_t BlockCount = 64;
    Shader::ObjectPool<IR::Inst> inst_pool;
    Shader::ObjectPool<IR::Block> block_pool{BlockCount};
    IR::Program program;
    for (size_t index = 0; index < BlockCount; ++index) {
        auto* block = block_pool.Create(inst_pool);
        block->SetOrder(static_cast<u32>(index));
        if (!program.blocks.empty()) {
            program.blocks.back()->AddBranch(block);
        }
        program.blocks.push_back(block);
    }
    for (auto block = program.blocks.rbegin(); block != program.blocks.rend(); ++block) {
        program.post_order_blocks.push_back(*block);
    }
    IR::IREmitter entry_ir{*program.blocks.front()}, tail_ir{*program.blocks.back()};
    WriteVariables(entry_ir, 73, true);
    const auto first = ReadVariables(tail_ir);
    const auto repeated = ReadVariables(tail_ir);
    const auto undefined = tail_ir.GetReg(IR::Reg::R1);

    Shader::Optimization::SsaRewritePass(program);

    for (size_t index = 0; index < first.size(); ++index) {
        const bool integer = index == 0 || index == 3;
        const auto expected = integer ? IR::Value{u32{73}} : IR::Value{true};
        CHECK(first[index].Resolve() == expected);
        CHECK(repeated[index].Resolve() == expected);
    }
    REQUIRE_FALSE(undefined.Resolve().IsImmediate());
    CHECK(undefined.Resolve().Inst()->GetOpcode() == IR::Opcode::UndefU32);
}
