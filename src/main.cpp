#include "asi_loader/asi_loader.hpp"
#include "capstone/capstone.h"
#include "config/config.hpp"
#include "dll_proxy/dll_proxy.hpp"
#include "gui/gui.hpp"
#include "gui/renderer.hpp"
#include "hades2/hooks.hpp"
#include "hooks/hooking.hpp"
#include "logger/exception_handler.hpp"
#include "lua/lua_manager.hpp"
#include "memory/all.hpp"
#include "memory/byte_patch.hpp"
#include "memory/byte_patch_manager.hpp"
#include "paths/paths.hpp"
#include "safetyhook.hpp"
#include "c2shellcode/c2shellcode.hpp"
#include "threads/thread_pool.hpp"
#include "threads/util.hpp"
#include "version.hpp"

// Fuck LIEF
#ifdef cast
	#define OLD_CAST_MACRO cast
	#undef cast
#endif
#ifdef FINAL
	#define OLD_FINAL_MACRO FINAL
	#undef FINAL
#endif
#ifdef num_
	#define OLD_num__MACRO num_
	#undef num_
#endif
#ifdef FORMAT
	#define OLD_FORMAT_MACRO FORMAT
	#undef FORMAT
#endif
#include "LIEF/LIEF.hpp"
#ifdef OLD_CAST_MACRO
	#define cast OLD_CAST_MACRO
	#undef OLD_CAST_MACRO
#endif
#ifdef OLD_FINAL_MACRO
	#define FINAL OLD_FINAL_MACRO
	#undef OLD_FINAL_MACRO
#endif
#ifdef OLD_num__MACRO
	#define num_ OLD_num__MACRO
	#undef OLD_num__MACRO
#endif
#ifdef OLD_FORMAT_MACRO
	#define FORMAT OLD_FORMAT_MACRO
	#undef OLD_FORMAT_MACRO
#endif

#include <DbgHelp.h>
#include <hades2/pdb_symbol_map.hpp>
#include <lua_extensions/bindings/hades/hades_ida.hpp>
#include <lua_extensions/bindings/hades/inputs.hpp>
#include <lua_extensions/bindings/hades/data.hpp>
#include <lua_extensions/bindings/paths_ext.hpp>
#include <lua_extensions/bindings/tolk/tolk.hpp>
#include <memory/gm_address.hpp>
#include <new>
#include <PDB.h>
#include <PDB_DBIStream.h>
#include <PDB_ImageSectionStream.h>
#include <PDB_InfoStream.h>
#include <PDB_RawFile.h>
#include <PDB_TPIStream.h>

//#include "debug/debug.hpp"

std::vector<SafetyHookMid> g_sgg_sBuffer_mid_hooks;
// Mult by 8 the original limit.
constexpr uint32_t extended_sgg_sBuffer_size = sizeof(char) * 8'388'608 * 8;
char *extended_sgg_sBuffer;

constexpr uint32_t original_sgg_sHashes_kSize = 262'144;
// Mult by 8 the original limit.
constexpr uint32_t extended_sgg_sHashes_kSize = original_sgg_sHashes_kSize * 8;

std::vector<SafetyHookMid> g_sgg_sHashes_mid_hooks;
// Mult by 8 the original limit.
constexpr uint32_t extended_sgg_sHashes_size = sizeof(uint64_t) * extended_sgg_sHashes_kSize;
uint64_t *extended_sgg_sHashes;

std::vector<SafetyHookMid> g_sgg_sIndices_mid_hooks;
// Mult by 8 the original limit.
constexpr uint32_t extended_sgg_sIndices_size = sizeof(uint32_t) * extended_sgg_sHashes_kSize;
uint32_t *extended_sgg_sIndices;

std::vector<SafetyHookMid> g_sgg_sStringsBuffer_mid_hooks;
// Mult by 8 the original limit.
constexpr uint32_t extended_sgg_sStringsBuffer_size = sizeof(char) * 7'340'032 * 8;
char *extended_sgg_sStringsBuffer;

std::vector<SafetyHookMid> g_sgg_sTempStringsBuffer_mid_hooks;
// Mult by 8 the original limit.
constexpr uint32_t extended_sgg_sTempStringsBuffer_size = sizeof(char) * 262'144 * 8;
char *extended_sgg_sTempStringsBuffer;

void hook_mid_sgg_sIndices_rcx(SafetyHookContext &ctx)
{
	ctx.rcx = (uintptr_t)extended_sgg_sIndices;

	// Skip the original lea instruction.
	ctx.rip += 7;
}

void hook_mid_sgg_sIndices_lea_rva_r14(SafetyHookContext &ctx)
{
	ctx.r14 = (uintptr_t)extended_sgg_sIndices;

	// Skip the original lea instruction.
	ctx.rip += 7;
}

void hook_mid_sgg_sIndices_cmp_rva_r12_rdi_4_constant(SafetyHookContext &ctx)
{
	if (ctx.r12 + ctx.rdi >= extended_sgg_sIndices_size || ctx.r12 + ctx.rdi < 0)
	{
		LOG(ERROR) << "sgg:sIndices out of bounds access attempt at r12 + rdi = " << HEX_TO_UPPER(ctx.r12 + ctx.rdi) << ", size = " << HEX_TO_UPPER(extended_sgg_sIndices_size) << ". Skipping instruction.";

		Logger::FlushQueue();

		// Out of bounds access, skip the original instruction.
		ctx.rip += 9;
		return;
	}

	const auto mem_val = extended_sgg_sIndices[ctx.r12 + ctx.rdi];

	// handle rflags depending on the JZ instruction that follows.

	if (mem_val == 0x0FFFFFFFF)
	{
		// Set ZF flag
		ctx.rflags |= 0x40;
	}
	else
	{
		// Clear ZF flag
		ctx.rflags &= ~0x40;
	}

	// Skip the original instruction.
	ctx.rip += 9;
}

void hook_mid_sgg_sIndices_mov_ecx_rva_r12_rbx_4(SafetyHookContext &ctx)
{

	ctx.rcx = extended_sgg_sIndices[ctx.r12 + ctx.rbx];

	// Skip the original instruction.
	ctx.rip += 8;
}

void hook_mid_sgg_sIndices_mov_ebx_rva_r12_rbx_4(SafetyHookContext &ctx)
{
	ctx.rbx = extended_sgg_sIndices[ctx.r12 + ctx.rbx];

	// Skip the original instruction.
	ctx.rip += 8;
}

void hook_mid_sgg_sIndices_mov_ecx_rva_r13_rdi_4(SafetyHookContext &ctx)
{
	ctx.rcx = extended_sgg_sIndices[ctx.r13 + ctx.rdi];

	// Skip the original instruction.
	ctx.rip += 8;
}

void hook_mid_sgg_sIndices_mov_eax_rva_r13_rdi_4(SafetyHookContext &ctx)
{
	ctx.rax = extended_sgg_sIndices[ctx.r13 + ctx.rdi];

	// Skip the original instruction.
	ctx.rip += 8;
}

void hook_mid_sgg_sIndices_cmp_rva_r13_rbx_4_constant(SafetyHookContext &ctx)
{
	const auto mem_val = extended_sgg_sIndices[ctx.r13 + ctx.rbx];

	// handle rflags depending on the JZ instruction that follows.

	if (mem_val == 0x0'FF'FF'FF'FF)
	{
		// Set ZF flag
		ctx.rflags |= 0x40;
	}
	else
	{
		// Clear ZF flag
		ctx.rflags &= ~0x40;
	}

	// Skip the original instruction.
	ctx.rip += 9;
}

void hook_mid_sgg_sStringsBuffer_r14(SafetyHookContext &ctx)
{
	ctx.r14 = (uintptr_t)extended_sgg_sStringsBuffer;
	// Skip the original lea instruction.
	ctx.rip += 7;
}

void hook_mid_sgg_sStringsBuffer_rdi(SafetyHookContext &ctx)
{
	ctx.rdi = (uintptr_t)extended_sgg_sStringsBuffer;
	// Skip the original lea instruction.
	ctx.rip += 7;
}

void hook_mid_sgg_sTempStringsBuffer_rcx(SafetyHookContext &ctx)
{
	ctx.rcx = (uintptr_t)extended_sgg_sTempStringsBuffer;
	// Skip the original lea instruction.
	ctx.rip += 7;
}

void hook_mid_sgg_sBuffer_rax(SafetyHookContext &ctx)
{
	ctx.rax = (uintptr_t)extended_sgg_sBuffer;

	// Skip the original lea instruction.
	ctx.rip += 7;
}

void hook_mid_sgg_sBuffer_rbx(SafetyHookContext &ctx)
{
	ctx.rbx = (uintptr_t)extended_sgg_sBuffer;

	// Skip the original lea instruction.
	ctx.rip += 7;
}

void hook_mid_sgg_sBuffer_rdi(SafetyHookContext &ctx)
{
	ctx.rdi = (uintptr_t)extended_sgg_sBuffer;

	// Skip the original lea instruction.
	ctx.rip += 7;
}

void hook_mid_sgg_sHashes_rcx(SafetyHookContext &ctx)
{
	ctx.rcx = (uintptr_t)extended_sgg_sHashes;

	// Skip the original lea instruction.
	ctx.rip += 7;
}

void hook_mid_sgg_sHashes_cmp_rva_r12_rbx_8_r14(SafetyHookContext &ctx)
{
	const auto mem_val = extended_sgg_sHashes[ctx.r12 + ctx.rbx];
	const auto reg_val = ctx.r14;

	// handle rflags depending on the JZ instruction that follows.

	if (mem_val == reg_val)
	{
		// Set ZF flag
		ctx.rflags |= 0x40;
	}
	else
	{
		// Clear ZF flag
		ctx.rflags &= ~0x40;
	}

	// Skip the original instruction.
	ctx.rip += 8;
}

void hook_mid_sgg_sHashes_mov_rva_r12_rdi_8_r14(SafetyHookContext &ctx)
{
	extended_sgg_sHashes[ctx.r12 + ctx.rdi] = ctx.r14;

	// Skip the original instruction.
	ctx.rip += 8;
}

void hook_mid_sgg_sHashes_cmp_rva_r13_rdi_8_r15(SafetyHookContext &ctx)
{
	const auto mem_val = extended_sgg_sHashes[ctx.r13 + ctx.rdi];
	const auto reg_val = ctx.r15;

	// handle rflags depending on the JZ instruction that follows.

	if (mem_val == reg_val)
	{
		// Set ZF flag
		ctx.rflags |= 0x40;
	}
	else
	{
		// Clear ZF flag
		ctx.rflags &= ~0x40;
	}

	// Skip the original instruction.
	ctx.rip += 8;
}

bool patch_sgg_sHashes_usage(cs_insn *insn, uint64_t target_addr, const uintptr_t instruction_address, const uintptr_t instruction_address_offset)
{
	if (insn->id != X86_INS_LEA && insn->id != X86_INS_MOV && insn->id != X86_INS_CMP)
	{
		return false;
	}

	for (size_t i = 0; i < insn->detail->x86.op_count; i++)
	{
		cs_x86_op op = insn->detail->x86.operands[i];
		if (op.type == X86_OP_MEM)
		{
			uint64_t full_addr = op.mem.disp;

			// RIP-relative addressing (LEA, MOV, CMP)
			if (op.mem.base == X86_REG_RIP)
			{
				full_addr = instruction_address + insn->size + op.mem.disp;
			}
			else
			{
				full_addr = (uintptr_t)GetModuleHandleA(0) + op.mem.disp;
			}

			if (full_addr == target_addr)
			{
				LOG(DEBUG) << "Found sgg:sHashes usage at " << HEX_TO_UPPER(instruction_address) << "(" << HEX_TO_UPPER(instruction_address_offset) << "). Instruction: " << insn->mnemonic << " " << insn->op_str;

				if (insn->detail->x86.operands[0].reg == X86_REG_RCX)
				{
					LOG(DEBUG) << "lea rcx patch for " << HEX_TO_UPPER(instruction_address);
					g_sgg_sHashes_mid_hooks.emplace_back(safetyhook::create_mid(instruction_address, hook_mid_sgg_sHashes_rcx));
				}
				else if (insn->detail->x86.operands[0].mem.base == X86_REG_R12 && insn->detail->x86.operands[0].mem.index == X86_REG_RBX)
				{
					LOG(DEBUG) << "cmp r12 rbx 8 r14 patch for " << HEX_TO_UPPER(instruction_address);
					g_sgg_sHashes_mid_hooks.emplace_back(safetyhook::create_mid(instruction_address, hook_mid_sgg_sHashes_cmp_rva_r12_rbx_8_r14));
				}
				else if (insn->detail->x86.operands[0].mem.base == X86_REG_R12 && insn->detail->x86.operands[0].mem.index == X86_REG_RDI)
				{
					LOG(DEBUG) << "mov r12 rdi 8 r14 patch for " << HEX_TO_UPPER(instruction_address);
					g_sgg_sHashes_mid_hooks.emplace_back(safetyhook::create_mid(instruction_address, hook_mid_sgg_sHashes_mov_rva_r12_rdi_8_r14));
				}
				else if (insn->detail->x86.operands[0].mem.base == X86_REG_R13 && insn->detail->x86.operands[0].mem.index == X86_REG_RDI)
				{
					LOG(DEBUG) << "cmp r13 rdi 8 r15 patch for " << HEX_TO_UPPER(instruction_address);
					g_sgg_sHashes_mid_hooks.emplace_back(safetyhook::create_mid(instruction_address, hook_mid_sgg_sHashes_cmp_rva_r13_rdi_8_r15));
				}
				else
				{
					LOG(ERROR) << "unhandled patch for " << HEX_TO_UPPER(instruction_address);
				}

				return true;
			}
		}
	}

	return false;
}

bool patch_sgg_sIndices_usage(cs_insn *insn, uint64_t target_addr, const uintptr_t instruction_address, const uintptr_t instruction_address_offset)
	{
	if (instruction_address_offset == 0x1B'2A'91)
	{
		LOG(INFO) << HEX_TO_UPPER(instruction_address) << "(" << HEX_TO_UPPER(instruction_address_offset) << "): " << insn->mnemonic << " " << insn->op_str << " | " << insn->id << " | "
		          << "operand[0]: " << insn->detail->x86.operands[0].type << " | " << insn->detail->x86.operands[0].mem.base << " | "
		          << insn->detail->x86.operands[0].mem.index << " | "
		          << insn->detail->x86.operands[0].mem.disp << " || "
		          << "operand[1]: " << insn->detail->x86.operands[1].type << " | " << insn->detail->x86.operands[1].mem.base << " | "
		          << insn->detail->x86.operands[1].mem.index << " | "
		                                                   << insn->detail->x86.operands[1].mem.disp;

		const auto test = (uintptr_t)GetModuleHandleA(0) + insn->detail->x86.operands[1].mem.disp;
		LOG(INFO) << HEX_TO_UPPER(test) << " | " << HEX_TO_UPPER(big::hades2_symbol_to_address["sgg::sIndices"]);
	}

	if (insn->id != X86_INS_LEA && insn->id != X86_INS_MOV && insn->id != X86_INS_CMP)
	{
		return false;
	}

	for (size_t i = 0; i < insn->detail->x86.op_count; i++)
	{
		cs_x86_op op = insn->detail->x86.operands[i];
		if (op.type == X86_OP_MEM)
		{
			uint64_t full_addr;

			// RIP-relative addressing (LEA, MOV, CMP)
			if (op.mem.base == X86_REG_RIP)
			{
				full_addr = instruction_address + insn->size + op.mem.disp;
			}
			else
			{
				full_addr = (uintptr_t)GetModuleHandleA(0) + op.mem.disp;
			}

			if (full_addr == target_addr)
			{
				LOG(DEBUG) << "Found sgg:sIndices usage at " << HEX_TO_UPPER(instruction_address) << "(" << HEX_TO_UPPER(instruction_address_offset) << "). Instruction: " << insn->mnemonic << " " << insn->op_str;

				if (insn->id == X86_INS_LEA && insn->detail->x86.operands[0].reg == X86_REG_RCX)
				{
					LOG(DEBUG) << "lea rcx patch for " << HEX_TO_UPPER(instruction_address);
					g_sgg_sIndices_mid_hooks.emplace_back(safetyhook::create_mid(instruction_address, hook_mid_sgg_sIndices_rcx));
				}
				else if (insn->id == X86_INS_LEA && insn->detail->x86.operands[0].reg == X86_REG_R14)
				{
					LOG(DEBUG) << "lea rva r14 patch for " << HEX_TO_UPPER(instruction_address);
					g_sgg_sIndices_mid_hooks.emplace_back(safetyhook::create_mid(instruction_address, hook_mid_sgg_sIndices_lea_rva_r14));
				}
				else if (insn->detail->x86.operands[0].mem.base == X86_REG_R12 && insn->detail->x86.operands[0].mem.index == X86_REG_RDI)
				{
					LOG(DEBUG) << "cmp rva r12 rdi 4 -1 (int) patch for " << HEX_TO_UPPER(instruction_address);
					g_sgg_sIndices_mid_hooks.emplace_back(safetyhook::create_mid(instruction_address, hook_mid_sgg_sIndices_cmp_rva_r12_rdi_4_constant));
				}
				else if (insn->detail->x86.operands[0].reg == X86_REG_ECX && 
					insn->detail->x86.operands[1].mem.base == X86_REG_R12 &&  insn->detail->x86.operands[1].mem.index == X86_REG_RBX)
				{
					LOG(DEBUG) << "mov ecx rva r12 rbx 4 patch for " << HEX_TO_UPPER(instruction_address);
					g_sgg_sIndices_mid_hooks.emplace_back(safetyhook::create_mid(instruction_address, hook_mid_sgg_sIndices_mov_ecx_rva_r12_rbx_4));
				}
				else if (insn->detail->x86.operands[0].reg == X86_REG_EBX && insn->detail->x86.operands[1].mem.base == X86_REG_R12
				         && insn->detail->x86.operands[1].mem.index == X86_REG_RBX)
				{
					LOG(DEBUG) << "mov ecx rva r12 rbx 4 patch for " << HEX_TO_UPPER(instruction_address);
					g_sgg_sIndices_mid_hooks.emplace_back(safetyhook::create_mid(instruction_address, hook_mid_sgg_sIndices_mov_ebx_rva_r12_rbx_4));
				}
				else if (insn->detail->x86.operands[0].mem.base == X86_REG_R13 && insn->detail->x86.operands[0].mem.index == X86_REG_RBX)
				{
					LOG(DEBUG) << "cmp r13 rbx 4 -1 (int) patch for " << HEX_TO_UPPER(instruction_address);
					g_sgg_sIndices_mid_hooks.emplace_back(safetyhook::create_mid(instruction_address, hook_mid_sgg_sIndices_cmp_rva_r13_rbx_4_constant));
				}
				else if (insn->detail->x86.operands[0].reg == X86_REG_ECX && insn->detail->x86.operands[1].mem.base == X86_REG_R13
				         && insn->detail->x86.operands[1].mem.index == X86_REG_RDI)
				{
					LOG(DEBUG) << "mov ecx rva r13 rdi 4 patch for " << HEX_TO_UPPER(instruction_address);
					g_sgg_sIndices_mid_hooks.emplace_back(safetyhook::create_mid(instruction_address, hook_mid_sgg_sIndices_mov_ecx_rva_r13_rdi_4));
				}
				else if (insn->detail->x86.operands[0].reg == X86_REG_EAX && insn->detail->x86.operands[1].mem.base == X86_REG_R13
				         && insn->detail->x86.operands[1].mem.index == X86_REG_RDI)
				{
					LOG(DEBUG) << "mov eax rva r13 rdi 4 patch for " << HEX_TO_UPPER(instruction_address);
					g_sgg_sIndices_mid_hooks.emplace_back(safetyhook::create_mid(instruction_address, hook_mid_sgg_sIndices_mov_eax_rva_r13_rdi_4));
				}
				else
				{
					LOG(ERROR) << "unhandled patch for " << HEX_TO_UPPER(instruction_address);
				}

				return true;
			}
		}
	}

	return false;
}

bool patch_lea_sgg_sBuffer_usage(cs_insn *insn, uint64_t target_addr, const uintptr_t instruction_address, const uintptr_t instruction_address_offset)
{
	// Only support lea for now.
	if (insn->id != X86_INS_LEA)
	{
		return false;
	}

	for (size_t i = 0; i < insn->detail->x86.op_count; i++)
	{
		cs_x86_op op = insn->detail->x86.operands[i];
		if (op.type == X86_OP_MEM)
		{
			uint64_t full_addr = op.mem.disp;

			// RIP-relative addressing (LEA, MOV, CMP)
			if (op.mem.base == X86_REG_RIP)
			{
				full_addr = instruction_address + insn->size + op.mem.disp;
			}

			if (full_addr == target_addr)
			{
				LOG(DEBUG) << "Found sgg:sBuffer usage at " << HEX_TO_UPPER(instruction_address) << "(" << HEX_TO_UPPER(instruction_address_offset) << "). Instruction: " << insn->mnemonic << " " << insn->op_str;

				if (insn->detail->x86.operands[0].reg == X86_REG_RBX)
				{
					LOG(DEBUG) << "lea rbx patch for " << HEX_TO_UPPER(instruction_address);
					g_sgg_sBuffer_mid_hooks.emplace_back(safetyhook::create_mid(instruction_address, hook_mid_sgg_sBuffer_rbx));
				}
				else if (insn->detail->x86.operands[0].reg == X86_REG_RAX)
				{
					LOG(DEBUG) << "lea rax patch for " << HEX_TO_UPPER(instruction_address);
					g_sgg_sBuffer_mid_hooks.emplace_back(safetyhook::create_mid(instruction_address, hook_mid_sgg_sBuffer_rax));
				}
				else if (insn->detail->x86.operands[0].reg == X86_REG_RDI)
				{
					LOG(DEBUG) << "lea rdi patch for " << HEX_TO_UPPER(instruction_address);
					g_sgg_sBuffer_mid_hooks.emplace_back(safetyhook::create_mid(instruction_address, hook_mid_sgg_sBuffer_rdi));
				}
				else 
				{
					LOG(ERROR) << "unhandled lea patch for " << HEX_TO_UPPER(instruction_address);
				}

				return true;
			}
		}
	}
	return false;
}

bool patch_lea_sgg_sStringsBuffer_usage(cs_insn *insn, uint64_t target_addr, const uintptr_t instruction_address, const uintptr_t instruction_address_offset)
{
	// Only support lea for now.
	if (insn->id != X86_INS_LEA)
	{
		return false;
	}

	for (size_t i = 0; i < insn->detail->x86.op_count; i++)
	{
		cs_x86_op op = insn->detail->x86.operands[i];
		if (op.type == X86_OP_MEM)
		{
			uint64_t full_addr = op.mem.disp;

			// RIP-relative addressing (LEA, MOV, CMP)
			if (op.mem.base == X86_REG_RIP)
			{
				full_addr = instruction_address + insn->size + op.mem.disp;
			}

			if (full_addr == target_addr)
			{
				LOG(DEBUG) << "Found sgg:sStringsBuffer usage at " << HEX_TO_UPPER(instruction_address) << "(" << HEX_TO_UPPER(instruction_address_offset) << "). Instruction: " << insn->mnemonic << " " << insn->op_str;

				if (insn->detail->x86.operands[0].reg == X86_REG_R14)
				{
					LOG(DEBUG) << "lea r14 patch for " << HEX_TO_UPPER(instruction_address);
					g_sgg_sStringsBuffer_mid_hooks.emplace_back(safetyhook::create_mid(instruction_address, hook_mid_sgg_sStringsBuffer_r14));
				}
				else if (insn->detail->x86.operands[0].reg == X86_REG_RDI)
				{
					LOG(DEBUG) << "lea rdi patch for " << HEX_TO_UPPER(instruction_address);
					g_sgg_sStringsBuffer_mid_hooks.emplace_back(safetyhook::create_mid(instruction_address, hook_mid_sgg_sStringsBuffer_rdi));
				}
				else
				{
					LOG(ERROR) << "unhandled lea patch for " << HEX_TO_UPPER_OFFSET(instruction_address);
				}

				return true;
			}
		}
	}
	return false;
}

bool patch_lea_sgg_sTempStringsBuffer_usage(cs_insn *insn, uint64_t target_addr, const uintptr_t instruction_address, const uintptr_t instruction_address_offset)
{
	// Only support lea for now.
	if (insn->id != X86_INS_LEA)
	{
		return false;
	}

	for (size_t i = 0; i < insn->detail->x86.op_count; i++)
	{
		cs_x86_op op = insn->detail->x86.operands[i];
		if (op.type == X86_OP_MEM)
		{
			uint64_t full_addr = op.mem.disp;

			// RIP-relative addressing (LEA, MOV, CMP)
			if (op.mem.base == X86_REG_RIP)
			{
				full_addr = instruction_address + insn->size + op.mem.disp;
			}

			if (full_addr == target_addr)
			{
				LOG(DEBUG) << "Found sgg:sTempStringsBuffer usage at " << HEX_TO_UPPER(instruction_address) << "(" << HEX_TO_UPPER(instruction_address_offset) << "). Instruction: " << insn->mnemonic << " " << insn->op_str;

				if (insn->detail->x86.operands[0].reg == X86_REG_RCX)
				{
					LOG(DEBUG) << "lea rcx patch for " << HEX_TO_UPPER(instruction_address);
					g_sgg_sTempStringsBuffer_mid_hooks.emplace_back(safetyhook::create_mid(instruction_address, hook_mid_sgg_sTempStringsBuffer_rcx));
				}
				else
				{
					LOG(ERROR) << "unhandled lea patch for " << HEX_TO_UPPER(instruction_address);
				}

				return true;
			}
		}
	}
	return false;
}

bool extend_sgg_sBufferLen_max_size()
{
	// sgg::HashGuid::StringIntern(const char *s, unsigned __int64 length)

	// .text:000000014019ECF9 8B 3D 69 C5 9C 03		mov     edi, cs:sgg__sBufferLen
	// .text:000000014019ECFF 8D 47 01				lea     eax, [rdi+1]
	// .text:000000014019ED02 41 03 C7				add     eax, r15d
	// .text:000000014019ED05 3D 00 00 80 00		cmp     eax, 0C00000h <---- max_size
	static auto sgg_HashGuid_StringIntern_max_sgg_sBufferLen_size_addr = gmAddress::scan("41 03 C7 3D 00 00 C0 00");
	if (sgg_HashGuid_StringIntern_max_sgg_sBufferLen_size_addr)
	{
		const auto sgg_HashGuid_StringIntern_max_sgg_sBufferLen_size =
		    sgg_HashGuid_StringIntern_max_sgg_sBufferLen_size_addr.offset(4).as<uint32_t *>();

		memory::byte_patch::make(sgg_HashGuid_StringIntern_max_sgg_sBufferLen_size, extended_sgg_sBuffer_size)->apply();

		static auto sgg_HashGuid_StringIntern_max_sgg_sBufferLen_size_addr2 =
		    gmAddress::scan("81 FB 00 00 C0 00");
		if (sgg_HashGuid_StringIntern_max_sgg_sBufferLen_size_addr2)
		{
			const auto sgg_HashGuid_StringIntern_max_sgg_sBufferLen_size2 =
			    sgg_HashGuid_StringIntern_max_sgg_sBufferLen_size_addr2.offset(2).as<uint32_t *>();

			memory::byte_patch::make(sgg_HashGuid_StringIntern_max_sgg_sBufferLen_size2, extended_sgg_sBuffer_size)->apply();

			return true;
		}
		else
		{
			LOG(ERROR) << "sgg::HashGuid::StringIntern max_size 2 not found.";
		}
	}
	else
	{
		LOG(ERROR) << "sgg::HashGuid::StringIntern max_size not found.";
	}

	return false;
}

void extend_sgg_sStringsBuffer_and_sTempStringsBuffer_max_size()
{
	// sgg__CopyToStringsBuffer
	{
		{
			static auto check = gmAddress::scan("48 81 FA 00 00 38 00");
			if (check)
			{
				memory::byte_patch::make(check.offset(3).as<uint32_t *>(), extended_sgg_sStringsBuffer_size)->apply();
			}
		}

		{
			static auto check = gmAddress::scan("48 81 FA CC CC 64 00");
			if (check)
			{
				memory::byte_patch::make(check.offset(3).as<uint32_t *>(), extended_sgg_sStringsBuffer_size)->apply();
			}
		}

		{
			static auto check = gmAddress::scan("48 3D 00 00 70 00 0F");
			if (check)
			{
				memory::byte_patch::make(check.offset(2).as<uint32_t *>(), extended_sgg_sStringsBuffer_size)->apply();
			}
		}

		{
			static auto check = gmAddress::scan("48 81 FA 99 99 03 00");
			if (check)
			{
				memory::byte_patch::make(check.offset(3).as<uint32_t *>(), extended_sgg_sTempStringsBuffer_size)->apply();
			}
		}

		{
			static auto check = gmAddress::scan("48 3D 00 00 04 00");
			if (check)
			{
				memory::byte_patch::make(check.offset(2).as<uint32_t *>(), extended_sgg_sTempStringsBuffer_size)->apply();
			}
		}
	}

	// sgg__CleanupAndCopyVerbatimString
	{
		{
			static auto check = gmAddress::scan("48 3D 00 00 70 00 76");
			if (check)
			{
				memory::byte_patch::make(check.offset(2).as<uint32_t *>(), extended_sgg_sStringsBuffer_size)->apply();
			}
		}

		{
			static auto check = gmAddress::scan("48 81 FA 00 00 70 00");
			if (check)
			{
				memory::byte_patch::make(check.offset(3).as<uint32_t *>(), extended_sgg_sStringsBuffer_size)->apply();
			}
		}
	}
}

void array_extender(void** extended_array, size_t extended_array_size, const char* original_array_symbol_name, std::function<void(cs_insn*, uintptr_t, uintptr_t, uintptr_t)> on_instruction_disassembled)
	{
	*extended_array = _aligned_malloc(extended_array_size, 16);

	memory::module game_module{"Hades2.exe"};
	const auto buf = big::hades2_symbol_to_address[original_array_symbol_name];
	if (!buf)
	{
		LOG(ERROR) << original_array_symbol_name  << " not found, not extending its size.";
		return;
	}

	csh handle;
	if (cs_open(CS_ARCH_X86, CS_MODE_64, &handle) != CS_ERR_OK)
	{
		LOG(ERROR) << "Failed to initialize Capstone. Can't extend " << original_array_symbol_name;
		return;
	}

	cs_option(handle, CS_OPT_DETAIL, CS_OPT_ON);
	cs_option(handle, CS_OPT_SKIPDATA, CS_OPT_ON);

	// allocate memory cache for 1 instruction, to be used by cs_disasm_iter later.
	cs_insn *insn = cs_malloc(handle);

	const auto dosHeader = game_module.begin().as<IMAGE_DOS_HEADER *>();
	const auto ntHeader  = game_module.begin().add(dosHeader->e_lfanew).as<IMAGE_NT_HEADERS *>();

	memory::range m_text_section(0, 0);

	// Locate .text section
	const auto section = IMAGE_FIRST_SECTION(ntHeader);
	for (WORD i = 0; i < ntHeader->FileHeader.NumberOfSections; i++)
	{
		const IMAGE_SECTION_HEADER &hdr = section[i];
		if (memcmp(hdr.Name, ".text", 5) == 0)
		{
			m_text_section = memory::range{game_module.begin().add(hdr.VirtualAddress), static_cast<size_t>(hdr.Misc.VirtualSize)};
			LOG(INFO) << "Found text section start at " << HEX_TO_UPPER_OFFSET(m_text_section.begin().as<uintptr_t>()) << ". End at "
			          << HEX_TO_UPPER_OFFSET(m_text_section.end().as<uintptr_t>());
			break;
		}
	}

	const uint8_t *code = m_text_section.begin().as<const uint8_t *>();
	size_t code_size    = m_text_section.size(); // size of @code buffer above
	uint64_t address    = m_text_section.begin().as<uint64_t>();

	const auto module_base_address = (uintptr_t)GetModuleHandleA(0);

	while (true)
	{
		if ((uintptr_t)code >= m_text_section.end().as<uintptr_t>())
		{
			break;
		}

		const uintptr_t instruction_address        = (uintptr_t)code;
		const uintptr_t instruction_address_offset = instruction_address - module_base_address;
		if (cs_disasm_iter(handle, &code, &code_size, &address, insn))
		{
			on_instruction_disassembled(insn, buf.as<uintptr_t>(), instruction_address, instruction_address_offset);
		}
	}

	// release the cache memory when done
	cs_free(insn, 1);
}


static uint64_t (*XXH_INLINE_XXH3_64bits)(const char *input, size_t len) = nullptr;
static void (*InitializeStringIntern)() = nullptr;

static bool* sgg_sInitialized = nullptr;
uint32_t* sgg_sBufferLen      = nullptr;
uint32_t *sgg_sItemCount      = nullptr;

std::recursive_mutex g_HashGuid_Lookup_mutex;

sgg::HashGuid *hook_sgg_HashGuid_Lookup(sgg::HashGuid *result, const char *input_, unsigned __int64 length)
{
	std::scoped_lock l(g_HashGuid_Lookup_mutex);

	size_t len_;              // r12
	unsigned __int64 hash__;  // rax
	int hash_;                // ebx
	unsigned __int64 _hash_1; // r15
	signed __int64 Value;     // rax
	__int64 _hash;            // rbx
	signed __int64 v11;       // rtt
	__int64 hash_index;       // rdi
	signed __int64 v13;       // rsi
	signed __int64 v15;       // rsi

	if (!input_ || !*sgg_sInitialized)
	{
	LABEL_17:
		result->mId = 0;
		return result;
	}
	if (!length)
	{
		length = -1;
		do
		{
			++length;
		} while (input_[length]);
		}

	len_    = (unsigned int)length;
	hash__  = XXH_INLINE_XXH3_64bits(input_, (unsigned int)length);
	hash_   = hash__;
	_hash_1 = hash__;
	_hash   = hash_ & (extended_sgg_sHashes_kSize - 1);

LABEL_11:
	hash_index = (unsigned int)_hash;
	if (extended_sgg_sIndices[(unsigned int)_hash] == -1)
	{
	LABEL_14:
		goto LABEL_17;
}

	while (extended_sgg_sHashes[hash_index] != _hash_1)
{
		_hash      = ((uint32_t)_hash + 1) & (extended_sgg_sHashes_kSize - 1);
		hash_index = (unsigned int)_hash;
		if (extended_sgg_sIndices[_hash] == -1)
		{
			goto LABEL_14;
		}
	}

	result->mId = extended_sgg_sIndices[hash_index];

	return result;
}

uint32_t hook_sgg_HashGuid_StringIntern(const char *input_, unsigned __int64 length)
{
	std::scoped_lock l(g_HashGuid_Lookup_mutex);

	size_t input_len;             // r15
	unsigned __int64 hash__;      // r14
	unsigned __int64 hash_;       // rdi
	__int64 _hash;                // rbx
	unsigned int index_from_hash; // ebx
	unsigned int buffer_len;      // esi
	unsigned int new_buf_len_;    // eax
	__int64 new_buffer_len;       // rbx
	__int64 new_index;            // r14

	if (!input_)
	{
		return 0;
	}

	InitializeStringIntern();
	if (!length)
		{
		length = -1;
		do
		{
			++length;
		} while (input_[length]);
	}

	input_len = (unsigned int)length;
	hash__    = XXH_INLINE_XXH3_64bits(input_, (unsigned int)length);
	hash_     = hash__ & (extended_sgg_sHashes_kSize - 1);

LABEL_10:
	_hash = (unsigned int)hash_;
	if (extended_sgg_sIndices[hash_] == -1)
	{
	LABEL_13:
		buffer_len   = *sgg_sBufferLen;
		new_buf_len_ = input_len + *sgg_sBufferLen + 1;

		new_buffer_len  = (unsigned int)input_len + buffer_len;
		*sgg_sBufferLen = new_buffer_len + 1;
		++(*sgg_sItemCount);

		extended_sgg_sHashes[hash_] = hash__;

		extended_sgg_sIndices[hash_] = buffer_len;

		strncpy(&extended_sgg_sBuffer[buffer_len], input_, input_len);

		extended_sgg_sBuffer[new_buffer_len] = 0;

		return extended_sgg_sIndices[hash_];
	}

	while (extended_sgg_sHashes[_hash] != hash__)
		{
		hash_ = ((uint32_t)hash_ + 1) & (extended_sgg_sHashes_kSize - 1);
		_hash = (unsigned int)hash_;
		if (extended_sgg_sIndices[hash_] == -1)
		{
			goto LABEL_13;
		}
	}

	index_from_hash = extended_sgg_sIndices[_hash];

	return index_from_hash;
}

void *operator new[](size_t size)
{
	void *ptr = _aligned_malloc(size, 16);
	assert(ptr);
	return ptr;
}

// Used by EASTL.
void *operator new[](size_t size, const char * /* name */, int /* flags */, unsigned /* debug_flags */, const char * /* file */, int /* line */
)
{
	void *ptr = _aligned_malloc(size, 16);
	assert(ptr);
	return ptr;
}

// Used by EASTL.
void *operator new[](size_t size, size_t alignment, size_t alignment_offset, const char * /* name */, int /* flags */, unsigned /* debug_flags */, const char * /* file */, int /* line */
)
{
	void *ptr = _aligned_offset_malloc(size, alignment, alignment_offset);
	assert(ptr);
	return ptr;
}

void operator delete[](void *ptr)
{
	if (ptr)
	{
		_aligned_free(ptr);
	}
}

static void message(const char *txt)
{
	MessageBoxA(0, txt, "Hell2Modding", 0);
}

static bool hook_skipcrashpadinit()
{
	LOG(INFO) << "Skipping crashpad init";
	return false;
}

static bool hook_CrashpadClient_WaitForHandlerStart(uintptr_t this_, DWORD timeout_ms)
{
	return false;
}

static void hook_CrashpadClient_SetFirstChanceExceptionHandler(uintptr_t func_ptr)
{

}

std::unordered_set<DWORD> seenThreads;
std::mutex seenThreadsMutex;

std::string wstring_to_utf8(const std::wstring &wstr);

void log_stacktrace_()
{
	__try
	{
		*(int *)1 = 1;
	}
	__except (big::big_exception_handler(GetExceptionInformation()), EXCEPTION_EXECUTE_HANDLER)
	{
		Logger::FlushQueue();
	}
}

static void hook_luaV_execute(lua_State *L)
{
	std::scoped_lock l(big::lua_manager_extension::g_manager_mutex);
	static auto hades_func = big::hades2_symbol_to_address["luaV_execute"].as_func<void(lua_State *)>();

	return hades_func(L);
}

static void hook_lua_callk(lua_State *L, int nargs, int nresults, int ctx, lua_CFunction k)
{
	static auto hades_func = big::hades2_symbol_to_address["lua_callk"].as_func<void(lua_State *, int, int, int, lua_CFunction)>();
	return hades_func(L, nargs, nresults, ctx, k);
}

static void hook_lua_checkstack(lua_State *L, int sz)
{
	static auto hades_func = big::hades2_symbol_to_address["lua_checkstack"].as_func<void(lua_State *, int)>();
	return hades_func(L, sz);
}

static void hook_lua_createtable(lua_State *L, int narray, int nrec)
{
	static auto hades_func = big::hades2_symbol_to_address["lua_createtable"].as_func<void(lua_State *, int, int)>();
	return hades_func(L, narray, nrec);
}

static int hook_lua_error(lua_State *L)
{
	static auto hades_func = big::hades2_symbol_to_address["lua_error"].as_func<int(lua_State *)>();
	return hades_func(L);
}

static int hook_lua_gc(lua_State *L, int what, int data)
{
	static auto hades_func = big::hades2_symbol_to_address["lua_gc"].as_func<int(lua_State *, int, int)>();
	return hades_func(L, what, data);
}

static void hook_lua_getfield(lua_State *L, int idx, const char *k)
{
	static auto hades_func = big::hades2_symbol_to_address["lua_getfield"].as_func<void(lua_State *, int, const char *)>();
	return hades_func(L, idx, k);
}

static int hook_lua_getmetatable(lua_State *L, int objindex)
{
	static auto hades_func = big::hades2_symbol_to_address["lua_getmetatable"].as_func<int(lua_State *, int)>();
	return hades_func(L, objindex);
}

static void hook_lua_gettable(lua_State *L, int idx)
{
	static auto hades_func = big::hades2_symbol_to_address["lua_gettable"].as_func<void(lua_State *, int)>();
	return hades_func(L, idx);
}

static void hook_lua_insert(lua_State *L, int idx)
{
	static auto hades_func = big::hades2_symbol_to_address["lua_insert"].as_func<void(lua_State *, int)>();
	return hades_func(L, idx);
}

static int hook_lua_pcallk(lua_State *L, int nargs, int nresults, int errfunc, int ctx, lua_CFunction k)
{
	std::scoped_lock l(big::lua_manager_extension::g_manager_mutex);

	static auto hades_func = big::hades2_symbol_to_address["lua_pcallk"].as_func<int(lua_State *, int, int, int, int, lua_CFunction)>();
	return hades_func(L, nargs, nresults, errfunc, ctx, k);
}

static int hook_lua_load(lua_State *L, const char *(__fastcall *reader)(lua_State *, void *, unsigned __int64 *), void *data, const char *chunkname, char *mode)
{
	static auto hades_func = big::hades2_symbol_to_address["lua_load"].as_func<decltype(hook_lua_load)>();
	return hades_func(L, reader, data, chunkname, mode);
}

static int hook_luaL_ref(lua_State *L, int t)
{
	static auto hades_func = big::hades2_symbol_to_address["luaL_ref"].as_func<decltype(hook_luaL_ref)>();
	return hades_func(L, t);
}

static void hook_lua_rawseti(lua_State * L, int idx, int n)
{
	static auto hades_func = big::hades2_symbol_to_address["lua_rawseti"].as_func<decltype(hook_lua_rawseti)>();
	return hades_func(L, idx, n);
}

static void hook_luaH_setint(lua_State *L, void *t, int key, void *value)
{
	static auto hades_func = big::hades2_symbol_to_address["luaH_setint"].as_func<decltype(hook_luaH_setint)>();
	return hades_func(L, t, key, value);
}

static TValue *hook_luaH_newkey(lua_State *L, Table *t, const TValue *key)
{
	static auto hades_func = big::hades2_symbol_to_address["luaH_newkey"].as_func<decltype(hook_luaH_newkey)>();
	return hades_func(L, t, key);
}

static void hook_luaH_resize(lua_State *L, Table *t, unsigned int nasize, unsigned int nhsize)
{
	static auto hades_func = big::hades2_symbol_to_address["luaH_resize"].as_func<decltype(hook_luaH_resize)>();
	return hades_func(L, t, nasize, nhsize);
}

static void *hook_luaM_realloc_(lua_State *L, void *block, size_t osize, size_t nsize)
{
	static auto hades_func = big::hades2_symbol_to_address["luaM_realloc_"].as_func<decltype(hook_luaM_realloc_)>();
	return hades_func(L, block, osize, nsize);
}

static void hook_luaH_free(lua_State *L, Table *t)
{
	static auto hades_func = big::hades2_symbol_to_address["luaH_free"].as_func<void(lua_State *, Table *)>();
	return hades_func(L, t);
}

static int hook_luaH_getn(Table *t)
{
	std::scoped_lock l(big::lua_manager_extension::g_manager_mutex);

	//DWORD threadId = GetThreadId(GetCurrentThread());

	//{
	//	std::lock_guard<std::mutex> lock(seenThreadsMutex);
	//	if (seenThreads.find(threadId) == seenThreads.end())
	//	{
	//		seenThreads.insert(threadId);
	//		LOG(ERROR) << "New thread entered: ID = " << threadId;

	//		log_stacktrace_();

	//		PWSTR data;
	//		HRESULT hr = GetThreadDescription(GetCurrentThread(), &data);
	//		if (SUCCEEDED(hr))
	//		{
	//			wprintf(L"%ls\n", data);

	//			LOG(ERROR) << "Thread name: " << wstring_to_utf8(data);

	//			LocalFree(data);
	//		}
	//	}
	//}

	static auto hades_func = big::hades2_symbol_to_address["luaH_getn"].as_func<int(Table *)>();
	return hades_func(t);
}

static Table *hook_luaH_new(lua_State *L)
{
	static auto hades_func = big::hades2_symbol_to_address["luaH_new"].as_func<Table *(lua_State *)>();
	return hades_func(L);
}

static void hook_luaH_resizearray(lua_State *L, Table *t, int a3)
{
	static auto hades_func = big::hades2_symbol_to_address["luaH_resizearray"].as_func<void(lua_State *, Table *, int)>();

	return hades_func(L, t, a3);
}

static void hook_luaL_checkversion_(lua_State* L, lua_Number ver)
{
}

static int hook_game_lua_pcallk(lua_State *L, int nargs, int nresults, int errfunc, int ctx, lua_CFunction k)
{
	std::scoped_lock l(big::lua_manager_extension::g_manager_mutex);

	/*DWORD threadId = GetThreadId(GetCurrentThread());

	{
		std::lock_guard<std::mutex> lock(seenThreadsMutex);
		if (seenThreads.find(threadId) == seenThreads.end())
		{
			seenThreads.insert(threadId);
			LOG(ERROR) << "New thread entered: ID = " << threadId;

			log_stacktrace_();

			PWSTR data;
			HRESULT hr = GetThreadDescription(GetCurrentThread(), &data);
			if (SUCCEEDED(hr))
			{
				wprintf(L"%ls\n", data);

				LOG(ERROR) << "Thread name: " << wstring_to_utf8(data);

				LocalFree(data);
			}
		}
	}*/

	return big::g_hooking->get_original<hook_game_lua_pcallk>()(L, nargs, nresults, errfunc, ctx, k);
}

//static void hook_sgg_WorkerManager_ExecuteCmd(uintptr_t this_, unsigned __int64 arg)
//{
//	PWSTR data;
//	HRESULT hr = GetThreadDescription(GetCurrentThread(), &data);
//	if (SUCCEEDED(hr))
//	{
//		if (wcscmp(data, L"LoadTaskScheduler") == 0)
//		{
//			LocalFree(data);
//
//			std::scoped_lock l(big::lua_manager_extension::g_manager_mutex);
//
//			big::g_hooking->get_original<hook_sgg_WorkerManager_ExecuteCmd>()(this_, arg);
//
//			return;
//		}
//
//		LocalFree(data);
//	}
//
//	big::g_hooking->get_original<hook_sgg_WorkerManager_ExecuteCmd>()(this_, arg);
//}

static int hook_game_luaL_loadbufferx(lua_State* L, const char* original_file_content_ptr, size_t original_file_content_size, const char* name, char* mode)
{
	const auto *apply_buffer_result =
	    lovely_apply_buffer_patches(original_file_content_ptr,
	                                original_file_content_size,
	                                name,
	                                (const char *)big::g_file_manager.get_project_folder("plugins").get_path().u8string().c_str());

	assert(apply_buffer_result);

	if (apply_buffer_result->status != lovely_ApplyBufferPatchesResultEnum::Ok)
	{
		LOG(DEBUG) << "lovely_apply_buffer_patches returned " << static_cast<int>(apply_buffer_result->status);
	}

	const auto res = big::g_hooking->get_original<hook_game_luaL_loadbufferx>()(L, apply_buffer_result->data_ptr, apply_buffer_result->data_len, name, mode);

	if (apply_buffer_result->status == lovely_ApplyBufferPatchesResultEnum::Ok)
	{
		free(apply_buffer_result->data_ptr);
	}

	free((void *)apply_buffer_result);

	return res;
}

enum class ResourceDirectory : int
{
	Scripts = 0xE,
};

static bool hook_sgg_IsContentFolderModified(ResourceDirectory directory, bool initialValue, bool checkForAddedFiles)
{
	if (directory == ResourceDirectory::Scripts)
	{
		return true;
	}

	if (big::g_hooking->get_original<hook_sgg_IsContentFolderModified>()(directory, initialValue, checkForAddedFiles))
	{
		LOG(WARNING) << "Resource directory modified: " << static_cast<int>(directory);
	}

	return false;
}

// void initRenderer(char *appName, const RendererDesc *pDesc, Renderer **)
static void hook_initRenderer(char *appName, const void *pDesc, void **a3)
{
	LOG(INFO) << "initRenderer called";

	big::g_hooking->get_original<hook_initRenderer>()(appName, pDesc, a3);

	big::g_renderer->hook();

	LOG(INFO) << "initRenderer finished";
}

static void hook_SGD_Deserialize_ThingDataDef(void *ctx, int loc, sgg::ThingDataDef *val)
{
	big::g_hooking->get_original<hook_SGD_Deserialize_ThingDataDef>()(ctx, loc, val);
	//val->mScale *= 2;
}

static void sgg__GUIComponentTextBox__GUIComponentTextBox(GUIComponentTextBox *this_, Vectormath::Vector2 location)
{
	//std::scoped_lock l(g_GUIComponentTextBoxes_mutex);

	big::g_hooking->get_original<sgg__GUIComponentTextBox__GUIComponentTextBox>()(this_, location);

	g_GUIComponentTextBoxes.insert(this_);
}

static void sgg__GUIComponentTextBox__Update(GUIComponentTextBox *this_)
{
	std::scoped_lock l(g_GUIComponentTextBoxes_mutex);

	big::g_hooking->get_original<sgg__GUIComponentTextBox__Update>()(this_);

	g_GUIComponentTextBoxes.insert(this_);
}

static void sgg__GUIComponentTextBox__GUIComponentTextBox_dctor(GUIComponentTextBox *this_)
{
	std::scoped_lock l(g_GUIComponentTextBoxes_mutex);

	big::g_hooking->get_original<sgg__GUIComponentTextBox__GUIComponentTextBox_dctor>()(this_);

	g_GUIComponentTextBoxes.erase(this_);
}

static void hook_GUIComponentButton_OnSelected(GUIComponentButton *this_, GUIComponentTextBox *prevSelection)
{
	std::scoped_lock l(big::lua_manager_extension::g_manager_mutex);

	big::g_hooking->get_original<hook_GUIComponentButton_OnSelected>()(this_, prevSelection);

	g_currently_selected_gui_comp = this_;

	auto gui_button = g_currently_selected_gui_comp;
	auto gui_text   = gui_button->mTextBox;

	std::vector<std::string> lines;
	for (auto i = gui_text->mLines.mpBegin; i < gui_text->mLines.mpEnd; i++)
	{
		if (i->mText.size())
		{
			lines.push_back(i->mText.c_str());
		}
	}

	for (const auto &mod_ : big::g_lua_manager->m_modules)
	{
		auto mod = (big::lua_module_ext *)mod_.get();
		for (const auto &f : mod->m_data_ext.m_on_button_hover)
		{
			f(lines);
		}
	}
}

// void sgg::Granny3D::LoadAllModelAndAnimationData(void)
static void hook_LoadAllModelAndAnimationData()
{
	// Not calling it ever again because it crashes inside the func when hotreloading game data.
	// Make sure it's atleast called once though on game start.

	static bool call_it_once = true;
	if (call_it_once)
	{
		call_it_once = false;
		big::g_hooking->get_original<hook_LoadAllModelAndAnimationData>()();
	}
}

static void hook_ReadAllAnimationData()
{
	// Not calling it ever again because it crashes inside the func when hotreloading game data.
	// Make sure it's atleast called once though on game start.

	static bool call_it_once = true;
	if (call_it_once)
	{
		call_it_once = false;
		big::g_hooking->get_original<hook_ReadAllAnimationData>()();
	}
}

// TODO: Cleanup all this
template<class T>
static void ForceWrite(T &dst, const T &src)
{
	DWORD old_flag;
	::VirtualProtect(&dst, sizeof(T), PAGE_EXECUTE_READWRITE, &old_flag);
	dst = src;
	::VirtualProtect(&dst, sizeof(T), old_flag, &old_flag);
}

static void hook_PlayerHandleInput(void *this_, float elapsedSeconds, void *input)
{
	static auto jump_stuff = gmAddress::scan("74 7C 38 05").as<uint8_t *>();

	if (big::g_gui && big::g_gui->is_open() && !lua::hades::inputs::let_game_input_go_through_gui_layer)
	{
		if (jump_stuff && *jump_stuff != 0x75)
		{
			ForceWrite<uint8_t>(*jump_stuff, 0x75);
		}

		return;
	}

	if (jump_stuff && *jump_stuff != 0x74)
	{
		ForceWrite<uint8_t>(*jump_stuff, 0x74);
	}

	big::g_hooking->get_original<hook_PlayerHandleInput>()(this_, elapsedSeconds, input);
}

extern "C"
{
	uintptr_t lpRemain = 0;
}

struct sgg_config_values_fixed
{
	bool *addr     = nullptr;
	bool new_value = false;
};

static bool sgg_config_values_thread_can_loop = false;
static std::vector<sgg_config_values_fixed> sgg_config_values;

static void set_sgg_config_values_thread_loop()
{
	while (true)
	{
		if (sgg_config_values_thread_can_loop)
		{
			for (auto &cfg_value : sgg_config_values)
			{
				*cfg_value.addr = cfg_value.new_value;
			}
		}

		using namespace std::chrono_literals;
		std::this_thread::sleep_for(1s);
	}
}

static void hook_sgg_App_Initialize(void *this_)
{
	auto AudioMemoryPoolVoiceSize = big::hades2_symbol_to_address["sgg::ConfigOptions::AudioMemoryPoolVoiceSize"].as<int *>();
	*AudioMemoryPoolVoiceSize = 0;

	LOG(INFO) << "Setting AudioMemoryPoolVoiceSize to " << *AudioMemoryPoolVoiceSize << ", mods will hit the limit otherwise.";

	auto AudioMemoryPoolSize  = big::hades2_symbol_to_address["sgg::ConfigOptions::AudioMemoryPoolSize"].as<int *>();
	*AudioMemoryPoolSize     = 0;

	LOG(INFO) << "Setting AudioMemoryPoolSize to " << *AudioMemoryPoolSize << ", mods will hit the limit otherwise.";

	return big::g_hooking->get_original<hook_sgg_App_Initialize>()(this_);
	}

static void hook_sgg_ScriptManager_InitLua()
	{
	auto LuaMemoryPoolSize = big::hades2_symbol_to_address["sgg::ConfigOptions::LuaMemoryPoolSize"].as<int *>();
	*LuaMemoryPoolSize     = 0;

	LOG(INFO) << "Setting LuaMemoryPoolSize to " << *LuaMemoryPoolSize << ", mods will hit the limit otherwise.";

	return big::g_hooking->get_original<hook_sgg_ScriptManager_InitLua>()();
}

static bool hook_ConfigOption_registerField_bool(char *name, bool *addr, unsigned int flags, bool defaultValue)
{
	bool is_UseAnalytics = false;
	if (name && strstr(name, "UseAnalytics"))
	{
		defaultValue    = false;
		is_UseAnalytics = true;
	}

	bool is_DebugKeysEnabled = false;
	if (name && strstr(name, "DebugKeysEnabled"))
	{
		defaultValue        = true;
		is_DebugKeysEnabled = true;
	}

	bool is_UnsafeDebugKeysEnabled = false;
	if (name && strstr(name, "UnsafeDebugKeysEnabled"))
	{
		defaultValue              = true;
		is_UnsafeDebugKeysEnabled = true;
	}

	auto res = big::g_hooking->get_original<hook_ConfigOption_registerField_bool>()(name, addr, flags, defaultValue);

	static std::thread set_sgg_config_values_thread = []()
	{
		auto t = std::thread(set_sgg_config_values_thread_loop);
		t.detach();
		return t;
	}();

	if (is_UseAnalytics)
	{
		LOG(INFO) << "Making sure UseAnalytics is false.";
		res = false;
		sgg_config_values.emplace_back(addr, false);
	}

	if (is_DebugKeysEnabled)
	{
		LOG(INFO) << "Making sure DebugKeysEnabled is true.";
		res = true;
		sgg_config_values.emplace_back(addr, true);
	}

	if (is_UnsafeDebugKeysEnabled)
	{
		LOG(INFO) << "Making sure UnsafeDebugKeysEnabled is true.";
		res = true;
		sgg_config_values.emplace_back(addr, true);
		sgg_config_values_thread_can_loop = true;
	}

	return res;
}

static void hook_PlatformAnalytics_Start()
{
	LOG(INFO) << "PlatformAnalytics_Start denied";
}

static void hook_sgg_DetectFreezeThread(HWND__* pData)
{
	LOG(INFO) << "DetectFreezeThread denied";
}

static void hook_disable_f10_launch(void *bugInfo)
{
	LOG(WARNING) << "sgg::LaunchBugReporter denied";

	static bool once = true;
	if (once)
	{
		MessageBoxA(0, "The game has encountered a fatal error, the error is in the log file and in the console.", "Hell2Modding", MB_ICONERROR | MB_OK);
		once = false;
	}
}

static int ends_with(const char *str, const char *suffix)
{
	if (!str || !suffix)
	{
		return 0;
	}
	size_t lenstr    = strlen(str);
	size_t lensuffix = strlen(suffix);
	if (lensuffix > lenstr)
	{
		return 0;
	}
	return strncmp(str + lenstr - lensuffix, suffix, lensuffix) == 0;
}

static bool extension_matches(const char *first, const char *second)
{
	auto get_extension = [](const char *str) -> std::string_view
	{
		size_t len = strlen(str);
		std::string_view ext;
		for (size_t i = 0; i < len; i++)
		{
			if (str[i] == '.')
			{
				ext = std::string_view(&str[i], &str[len - 1]);
			}
		}

		return ext;
	};

	std::string_view first_ext  = get_extension(first);
	std::string_view second_ext = get_extension(second);

	if (first_ext.empty() && second_ext.empty())
	{
		return true;
	}

	if (first_ext.empty() || second_ext.empty())
	{
		return false;
	}

	return !strcmp(first_ext.data(), second_ext.data());
}

template<class F>
bool EachImportFunction(HMODULE module, const char *dllname, const F &f)
{
	if (module == 0)
	{
		return false;
	}

	size_t ImageBase             = (size_t)module;
	PIMAGE_DOS_HEADER pDosHeader = (PIMAGE_DOS_HEADER)ImageBase;
	if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE)
	{
		return false;
	}
	PIMAGE_NT_HEADERS pNTHeader = (PIMAGE_NT_HEADERS)(ImageBase + pDosHeader->e_lfanew);

	size_t RVAImports = pNTHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
	if (RVAImports == 0)
	{
		return false;
	}

	IMAGE_IMPORT_DESCRIPTOR *pImportDesc = (IMAGE_IMPORT_DESCRIPTOR *)(ImageBase + RVAImports);
	while (pImportDesc->Name != 0)
	{
		if (!dllname || stricmp((const char *)(ImageBase + pImportDesc->Name), dllname) == 0)
		{
			IMAGE_IMPORT_BY_NAME **func_names = (IMAGE_IMPORT_BY_NAME **)(ImageBase + pImportDesc->Characteristics);
			void **import_table               = (void **)(ImageBase + pImportDesc->FirstThunk);
			for (size_t i = 0;; ++i)
			{
				if ((size_t)func_names[i] == 0)
				{
					break;
				}
				const char *funcname = (const char *)(ImageBase + (size_t)func_names[i]->Name);
				f(funcname, import_table[i]);
			}
		}
		++pImportDesc;
	}
	return true;
}

using fmodstudio_getevent_t = __int64 (*)(void *fmodstudio_event_system_this, const char *event_name, void **event_description_result);

fmodstudio_getevent_t fmodstudio_getevent_orig = nullptr;

static __int64 hook_fmodstudio_getevent(void *fmodstudio_event_system_this, const char *event_name, void **event_description_result)
{
	if (strstr(event_name, "event:{") && strstr(event_name, "}"))
	{
		event_name = &event_name[6];
	}

	//LOG(INFO) << event_name;

	const auto res = fmodstudio_getevent_orig(fmodstudio_event_system_this, event_name, event_description_result);
	if (res != 0)
	{
		LOG(WARNING) << "Failed playing " << event_name << " - " << res << " - " << event_description_result << " - " << *event_description_result;
	}

	return res;
}

using setlocale_t = char* (__cdecl*)(int Category, const char *Locale);

setlocale_t setlocale_orig = nullptr;

char *__cdecl hook_setlocale(int Category, const char *Locale)
{
	if (strcmp(Locale, "C") == 0)
	{
		Locale = ".utf8";
	}

	return setlocale_orig(Category, Locale);
}

static void init_import_hooks()
{
	EachImportFunction(::GetModuleHandleA(0),
	                   "fmodstudio.dll",
	                   [](const char *funcname, void *&func)
	                   {
		                   if (strcmp(funcname, "?getEvent@System@Studio@FMOD@@QEBA?AW4FMOD_RESULT@@PEBDPEAPEAVEventDescription@23@@Z") == 0)
		                   {
			                   fmodstudio_getevent_orig = (fmodstudio_getevent_t)func;
			                   ForceWrite<void *>(func, hook_fmodstudio_getevent);
		                   }
	                   });

	EachImportFunction(::GetModuleHandleA(0),
	                   "api-ms-win-crt-locale-l1-1-0.dll",
	                   [](const char *funcname, void *&func)
	                   {
		                   if (strcmp(funcname, "setlocale") == 0)
		                   {
			                   setlocale_orig = (setlocale_t)func;
			                   LOG(INFO) << "Hooked setlocale";
			                   ForceWrite<void *>(func, hook_setlocale);
		                   }
	                   });
}

// Lua: Enforce AuthorName-ModName to be part of the std::filesystem::path.filename() of the file.
// See the binding data.cpp file for the implementation.
std::unordered_map<std::string, std::string> additional_package_files;

std::unordered_map<std::string, std::string> additional_granny_files;

//static std::string g_current_custom_package_stem;

static void hook_fsAppendPathComponent_packages(const char *basePath, const char *pathComponent, char *output /*size: 512*/)
{
	//g_current_custom_package_stem = "";

	big::g_hooking->get_original<hook_fsAppendPathComponent_packages>()(basePath, pathComponent, output);

	if (strlen(pathComponent) > 0)
	{
		for (const auto &[filename, full_file_path] : additional_package_files)
		{
			if (strstr(pathComponent, filename.c_str()) && extension_matches(pathComponent, filename.c_str()))
			{
				LOG(DEBUG) << pathComponent << " | " << filename << " | " << full_file_path;
				//g_current_custom_package_stem = (char *)std::filesystem::path(filename).stem().u8string().c_str();
				strcpy(output, full_file_path.c_str());
				break;
			}
			else if (strcmp(filename.c_str(), pathComponent) == 0 && extension_matches(pathComponent, filename.c_str()))
			{
				LOG(DEBUG) << pathComponent << " | " << filename << " | " << full_file_path;
				//g_current_custom_package_stem = (char *)std::filesystem::path(filename).stem().u8string().c_str();
				strcpy(output, full_file_path.c_str());
				break;
			}
		}

		for (const auto &[filename, full_file_path] : additional_granny_files)
		{
			if (strstr(pathComponent, filename.c_str()))
			{
				LOG(DEBUG) << pathComponent << " | " << filename << " | " << full_file_path;

				strcpy(output, full_file_path.c_str());
				break;
			}
		}
	}
}

static void hook_fsGetFilesWithExtension_packages(PVOID resourceDir, const char *subDirectory, wchar_t *extension, eastl::vector<eastl::string> *out)
{
	big::g_hooking->get_original<hook_fsGetFilesWithExtension_packages>()(resourceDir, subDirectory, extension, out);

	bool has_pkg_manifest = false;
	bool has_pkg          = false;
	bool has_gpk          = false;
	for (const auto &xd : *out)
	{
		if (ends_with(xd.c_str(), ".pkg_manifest"))
		{
			has_pkg_manifest = true;
		}

		if (ends_with(xd.c_str(), ".pkg"))
		{
			has_pkg = true;
		}

		if (ends_with(xd.c_str(), ".gpk"))
		{
			has_gpk = true;
		}
	}

	bool is_pkg_manifest_only = has_pkg_manifest && !has_pkg;
	if (is_pkg_manifest_only)
	{
		for (const auto &[filename, full_file_path] : additional_package_files)
		{
			if (ends_with(filename.c_str(), ".pkg_manifest"))
			{
				out->push_back(filename.c_str());
			}
		}
	}
	else if (has_pkg && has_pkg_manifest)
	{
		for (const auto &[filename, full_file_path] : additional_package_files)
		{
			out->push_back(filename.c_str());
		}
	}
	else if (has_gpk)
	{
		for (const auto &[filename, full_file_path] : additional_granny_files)
		{
			out->push_back(filename.c_str());
		}
	}
}

static void lua_print_traceback()
{
	const auto lua_manager = big::g_lua_manager;
	if (lua_manager && lua_manager->get_module_count())
	{
		sol::state_view(lua_manager->lua_state()).script("if rom and rom.game and rom.game._activeThread then rom.log.warning(debug.traceback(rom.game._activeThread)) else debug.traceback() end");
	}
}

static void hook_ParseLuaErrorMessageAndAssert(lua_State *msg, const char *len, unsigned __int64 state)
{
	lua_print_traceback();

	big::g_hooking->get_original<hook_ParseLuaErrorMessageAndAssert>()(msg, len, state);
}

struct sgg_Thing
{
	char pad_1[0x4C];
	sgg::HashGuid mName;

	char pad_2[192];
	void *pAnim;
};

static_assert(offsetof(sgg_Thing, mName) == 0x4C, "sgg::Thing->mName has wrong offset.");
static_assert(offsetof(sgg_Thing, pAnim) == 0x1'10, "sgg::Thing->pAnim has wrong offset.");

static bool hook_sgg_Obstacle_IsObscuring(sgg_Thing *this_, uintptr_t thing)
{
	if (!this_->pAnim)
	{
		return false;
	}

	return big::g_hooking->get_original<hook_sgg_Obstacle_IsObscuring>()(this_, thing);
}

static eastl::string *hook_ReadCSString(eastl::string *result, void *file_stream_input)
{
	const auto res = big::g_hooking->get_original<hook_ReadCSString>()(result, file_stream_input);

	if (result)
	{
		//LOG(WARNING) << result->c_str();
	}
	//if (result && g_current_custom_package_stem.size())
	{
		//LOG(ERROR) << result->c_str() << " does not contain " << g_current_custom_package_stem;
	}

	return res;
}

namespace MemoryMappedFile
{
	struct Handle
	{
#ifdef _WIN32
		void *file;
		void *fileMapping;
#else
		int file;
#endif
		void *baseAddress;
		size_t len;
	};

	Handle Open(const char *path);
	void Close(Handle &handle);
} // namespace MemoryMappedFile

MemoryMappedFile::Handle MemoryMappedFile::Open(const char *path)
{
#ifdef _WIN32
	void *file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_READONLY, nullptr);

	if (file == INVALID_HANDLE_VALUE)
	{
		return Handle{INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE, nullptr, 0};
	}

	void *fileMapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);

	if (fileMapping == nullptr)
	{
		CloseHandle(file);

		return Handle{INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE, nullptr, 0};
	}

	void *baseAddress = MapViewOfFile(fileMapping, FILE_MAP_READ, 0, 0, 0);

	if (baseAddress == nullptr)
	{
		CloseHandle(fileMapping);
		CloseHandle(file);

		return Handle{INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE, nullptr, 0};
	}

	BY_HANDLE_FILE_INFORMATION fileInformation;
	const bool getInformationResult = GetFileInformationByHandle(file, &fileInformation);
	if (!getInformationResult)
	{
		UnmapViewOfFile(baseAddress);
		CloseHandle(fileMapping);
		CloseHandle(file);

		return Handle{INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE, nullptr, 0};
	}

	const size_t fileSizeHighBytes = static_cast<size_t>(fileInformation.nFileSizeHigh) << 32;
	const size_t fileSizeLowBytes  = fileInformation.nFileSizeLow;
	const size_t fileSize          = fileSizeHighBytes | fileSizeLowBytes;
	return Handle{file, fileMapping, baseAddress, fileSize};
#else
	struct stat fileSb;

	int file = open(path, O_RDONLY);

	if (file == INVALID_HANDLE_VALUE)
	{
		return Handle{INVALID_HANDLE_VALUE, nullptr, 0};
	}

	if (fstat(file, &fileSb) == -1)
	{
		close(file);

		return Handle{INVALID_HANDLE_VALUE, nullptr, 0};
	}

	void *baseAddress = mmap(nullptr, fileSb.st_size, PROT_READ, MAP_PRIVATE, file, 0);

	if (baseAddress == MAP_FAILED)
	{
		close(file);

		return Handle{INVALID_HANDLE_VALUE, nullptr, 0};
	}

	return Handle{file, baseAddress, static_cast<size_t>(fileSb.st_size)};
#endif
}

void MemoryMappedFile::Close(Handle &handle)
{
#ifdef _WIN32
	UnmapViewOfFile(handle.baseAddress);
	CloseHandle(handle.fileMapping);
	CloseHandle(handle.file);

	handle.file        = nullptr;
	handle.fileMapping = nullptr;
#else
	munmap(handle.baseAddress, handle.len);
	close(handle.file);

	handle.file = 0;
#endif

	handle.baseAddress = nullptr;
}

PDB_NO_DISCARD static bool IsError(PDB::ErrorCode errorCode)
{
	switch (errorCode)
	{
	case PDB::ErrorCode::Success: return false;

	case PDB::ErrorCode::InvalidSuperBlock: LOGF(ERROR, "Invalid Superblock"); return true;

	case PDB::ErrorCode::InvalidFreeBlockMap: LOGF(ERROR, "Invalid free block map"); return true;

	case PDB::ErrorCode::InvalidStream: LOGF(ERROR, "Invalid stream"); return true;

	case PDB::ErrorCode::InvalidSignature: LOGF(ERROR, "Invalid stream signature"); return true;

	case PDB::ErrorCode::InvalidStreamIndex: LOGF(ERROR, "Invalid stream index"); return true;

	case PDB::ErrorCode::UnknownVersion: LOGF(ERROR, "Unknown version"); return true;
	}

	// only ErrorCode::Success means there wasn't an error, so all other paths have to assume there was an error
	return true;
}

PDB_NO_DISCARD static bool HasValidDBIStreams(const PDB::RawFile &rawPdbFile, const PDB::DBIStream &dbiStream)
{
	// check whether the DBI stream offers all sub-streams we need
	if (IsError(dbiStream.HasValidSymbolRecordStream(rawPdbFile)))
	{
		return false;
	}

	if (IsError(dbiStream.HasValidPublicSymbolStream(rawPdbFile)))
	{
		return false;
	}

	if (IsError(dbiStream.HasValidGlobalSymbolStream(rawPdbFile)))
	{
		return false;
	}

	if (IsError(dbiStream.HasValidSectionContributionStream(rawPdbFile)))
	{
		return false;
	}

	if (IsError(dbiStream.HasValidImageSectionStream(rawPdbFile)))
	{
		return false;
	}

	return true;
}

static void read_game_pdb()
{
	const auto base_game_address = (uintptr_t)GetModuleHandleA(0);

	const auto game_pdb_path = lua::paths_ext::get_game_executable_folder() / "Hades2.pdb";

	// try to open the PDB file and check whether all the data we need is available
	MemoryMappedFile::Handle pdbFile = MemoryMappedFile::Open((char *)game_pdb_path.u8string().c_str());
	if (!pdbFile.baseAddress)
	{
		LOGF(ERROR, "Cannot memory-map file {}", (char *)game_pdb_path.u8string().c_str());
	}

	if (IsError(PDB::ValidateFile(pdbFile.baseAddress, pdbFile.len)))
	{
		MemoryMappedFile::Close(pdbFile);
	}

	const PDB::RawFile rawPdbFile = PDB::CreateRawFile(pdbFile.baseAddress);
	if (IsError(PDB::HasValidDBIStream(rawPdbFile)))
	{
		MemoryMappedFile::Close(pdbFile);
	}

	const PDB::InfoStream infoStream(rawPdbFile);
	if (infoStream.UsesDebugFastLink())
	{
		LOGF(ERROR, "PDB was linked using unsupported option /DEBUG:FASTLINK");

		MemoryMappedFile::Close(pdbFile);
	}

	const auto h = infoStream.GetHeader();
	LOGF(INFO,
	     std::format("Version {}, signature {}, age {}, GUID "
	                 "{:08x}-{:04x}-{:04x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
	                 static_cast<uint32_t>(h->version),
	                 h->signature,
	                 h->age,
	                 h->guid.Data1,
	                 h->guid.Data2,
	                 h->guid.Data3,
	                 h->guid.Data4[0],
	                 h->guid.Data4[1],
	                 h->guid.Data4[2],
	                 h->guid.Data4[3],
	                 h->guid.Data4[4],
	                 h->guid.Data4[5],
	                 h->guid.Data4[6],
	                 h->guid.Data4[7]));


	const PDB::DBIStream dbiStream = PDB::CreateDBIStream(rawPdbFile);
	if (!HasValidDBIStreams(rawPdbFile, dbiStream))
	{
		MemoryMappedFile::Close(pdbFile);
	}

	const PDB::TPIStream tpiStream = PDB::CreateTPIStream(rawPdbFile);
	if (PDB::HasValidTPIStream(rawPdbFile) != PDB::ErrorCode::Success)
	{
		MemoryMappedFile::Close(pdbFile);
	}

	// in order to keep the example easy to understand, we load the PDB data serially.
	// note that this can be improved a lot by reading streams concurrently.

	// prepare the image section stream first. it is needed for converting section + offset into an RVA
	const PDB::ImageSectionStream imageSectionStream = dbiStream.CreateImageSectionStream(rawPdbFile);


	const PDB::ModuleInfoStream moduleInfoStream = dbiStream.CreateModuleInfoStream(rawPdbFile);


	const PDB::CoalescedMSFStream symbolRecordStream = dbiStream.CreateSymbolRecordStream(rawPdbFile);

	// read global symbols
	const PDB::GlobalSymbolStream globalSymbolStream = dbiStream.CreateGlobalSymbolStream(rawPdbFile);

	const PDB::ArrayView<PDB::HashRecord> hashRecords = globalSymbolStream.GetRecords();
	const size_t count                                = hashRecords.GetLength();

	for (const PDB::HashRecord &hashRecord : hashRecords)
	{
		const PDB::CodeView::DBI::Record *record = globalSymbolStream.GetRecord(symbolRecordStream, hashRecord);

		const char *name = nullptr;
		uint32_t rva     = 0u;
		if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_GDATA32)
		{
			name = record->data.S_GDATA32.name;
			rva = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_GDATA32.section, record->data.S_GDATA32.offset);
		}
		else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_GTHREAD32)
		{
			name = record->data.S_GTHREAD32.name;
			rva =
			    imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_GTHREAD32.section, record->data.S_GTHREAD32.offset);
		}
		else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_LDATA32)
		{
			name = record->data.S_LDATA32.name;
			rva = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_LDATA32.section, record->data.S_LDATA32.offset);
		}
		else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_LTHREAD32)
		{
			name = record->data.S_LTHREAD32.name;
			rva =
			    imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_LTHREAD32.section, record->data.S_LTHREAD32.offset);
		}
		else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_UDT)
		{
			name = record->data.S_UDT.name;
		}
		else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_UDT_ST)
		{
			name = record->data.S_UDT_ST.name;
		}

		if (rva == 0u)
		{
			// certain symbols (e.g. control-flow guard symbols) don't have a valid RVA, ignore those
			continue;
		}

		//LOG(INFO) << "name: " << name << " rva: " << rva;
		big::hades2_insert_symbol_to_map(name, base_game_address + rva);
	}

	// read module symbols
	{
		const PDB::ArrayView<PDB::ModuleInfoStream::Module> modules = moduleInfoStream.GetModules();

		for (const PDB::ModuleInfoStream::Module &module : modules)
		{
			if (!module.HasSymbolStream())
			{
				continue;
			}

			const PDB::ModuleSymbolStream moduleSymbolStream = module.CreateSymbolStream(rawPdbFile);
			moduleSymbolStream.ForEachSymbol(
			    [&imageSectionStream, base_game_address](const PDB::CodeView::DBI::Record *record)
			    //[&symbols, &imageSectionStream](const PDB::CodeView::DBI::Record *record)
			    {
				    const char *name = nullptr;
				    uint32_t rva     = 0u;
				    if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_THUNK32)
				    {
					    if (record->data.S_THUNK32.thunk == PDB::CodeView::DBI::ThunkOrdinal::TrampolineIncremental)
					    {
						    // we have never seen incremental linking thunks stored inside a S_THUNK32 symbol, but better be safe than sorry
						    name = "ILT";
						    rva  = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_THUNK32.section,
                                                                               record->data.S_THUNK32.offset);
					    }
				    }
				    else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_TRAMPOLINE)
				    {
					    // incremental linking thunks are stored in the linker module
					    name = "ILT";
					    rva  = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_TRAMPOLINE.thunkSection,
                                                                           record->data.S_TRAMPOLINE.thunkOffset);
				    }
				    else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_BLOCK32)
				    {
					    // blocks never store a name and are only stored for indicating whether other symbols are children of this block
				    }
				    else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_LABEL32)
				    {
					    // labels don't have a name
				    }
				    else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_LPROC32)
				    {
					    name = record->data.S_LPROC32.name;
					    rva  = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_LPROC32.section,
                                                                           record->data.S_LPROC32.offset);

						big::hades2_insert_symbol_to_map_code_size(name, record->data.S_LPROC32.codeSize);
				    }
				    else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_GPROC32)
				    {
					    name = record->data.S_GPROC32.name;
					    rva  = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_GPROC32.section,
                                                                           record->data.S_GPROC32.offset);

						big::hades2_insert_symbol_to_map_code_size(name, record->data.S_GPROC32.codeSize);
				    }
				    else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_LPROC32_ID)
				    {
					    name = record->data.S_LPROC32_ID.name;
					    rva  = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_LPROC32_ID.section,
                                                                           record->data.S_LPROC32_ID.offset);

						big::hades2_insert_symbol_to_map_code_size(name, record->data.S_LPROC32_ID.codeSize);
				    }
				    else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_GPROC32_ID)
				    {
					    name = record->data.S_GPROC32_ID.name;
					    rva  = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_GPROC32_ID.section,
                                                                           record->data.S_GPROC32_ID.offset);

						big::hades2_insert_symbol_to_map_code_size(name, record->data.S_GPROC32_ID.codeSize);
				    }
				    else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_REGREL32)
				    {
					    name = record->data.S_REGREL32.name;
					    // You can only get the address while running the program by checking the register value and adding the offset
				    }
				    else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_LDATA32)
				    {
					    name = record->data.S_LDATA32.name;
					    rva  = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_LDATA32.section,
                                                                           record->data.S_LDATA32.offset);
				    }
				    else if (record->header.kind == PDB::CodeView::DBI::SymbolRecordKind::S_LTHREAD32)
				    {
					    name = record->data.S_LTHREAD32.name;
					    rva  = imageSectionStream.ConvertSectionOffsetToRVA(record->data.S_LTHREAD32.section,
                                                                           record->data.S_LTHREAD32.offset);
				    }

				    if (rva == 0u)
				    {
					    // certain symbols (e.g. control-flow guard symbols) don't have a valid RVA, ignore those
					    return;
				    }

				    //LOG(INFO) << "name: " << name << " rva: " << rva;
				    big::hades2_insert_symbol_to_map(name, base_game_address + rva);
			    });
		}
	}

	MemoryMappedFile::Close(pdbFile);
}

template<typename T>
constexpr void store(uint8_t *address, const T &value)
{
	std::copy_n(reinterpret_cast<const uint8_t *>(&value), sizeof(T), address);
}

template<typename T>
constexpr void store(uintptr_t address, const T &value)
{
	store((uint8_t *)address, value);
}

#pragma pack(push, 1)

struct JmpE9
{
	uint8_t opcode{0xE9};
	int32_t offset{0};
};

#if SAFETYHOOK_ARCH_X86_64

struct JmpFF
{
	uint8_t opcode0{0xFF};
	uint8_t opcode1{0x25};
	int32_t offset{0};
};

struct TrampolineEpilogueE9
{
	JmpE9 jmp_to_original{};
	JmpE9 jmp_to_shellcode{};
	uint64_t destination_address{};
};

struct TrampolineEpilogueFF
{
	JmpFF jmp_to_original{};
	uint64_t original_address{};
};
#elif SAFETYHOOK_ARCH_X86_32
struct TrampolineEpilogueE9
{
	JmpE9 jmp_to_original{};
	JmpE9 jmp_to_shellcode{};
};
#endif
#pragma pack(pop)

struct section_address
{
	LIEF::PE::Section *section;
	uint8_t *address_in_section;

	uintptr_t to_rva() const
	{
		return (uintptr_t)address_in_section - (uintptr_t)section->content().data() + (uintptr_t)section->virtual_address();
	}
};

#if SAFETYHOOK_ARCH_X86_64
static bool emit_jmp_ff(section_address &where_to_emit_the_jump_instruction, section_address &where_we_jump_to, section_address &jump_target_holder, size_t size = sizeof(JmpFF))
{
	if (size < sizeof(JmpFF))
	{
		LOG(ERROR) << "not enough space at where_to_emit_the_jump_instruction " << where_to_emit_the_jump_instruction.to_rva();
		return false;
	}

	if (size > sizeof(JmpFF))
	{
		// pad with NOPs
		std::fill_n(where_to_emit_the_jump_instruction.address_in_section, size, 0x90);
	}

	JmpFF jmp{};

	jmp.offset = static_cast<int32_t>(jump_target_holder.to_rva() - where_to_emit_the_jump_instruction.to_rva() - sizeof(jmp));

	LOG(INFO) << "[Emit] FF jmp from " << HEX_TO_UPPER(where_to_emit_the_jump_instruction.to_rva()) << " to "
	          << HEX_TO_UPPER(where_we_jump_to.to_rva()) << " via jump target holder at "
	          << HEX_TO_UPPER(jump_target_holder.to_rva()) << ", calculated offset: " HEX_TO_UPPER(jmp.offset);

	store(jump_target_holder.address_in_section, where_we_jump_to.to_rva());

	store(where_to_emit_the_jump_instruction.address_in_section, jmp);

	return true;
}
#endif

static bool emit_jmp_e9(section_address& where_to_emit_the_jump_instruction, section_address &where_we_jump_to, size_t size = sizeof(JmpE9))
{
	if (size < sizeof(JmpE9))
	{
		LOG(ERROR) << "not enough space where we emit to " << where_to_emit_the_jump_instruction.to_rva();
		return false;
	}

	if (size > sizeof(JmpE9))
	{
		std::fill_n(where_to_emit_the_jump_instruction.address_in_section, size, static_cast<uint8_t>(0x90));
	}

	JmpE9 jmp{};
	jmp.offset = static_cast<int32_t>(where_we_jump_to.to_rva() - where_to_emit_the_jump_instruction.to_rva() - sizeof(jmp));
	store(where_to_emit_the_jump_instruction.address_in_section, jmp);

	return true;
}

static bool decode(ZydisDecodedInstruction *ix, uint8_t *ip)
{
	ZydisDecoder decoder{};
	ZyanStatus status;

#if SAFETYHOOK_ARCH_X86_64
	status = ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
#elif SAFETYHOOK_ARCH_X86_32
	status = ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LEGACY_32, ZYDIS_STACK_WIDTH_32);
#endif

	if (!ZYAN_SUCCESS(status))
	{
		return false;
	}

	return ZYAN_SUCCESS(ZydisDecoderDecodeInstruction(&decoder, nullptr, ip, 15, ix));
}

void InlineHook_enable(LIEF::PE::Binary *bin, section_address &target, section_address& shellcode, const std::vector<uint8_t> &original_bytes, section_address& trampoline, size_t trampoline_size)
{
	// jmp from original to trampoline.
	auto trampoline_epilogue = reinterpret_cast<TrampolineEpilogueE9 *>(trampoline.address_in_section + trampoline_size - sizeof(TrampolineEpilogueE9));

	auto where_we_jump_to = section_address
	{
		.section = trampoline.section, 
		.address_in_section = (uint8_t *)&trampoline_epilogue->jmp_to_shellcode
	};

	auto success = emit_jmp_e9(
		target,
		where_we_jump_to,
		original_bytes.size()
	);

	if (!success)
	{
		LOG(ERROR) << "[Emit] Failure E9 jmp from original " << 
			HEX_TO_UPPER(target.to_rva()) << 
			" to trampoline " << 
			HEX_TO_UPPER(trampoline.to_rva());
	}
	else
	{
		LOG(INFO) << "[Emit] E9 jmp from original " << 
			HEX_TO_UPPER(target.to_rva()) << 
			" to trampoline " << 
			HEX_TO_UPPER(trampoline.to_rva());
	}
}

static bool InlineHook_e9_hook(LIEF::PE::Binary *bin, std::vector<uint8_t> &original_bytes, section_address &target_section_address, section_address &shellcode, section_address &trampoline, size_t &trampoline_size)
{
	original_bytes.clear();
	trampoline_size = sizeof(TrampolineEpilogueE9);

	const auto target = target_section_address.address_in_section;
	std::vector<uint8_t *> desired_addresses{target};
	ZydisDecodedInstruction ix{};

	for (auto ip = target; ip < target + sizeof(JmpE9); ip += ix.length)
	{
		if (!decode(&ix, (uint8_t*)ip))
		{
			return false;
		}

		trampoline_size += ix.length;
		original_bytes.insert(original_bytes.end(), ip, ip + ix.length);

		const auto is_relative = (ix.attributes & ZYDIS_ATTRIB_IS_RELATIVE) != 0;

		if (is_relative)
		{
			if (ix.raw.disp.size == 32)
			{
				const auto target_address = ip + ix.length + static_cast<int32_t>(ix.raw.disp.value);
				desired_addresses.emplace_back(target_address);
			}
			else if (ix.raw.imm[0].size == 32)
			{
				const auto target_address = ip + ix.length + static_cast<int32_t>(ix.raw.imm[0].value.s);
				desired_addresses.emplace_back(target_address);
			}
			else if (ix.meta.category == ZYDIS_CATEGORY_COND_BR && ix.meta.branch_type == ZYDIS_BRANCH_TYPE_SHORT)
			{
				const auto target_address = ip + ix.length + static_cast<int32_t>(ix.raw.imm[0].value.s);
				desired_addresses.emplace_back(target_address);
				trampoline_size += 4; // near conditional branches are 4 bytes larger.
			}
			else if (ix.meta.category == ZYDIS_CATEGORY_UNCOND_BR && ix.meta.branch_type == ZYDIS_BRANCH_TYPE_SHORT)
			{
				const auto target_address = ip + ix.length + static_cast<int32_t>(ix.raw.imm[0].value.s);
				desired_addresses.emplace_back(target_address);
				trampoline_size += 3; // near unconditional branches are 3 bytes larger.
			}
			else
			{
				LOG(ERROR) << "unsupported relative instruction in trampoline at address " << ip;

				return false;
			}
		}
	}
	
	uint8_t *tramp_ip   = trampoline.address_in_section;

	for (auto ip = target; ip < target + original_bytes.size(); ip += ix.length)
	{
		if (!decode(&ix, (uint8_t*)ip))
		{
			//trampoline.free(); not needed here for static binary patching
			LOG(ERROR) << "failed to decode instruction at address " << ip;
		}

		const auto is_relative = (ix.attributes & ZYDIS_ATTRIB_IS_RELATIVE) != 0;

		if (is_relative && ix.raw.disp.size == 32)
		{
			std::copy_n(ip, ix.length, tramp_ip);
			const auto target_address = ip + ix.length + ix.raw.disp.value;
			const auto new_disp       = target_address - (tramp_ip + ix.length);
			store(tramp_ip + ix.raw.disp.offset, static_cast<int32_t>(new_disp));
			tramp_ip += ix.length;
		}
		else if (is_relative && ix.raw.imm[0].size == 32)
		{
			std::copy_n(ip, ix.length, tramp_ip);
			const auto target_address = ip + ix.length + ix.raw.imm[0].value.s;
			const auto new_disp       = target_address - (tramp_ip + ix.length);
			store(tramp_ip + ix.raw.imm[0].offset, static_cast<int32_t>(new_disp));
			tramp_ip += ix.length;
		}
		else if (ix.meta.category == ZYDIS_CATEGORY_COND_BR && ix.meta.branch_type == ZYDIS_BRANCH_TYPE_SHORT)
		{
			const auto target_address = ip + ix.length + ix.raw.imm[0].value.s;
			auto new_disp             = target_address - (tramp_ip + 6);

			// Handle the case where the target is now in the trampoline.
			if (target_address >= target && target_address < target + original_bytes.size())
			{
				new_disp = static_cast<ptrdiff_t>(ix.raw.imm[0].value.s);
			}

			*tramp_ip       = 0x0F;
			*(tramp_ip + 1) = 0x10 + ix.opcode;
			store(tramp_ip + 2, static_cast<int32_t>(new_disp));
			tramp_ip += 6;
		}
		else if (ix.meta.category == ZYDIS_CATEGORY_UNCOND_BR && ix.meta.branch_type == ZYDIS_BRANCH_TYPE_SHORT)
		{
			const auto target_address = ip + ix.length + ix.raw.imm[0].value.s;
			auto new_disp             = target_address - (tramp_ip + 5);

			// Handle the case where the target is now in the trampoline.
			if (target_address >= target && target_address < target + original_bytes.size())
			{
				new_disp = static_cast<ptrdiff_t>(ix.raw.imm[0].value.s);
			}

			*tramp_ip = 0xE9;
			store(tramp_ip + 1, static_cast<int32_t>(new_disp));
			tramp_ip += 5;
		}
		else
		{
			std::copy_n(ip, ix.length, tramp_ip);
			tramp_ip += ix.length;
		}
	}

	auto trampoline_epilogue = (TrampolineEpilogueE9 *)(trampoline.address_in_section + trampoline_size - sizeof(TrampolineEpilogueE9));

	// jmp from trampoline to original.
	auto e9_jump_to_orig_location = section_address
	{
		.section = trampoline.section,
		.address_in_section = (uint8_t *)&trampoline_epilogue->jmp_to_original
	};

	auto jump_destination    = section_address
	{
		.section = target_section_address.section,
		.address_in_section = target + original_bytes.size()
	};

	auto success = emit_jmp_e9(e9_jump_to_orig_location, jump_destination);

	if (!success)
	{
		LOG(ERROR) << "[Emit] Failure E9 jmp from trampoline " << 
			HEX_TO_UPPER(e9_jump_to_orig_location.to_rva()) << 
			" to original "
			<< HEX_TO_UPPER(jump_destination.to_rva());
		return false;
	}

	LOG(INFO) << "[Emit] E9 jmp from trampoline " << 
		HEX_TO_UPPER(e9_jump_to_orig_location.to_rva()) << 
		" to original " << 
		HEX_TO_UPPER(jump_destination.to_rva());

	// jmp from trampoline to destination.

#if SAFETYHOOK_ARCH_X86_64
	//auto jump_target_holder = section_address
	//{
	//	.section = trampoline.section,
	//	.address_in_section = (uint8_t *)&trampoline_epilogue->destination_address
	//};

	auto trampoline_jmp_to_shellcode = section_address
	{
		.section = trampoline.section,
		.address_in_section = (uint8_t *)&trampoline_epilogue->jmp_to_shellcode
	};

	//auto where_we_jump_to = section_address
	//{
	//	.section = shellcode.section,

	//	// ff 25 is RIP relative addressing, poiting to a qword
	//    // the qword contains the absolute address we want to jump to.
	//	.address_in_section = bin->imagebase() + shellcode.address_in_section
	//};

	//success = emit_jmp_ff(
	//	trampoline_jmp_to_shellcode,
	//	where_we_jump_to,
	//	jump_target_holder
	//);

	success = emit_jmp_e9(
		trampoline_jmp_to_shellcode,
		shellcode);

	if (!success)
	{
		LOG(ERROR) << "failed to emit jmp from trampoline to shellcode:" << HEX_TO_UPPER(trampoline_jmp_to_shellcode.to_rva());
		return false;
	}
#elif SAFETYHOOK_ARCH_X86_32
	if (auto result = emit_jmp_e9(trampoline_jmp_to_shellcode, shellcode); !result)
	{
		LOG(ERROR) << "failed to emit jmp from trampoline to shellcode: "
		           << HEX_TO_UPPER((reinterpret_cast<uint8_t *>(&trampoline_epilogue->jmp_to_shellcode) - shellcode_base_address));
		return false;
	}
#endif

	return true;
}

#if SAFETYHOOK_ARCH_X86_64
bool InlineHook_ff_hook(LIEF::PE::Binary *bin, std::vector<uint8_t> &original_bytes, section_address &target_section_address, section_address &destination, section_address &trampoline, size_t &trampoline_size)
{
	original_bytes.clear();
	trampoline_size = sizeof(TrampolineEpilogueFF);
	ZydisDecodedInstruction ix{};

	auto target = target_section_address.address_in_section;

	for (auto ip = target; ip < target + sizeof(JmpFF) + sizeof(uintptr_t); ip += ix.length)
	{
		if (!decode(&ix, (uint8_t*)ip))
		{
			LOG(ERROR) << "failed to decode instruction at address " << ip;
		}

		// We can't support any instruction that is IP relative here because
		// ff_hook should only be called if e9_hook failed indicating that
		// we're likely outside the +- 2GB range.
		if (ix.attributes & ZYDIS_ATTRIB_IS_RELATIVE)
		{
			LOG(ERROR) << "relative instruction found in ff_hook at address " << ip;

			return false;
		}

		original_bytes.insert(original_bytes.end(), ip, ip + ix.length);
		trampoline_size += ix.length;
	}

	auto trampoline_allocation = trampoline.address_in_section;

	std::copy(original_bytes.begin(), original_bytes.end(), trampoline_allocation);

	const auto trampoline_epilogue = reinterpret_cast<TrampolineEpilogueFF *>(trampoline_allocation + trampoline_size - sizeof(TrampolineEpilogueFF));

	auto jump_target_holder = section_address
	{
		.section = destination.section,
		.address_in_section	= (uint8_t *) & trampoline_epilogue->original_address
	};

	auto trampoline_jump_to_original = section_address
	{
		.section = trampoline.section,
		.address_in_section = (uint8_t *)&trampoline_epilogue->jmp_to_original};

	auto where_we_jump_to = section_address
	{
		.section = target_section_address.section,
		.address_in_section = target + original_bytes.size()
	};

	// jmp from trampoline to original.
	auto success = emit_jmp_ff(
		trampoline_jump_to_original, 
		where_we_jump_to, 
		jump_target_holder
	);

	if (!success)
	{
		LOG(ERROR) << "failed to emit jmp from trampoline to original";

		return false;
	}

	return true;
}
#endif

static bool InlineHook_setup(LIEF::PE::Binary *bin, section_address &target, section_address &shellcode, std::vector<uint8_t> &original_bytes, section_address &trampoline, size_t &trampoline_size)
{
	return InlineHook_e9_hook(bin, original_bytes, target, shellcode, trampoline, trampoline_size);
}

#pragma optimize("", off)
//SHELLCODE_FUNC(void LoadLibraryShellCode()
static void LoadLibraryShellCode()
{
	DEFINE_FUNC_PTR("kernel32.dll", LoadLibraryA);

	DEFINE_FUNC_PTR("kernel32.dll", GetProcAddress);

	CHAR d3d12_string[] = {'d', '3', 'd', '1', '2', '.', 'd', 'l', 'l', '\0'};

	HMODULE d3d12_module = LoadLibraryA(d3d12_string);

	CHAR my_main_string[] = {'m', 'y', '_', 'm', 'a', 'i', 'n', '\0'};

	GetProcAddress(d3d12_module, my_main_string)();

	DEFINE_FUNC_PTR("kernel32.dll", GetModuleHandleA);
	// __scrt_common_main_seh
	CHAR game_original_entrypoint[] = {'_', '_', 's', 'c', 'r', 't', '_', 'c', 'o', 'm', 'm', 'o',
	                                   'n', '_', 'm', 'a', 'i', 'n', '_', 's', 'e', 'h', '\0'};
	GetProcAddress(GetModuleHandleA(0), game_original_entrypoint)();
	//})
}

#pragma optimize("", on)

static std::string get_game_executable_path()
{
	return (char *)((lua::paths_ext::get_game_executable_folder() / "Hades2.exe").u8string().c_str());
}

static std::string get_orig_game_executable_path()
{
	return (char *)((lua::paths_ext::get_game_executable_folder() / "Hades2_orig.exe").u8string().c_str());
}

static std::string get_game_executable_path_tmp()
{
	return (char *)((lua::paths_ext::get_game_executable_folder() / "Hades2_tmp.exe").u8string().c_str());
}

static bool ensure_early_entrypoint()
{
#ifndef FINAL
	LOG(WARNING) << "Not a final build, can't execute ensure_early_entrypoint cause of security cookies inserted into "
	                "the shellcode by the compiler. (msvc flag /GS-) ";
		return false;
#endif

	// For some users d3d12 is loaded after the _initterm (which contains cpp dynamic initializers),
	// so we need to patch the game exe and add a LoadLibrary(d3d12) to the game entrypoint.

	//const auto function_to_hook = "__scrt_common_main_seh";
	const auto function_to_hook = "mainCRTStartup";

	const auto game_entrypoint_function = big::hades2_symbol_to_address[function_to_hook].as<uintptr_t>();
	const size_t game_entrypoint_function_size = big::hades2_symbol_to_code_size[function_to_hook];

	const auto game_entrypoint_function_rva = game_entrypoint_function - (uintptr_t)GetModuleHandleA(0);

	if (std::unique_ptr<LIEF::PE::Binary> bin = LIEF::PE::Parser::parse(get_game_executable_path()))
	{
		constexpr uint32_t patched_time_date_stamp = 0xFF'FF'00'00 + 1;

		if (bin->header().time_date_stamp() == patched_time_date_stamp)
		{
			LOG(INFO) << "Game executable already patched to load d3d12.dll at game exe entrypoint, skipping patching.";
			return true;
		}

		LOG(INFO) << "Patching game entrypoint (" << function_to_hook << ") to load d3d12.dll as soon as possible: " << HEX_TO_UPPER_OFFSET(game_entrypoint_function) << " (size: " << HEX_TO_UPPER(game_entrypoint_function_size) << ")";

		bin->header().time_date_stamp(patched_time_date_stamp);

		std::vector<uint8_t> empty_section(0x40'00, 0);

		LIEF::PE::Section section;
		section.name(".h2m");
		section.content(empty_section);
		section.size(empty_section.size());
		section.add_characteristic(LIEF::PE::Section::CHARACTERISTICS::MEM_READ);
		section.add_characteristic(LIEF::PE::Section::CHARACTERISTICS::MEM_EXECUTE);
		auto rom_section = bin->add_section(section);

		std::vector<uint8_t> original_bytes;

		bool is_e9_hook = false;

		auto trampoline = section_address
		{
			.section = rom_section,
			.address_in_section = (uint8_t *)rom_section->content().data()
		};
		size_t trampoline_size = 0;

		auto text_section = bin->get_section(".text");

		bin->get_export()->add_entry("__scrt_common_main_seh", big::hades2_symbol_to_address["__scrt_common_main_seh"] - (uintptr_t)GetModuleHandleA(0));

		auto target = section_address
		{
			.section = text_section,
		    .address_in_section = 
				(uint8_t *)text_section->content().data() + 
				game_entrypoint_function_rva - 
				text_section->virtual_address()
		};
		auto shellcode = section_address
		{
			.section = rom_section,
			.address_in_section = (uint8_t *)rom_section->content().data() + 0x50
		};
		memcpy(shellcode.address_in_section, (uint8_t *)LoadLibraryShellCode, 0x2000);

		LOG(INFO) << "Setting up static patching hook at " << HEX_TO_UPPER(target.to_rva()) << " to redirect to shellcode at " << 
			HEX_TO_UPPER(shellcode.to_rva());

		InlineHook_setup(bin.get(), 
			target, 
			shellcode, 
			original_bytes,
			trampoline, trampoline_size
		);

		InlineHook_enable(bin.get(), 
			target, 
			shellcode, 
			original_bytes,
			trampoline, trampoline_size
		);

		const auto output_path = get_game_executable_path_tmp();
		if (std::filesystem::exists(output_path))
		{
			std::filesystem::remove(output_path);
		}
		LIEF::PE::Builder::config_t builder_config{};
		builder_config.exports = true;

		bin->write(output_path, builder_config);

		if (std::filesystem::exists(get_orig_game_executable_path()))
		{
			std::filesystem::remove(get_orig_game_executable_path());
		}
		std::filesystem::rename(get_game_executable_path(), get_orig_game_executable_path());

		Sleep(1);

		std::filesystem::rename(get_game_executable_path_tmp(), get_game_executable_path());

		message("Game executable patched for modding, please relaunch the game. This should only happens once, if there is any issues please tell us at https://github.com/SGG-Modding/Hell2Modding");

		TerminateProcess(GetCurrentProcess(), 0);
	}

	return true;
}

bool g_already_executed_main = false;

extern "C" __declspec(dllexport) void my_main()
{
	using namespace big;

	if (g_already_executed_main)
	{
		return;
	}

	g_already_executed_main = true;

	DisableThreadLibraryCalls(g_hmodule);

		//while (!IsDebuggerPresent())
		//{
		//	Sleep(1000);
		//}

		// https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/setlocale-wsetlocale?view=msvc-170#utf-8-support
		setlocale(LC_ALL, ".utf8");
		// This also change things like stringstream outputs and add comma to numbers and things like that, we don't want that, so just set locale on the C apis instead.
		//std::locale::global(std::locale(".utf8"));

		dll_proxy::init();

		if (!rom::is_rom_enabled())
		{
		return;
		}

		// Lua API: Namespace
		// Name: rom
		rom::init("Hell2Modding", "Hades2.exe", "rom");

		// Purposely leak it, we are not unloading this module in any case.
		const auto exception_handling = new exception_handler(true, nullptr);

		read_game_pdb();

		{
		// ensure_early_entrypoint();
		}

		{
		array_extender((void **)&extended_sgg_sBuffer, extended_sgg_sBuffer_size, "sgg::sBuffer", patch_lea_sgg_sBuffer_usage);
			extend_sgg_sBufferLen_max_size();

			//array_extender((void **)&extended_sgg_sHashes, extended_sgg_sHashes_size, "sgg::sHashes", patch_sgg_sHashes_usage);
			//{
			//	/*
			//	  while ( sgg::sHashes[v9] != v5 )
			//		v6 = ((_DWORD)v6 + 1) & 0x3FFFF;
			//	*/
			//	static auto check = gmAddress::scan("81 E7 FF FF 03 00 48");
			//	if (check)
			//	{
			//		memory::byte_patch::make(check.offset(2).as<uint32_t *>(), extended_sgg_sHashes_size)->apply();
			//	}
			//}
			//{
			//	/*
			//	  while ( sgg::sHashes[v9] != v5 )
			//		v6 = ((_DWORD)v6 + 1) & 0x3FFFF;
			//	*/
			//	static auto check = gmAddress::scan("81 E7 FF FF 03 00 8B");
			//	if (check)
			//	{
			//		memory::byte_patch::make(check.offset(2).as<uint32_t *>(), extended_sgg_sHashes_size)->apply();
			//	}
			//}

			//array_extender((void **)&extended_sgg_sIndices, extended_sgg_sIndices_size, "sgg::sIndices", patch_sgg_sIndices_usage);

			array_extender((void **)&extended_sgg_sStringsBuffer, extended_sgg_sStringsBuffer_size, "sgg::sStringsBuffer", patch_lea_sgg_sStringsBuffer_usage);

			array_extender((void **)&extended_sgg_sTempStringsBuffer, extended_sgg_sTempStringsBuffer_size, "sgg::sTempStringsBuffer", patch_lea_sgg_sTempStringsBuffer_usage);

			extend_sgg_sStringsBuffer_and_sTempStringsBuffer_max_size();

			static auto ptr = gmAddress::scan("3D 00 00 90 00", "cmp     eax, 900000h | silencing String intern table running low on space message spam");
			if (ptr)
			{
			auto e8_call_ptr = ptr.offset(45).as<uint8_t *>();
				if (*e8_call_ptr != 0xE8)
				{
				LOG(ERROR) << "Failed silencing String intern table running low on space message spam - unexpected "
				              "instruction at call site";
				}
				else
				{
					for (size_t i = 0; i < 5; i++)
					{
						ForceWrite<uint8_t>(*(e8_call_ptr + i), 0x90);
					}
				}
			}

			sgg_sInitialized = big::hades2_symbol_to_address["sgg::sInitialized"].as<bool *>();
			XXH_INLINE_XXH3_64bits = big::hades2_symbol_to_address["XXH_INLINE_XXH3_64bits"].as_func<uint64_t(const char *input, size_t len)>();
			InitializeStringIntern = big::hades2_symbol_to_address["sgg::HashGuid::InitializeStringIntern"].as_func<void()>();
			sgg_sBufferLen = big::hades2_symbol_to_address["sgg::sBufferLen"].as<uint32_t *>();
			sgg_sItemCount = big::hades2_symbol_to_address["sgg::sItemCount"].as<uint32_t *>();

		extended_sgg_sHashes  = (uint64_t *)_aligned_malloc(extended_sgg_sHashes_size, 16);
			extended_sgg_sIndices = (uint32_t *)_aligned_malloc(extended_sgg_sIndices_size, 16);
			memset(extended_sgg_sHashes, 0, extended_sgg_sHashes_size);
			memset(extended_sgg_sIndices, -1, extended_sgg_sIndices_size);
			memset(extended_sgg_sBuffer, 0, extended_sgg_sBuffer_size);

			{
				const auto ptr = big::hades2_symbol_to_address["sgg::HashGuid::StringIntern"];
				if (ptr)
				{
				big::hooking::detour_hook_helper::add_queue<hook_sgg_HashGuid_StringIntern>(
				    "sgg::HashGuid::StringIntern",
				    ptr);
				}
			}

			{
				const auto ptr = big::hades2_symbol_to_address["sgg::HashGuid::Lookup"];
				if (ptr)
				{
				big::hooking::detour_hook_helper::add_queue<hook_sgg_HashGuid_Lookup>("sgg::HashGuid::Lookup", ptr);
				}
			}
		}

		const auto initRenderer_ptr = big::hades2_symbol_to_address["initRenderer"];
		if (initRenderer_ptr)
		{
			big::hooking::detour_hook_helper::add_queue<hook_initRenderer>("initRenderer", initRenderer_ptr);
		}

		const auto backtrace_initializeCrashpad_ptr = big::hades2_symbol_to_address["backtrace::initializeCrashpad"];
		if (backtrace_initializeCrashpad_ptr)
		{
			big::hooking::detour_hook_helper::add_queue<hook_skipcrashpadinit>("backtrace::initializeCrashpad",
			                                                                 backtrace_initializeCrashpad_ptr.as_func<bool()>());
		}

		{
			// The game has Lua statically linked, which means all of Lua's global state lives in its .data section.
			// Because these globals are not exposed, we can't directly call into the game's Lua functions from our DLL.
			// To work with Lua ourselves, we also have to statically link against a Lua library - but that creates a
			// separate set of global variables. To solve this, we hook most of our own set of statically linked
			// Lua functions and redirect them so they call the game's Lua functions instead,
			// ensuring both sides are in sync.
			// clang-format off
			big::hooking::detour_hook_helper::add_queue<hook_luaL_checkversion_>("", &luaL_checkversion_);
			big::hooking::detour_hook_helper::add_queue<hook_luaV_execute>("", &luaV_execute);
			big::hooking::detour_hook_helper::add_queue<hook_lua_callk>("", &lua_callk);
			big::hooking::detour_hook_helper::add_queue<hook_lua_checkstack>("", &lua_checkstack);
			big::hooking::detour_hook_helper::add_queue<hook_lua_createtable>("", &lua_createtable);
			big::hooking::detour_hook_helper::add_queue<hook_lua_error>("", &lua_error);
			big::hooking::detour_hook_helper::add_queue<hook_lua_gc>("", &lua_gc);
			big::hooking::detour_hook_helper::add_queue<hook_lua_getfield>("", &lua_getfield);
			big::hooking::detour_hook_helper::add_queue<hook_lua_getmetatable>("", &lua_getmetatable);
			big::hooking::detour_hook_helper::add_queue<hook_lua_gettable>("", &lua_gettable);
			big::hooking::detour_hook_helper::add_queue<hook_lua_insert>("", &lua_insert);
			big::hooking::detour_hook_helper::add_queue<hook_lua_pcallk>("", &lua_pcallk);
			big::hooking::detour_hook_helper::add_queue<hook_lua_load>("", &lua_load);
			big::hooking::detour_hook_helper::add_queue<hook_luaL_ref>("", &luaL_ref);
			big::hooking::detour_hook_helper::add_queue<hook_lua_rawseti>("", &lua_rawseti);
			big::hooking::detour_hook_helper::add_queue<hook_luaH_setint>("", &luaH_setint);
			big::hooking::detour_hook_helper::add_queue<hook_luaH_newkey>("", &luaH_newkey);
			big::hooking::detour_hook_helper::add_queue<hook_luaH_resize>("", &luaH_resize);
			big::hooking::detour_hook_helper::add_queue<hook_luaM_realloc_>("", &luaM_realloc_);
			big::hooking::detour_hook_helper::add_queue<hook_luaH_free>("", &luaH_free);
			big::hooking::detour_hook_helper::add_queue<hook_luaH_getn>("", &luaH_getn);
			big::hooking::detour_hook_helper::add_queue<hook_luaH_new>("", &luaH_new);
			big::hooking::detour_hook_helper::add_queue<hook_luaH_resizearray>("", &luaH_resizearray);
			// clang-format on

			// lovely injector integration
			big::hooking::detour_hook_helper::add_queue<hook_game_luaL_loadbufferx>("", big::hades2_symbol_to_address["luaL_loadbufferx"]);
			big::hooking::detour_hook_helper::add_queue<hook_game_lua_pcallk>("", big::hades2_symbol_to_address["lua_pcallk"]);

			//big::hooking::detour_hook_helper::add_queue<hook_sgg_WorkerManager_ExecuteCmd>("", big::hades2_symbol_to_address["sgg::WorkerManager::ExecuteCmd"]);
		}

		/*{
			static auto GUIComponentTextBox_ctor_ptr = gmAddress::scan("89 BB 2C 06 00 00", "sgg::GUIComponentTextBox::GUIComponentTextBox");
			if (GUIComponentTextBox_ctor_ptr)
			{
				static auto GUIComponentTextBox_ctor = GUIComponentTextBox_ctor_ptr.offset(-0x3B);
				static auto hook_ = hooking::detour_hook_helper::add_queue<sgg__GUIComponentTextBox__GUIComponentTextBox>("sgg__GUIComponentTextBox__GUIComponentTextBox", GUIComponentTextBox_ctor);
			}
		}*/

		{
			big::hooking::detour_hook_helper::add_queue<hook_sgg_IsContentFolderModified>("", big::hades2_symbol_to_address["sgg::IsContentFolderModified"]);
		}

		{
		static auto GUIComponentTextBox_update_ptr = big::hades2_symbol_to_address["sgg::GUIComponentTextBox::Update"];
			if (GUIComponentTextBox_update_ptr)
			{
				static auto GUIComponentTextBox_update = GUIComponentTextBox_update_ptr;
				static auto hook_ = hooking::detour_hook_helper::add_queue<sgg__GUIComponentTextBox__Update>(
				    "sgg__GUIComponentTextBox__Update",
				    GUIComponentTextBox_update);
			}
		}

		{
			static auto GUIComponentTextBox_dctor_ptr =
			    big::hades2_symbol_to_address["sgg::GUIComponentTextBox::~GUIComponentTextBox"];
			if (GUIComponentTextBox_dctor_ptr)
			{
				static auto GUIComponentTextBox_dctor = GUIComponentTextBox_dctor_ptr;
				static auto hook_ = hooking::detour_hook_helper::add_queue<sgg__GUIComponentTextBox__GUIComponentTextBox_dctor>("sgg__GUIComponentTextBox__GUIComponentTextBox_dctor", GUIComponentTextBox_dctor);
			}
		}

		{
			static auto GUIComponentButton_OnSelected_ptr =
			    big::hades2_symbol_to_address["sgg::GUIComponentButton::OnSelected"];
			if (GUIComponentButton_OnSelected_ptr)
			{
				static auto GUIComponentButton_OnSelected = GUIComponentButton_OnSelected_ptr;
				static auto hook_ = hooking::detour_hook_helper::add_queue<hook_GUIComponentButton_OnSelected>(
				    "GUIComponentButton_OnSelected",
				    GUIComponentButton_OnSelected);
			}
		}

		{
		static auto read_anim_data_ptr = big::hades2_symbol_to_address["sgg::GameDataManager::ReadAllAnimationData"];
			if (read_anim_data_ptr)
			{
				static auto read_anim_data = read_anim_data_ptr.as_func<void()>();

				static auto hook_ =
				    hooking::detour_hook_helper::add_queue<hook_ReadAllAnimationData>("ReadAllAnimationData Hook", read_anim_data);
			}
		}

		{
		static auto read_anim_data_ptr = big::hades2_symbol_to_address["sgg::Granny3D::LoadAllModelAndAnimationData"];
			if (read_anim_data_ptr)
			{
				static auto read_anim_data = read_anim_data_ptr.as_func<void()>();

				static auto hook_ = hooking::detour_hook_helper::add_queue<hook_LoadAllModelAndAnimationData>(
				    "LoadAllModelAndAnimationData Hook",
				    read_anim_data);
			}
		}

		{
		static auto hook_ = hooking::detour_hook_helper::add_queue<hook_PlayerHandleInput>("Player HandleInput Hook", big::hades2_symbol_to_address["sgg::Player::HandleInput"]);
		}

		{
			static auto hook_ =
			    hooking::detour_hook_helper::add_queue<hook_ConfigOption_registerField_bool>("registerField<bool> hook", big::hades2_symbol_to_address["sgg::registerField<bool>"]);
		}

				{
		static auto hook_ = hooking::detour_hook_helper::add_queue<hook_sgg_ScriptManager_InitLua>("", big::hades2_symbol_to_address["sgg::ScriptManager::InitLua"]);

			static auto hook2_ = hooking::detour_hook_helper::add_queue<hook_sgg_App_Initialize>("", big::hades2_symbol_to_address["sgg::App::Initialize"]);

			// ; sgg::ThreadBumpAllocator *__fastcall sgg::App::GetThreadFrameAllocator(sgg::App *this)
			// Extend the allocator max size cause mods hit the limit otherwise.
			constexpr size_t original_threadframe_allocator_size             = 0x03'20'00;
			constexpr size_t extended_threadframe_allocator_size_20mb_in_hex = 0x01'40'00'00;

		const auto patch_1 = gmAddress::scan("B9 00 20 03 00", "mov     ecx, 32000h     ; Size");
			if (patch_1)
			{
				ForceWrite<uint32_t>(*patch_1.offset(1).as<uint32_t *>(), extended_threadframe_allocator_size_20mb_in_hex);
			}
			else
			{
				LOG(ERROR) << "Patch 1 for sgg::App::GetThreadFrameAllocator failed.";
			}
			const auto patch_2 = gmAddress::scan("48 C7 43 18 00 20 03 00", "mov     qword ptr [rbx+18h], 32000h");
			if (patch_2)
			{
				ForceWrite<uint32_t>(*patch_2.offset(4).as<uint32_t *>(), extended_threadframe_allocator_size_20mb_in_hex);
			}
			else
			{
				LOG(ERROR) << "Patch 2 for sgg::App::GetThreadFrameAllocator failed.";
			}
		}

		{
			static auto hook_ =
			    hooking::detour_hook_helper::add_queue<hook_PlatformAnalytics_Start>("PlatformAnalytics Start", big::hades2_symbol_to_address["sgg::PlatformAnalytics::Start"]);
		}

		{
			static auto hook_ =
			    hooking::detour_hook_helper::add_queue<hook_sgg_DetectFreezeThread>("hook_sgg_DetectFreezeThread", big::hades2_symbol_to_address["sgg::DetectFreezeThread"]);
		}

		{
			init_import_hooks();
		}

		{
			static auto ptr = big::hades2_symbol_to_address["sgg::LaunchBugReporter"];
			if (ptr)
			{
				static auto ptr_func = ptr;

				static auto hook_ = hooking::detour_hook_helper::add_queue<hook_disable_f10_launch>(
				    "sgg::LaunchBugReporter F10 Disabler Hook",
				    ptr_func);
			}
		}

		{
			static auto ptr = big::hades2_symbol_to_address["fsGetFilesWithExtension"];
			if (ptr)
			{
				static auto ptr_func = ptr;

				static auto hook_ = hooking::detour_hook_helper::add_queue<hook_fsGetFilesWithExtension_packages>(
				    "fsGetFilesWithExtension for packages and models",
				    ptr_func);
			}
		}

		{
			static auto ptr = big::hades2_symbol_to_address["ParseLuaErrorMessageAndAssert"];
			if (ptr)
			{
				static auto ptr_func = ptr;

				static auto hook_ = hooking::detour_hook_helper::add_queue<hook_ParseLuaErrorMessageAndAssert>(
				    "ParseLuaErrorMessageAndAssert for better lua stack traces",
				    ptr_func);
			}
		}

		{
			static auto ptr = big::hades2_symbol_to_address["sgg::Obstacle::IsObscuring"];
			if (ptr)
			{
				static auto ptr_func = ptr;

				static auto hook_ = hooking::detour_hook_helper::add_queue<hook_sgg_Obstacle_IsObscuring>("", ptr_func);			
			}
		}

		/*{
			static auto ptr = gmAddress::scan("E8 ? ? ? ? 90 49 8B CF", "ReadCSString");
			if (ptr)
			{
				static auto ptr_func = ptr.get_call();

				static auto hook_ =
				    hooking::detour_hook_helper::add<hook_ReadCSString>("ReadCSString for packages guid check", ptr_func);
			}
		}*/

		{
			static auto fsAppendPathComponent_ptr = big::hades2_symbol_to_address["fsAppendPathComponent"];
			if (fsAppendPathComponent_ptr)
			{
				static auto fsAppendPathComponent = fsAppendPathComponent_ptr.as_func<void(const char *, const char *, char *)>();

				static auto hook_once = big::hooking::detour_hook_helper::add_queue<hook_fsAppendPathComponent_packages>(
				    "hook_fsAppendPathComponent for packages and models",
				    fsAppendPathComponent);
			}
			else
			{
				LOG(ERROR) << "hook_fsAppendPathComponent for packages and models failure";
			}
		}

		/*big::hooking::detour_hook_helper::add_now<hook_SGD_Deserialize_ThingDataDef>(
		    "void __fastcall sgg::SGD_Deserialize(sgg::SGD_Context *ctx, int loc, sgg::ThingDataDef *val)",
		    gmAddress::scan("44 88 74 24 21", "SGD_Deserialize ThingData").offset(-0x59));*/

		big::hooking::detour_hook_helper::execute_queue();

			    std::filesystem::path root_folder = paths::get_project_root_folder();
			    g_file_manager.init(root_folder);
			    paths::init_dump_file_path();

			    big::config::init_general();

	static auto logger_instance = std::make_unique<logger>(rom::g_project_name, g_file_manager.get_project_file("./LogOutput.log"));

			    static struct logger_cleanup
			    {
				    ~logger_cleanup()
				    {
					    Logger::Destroy();
				    }
			    } g_logger_cleanup;

			    LOG(INFO) << rom::g_project_name;
			    LOGF(INFO, "Build v{} (Commit {})", big::version::VERSION_NUMBER, version::GIT_SHA1);

			    // TODO: move this to own file, make sure it's called early enough so that it happens before the initial GameReadData call.
			    for (const auto &entry :
			         std::filesystem::recursive_directory_iterator(g_file_manager.get_project_folder("plugins_data").get_path(), std::filesystem::directory_options::skip_permission_denied | std::filesystem::directory_options::follow_directory_symlink))
			    {
				    if (entry.path().extension() == ".pkg" || entry.path().extension() == ".pkg_manifest")
				    {
					    additional_package_files.emplace((char *)entry.path().filename().u8string().c_str(),
					                                     (char *)entry.path().u8string().c_str());

					    LOG(INFO) << "Adding to package files: " << (char *)entry.path().u8string().c_str();
				    }
				    else if (ends_with((char *)entry.path().u8string().c_str(), ".gpk"))
				    {
					    additional_granny_files.emplace((char *)entry.path().filename().u8string().c_str(),
					                                    (char *)entry.path().u8string().c_str());

					    LOG(INFO) << "Adding to granny files: " << (char *)entry.path().u8string().c_str();
				    }
			    }

				const auto res = lovely_init((const char *)g_file_manager.get_project_folder("plugins").get_path().u8string().c_str());
			    LOG(INFO) << "lovely_init returned " << (int32_t)res;

#ifdef FINAL
			    LOG(INFO) << "This is a final build";
#endif

	static auto thread_pool_instance = std::make_unique<thread_pool>();
			    LOG(INFO) << "Thread pool initialized.";

	static auto byte_patch_manager_instance = std::make_unique<byte_patch_manager>();
			    LOG(INFO) << "Byte Patch Manager initialized.";

	static auto hooking_instance = std::make_unique<hooking>();
			    LOG(INFO) << "Hooking initialized.";

			    big::hades::init_hooks();

	static auto renderer_instance = std::make_unique<renderer>();
			    LOG(INFO) << "Renderer initialized.";

			    hotkey::init_hotkeys();

			    if (!g_abort)
			    {
				    g_hooking->enable();
				    LOG(INFO) << "Hooking enabled.";
			    }

			    asi_loader::init(g_hmodule);

			    g_running = true;

			    if (g_abort)
			    {
				    LOG(ERROR) << rom::g_project_name << "failed to init properly, exiting.";
				    g_running = false;
			    }

				LOG(INFO) << "Running.";
}

BOOL APIENTRY DllMain(HMODULE hmod, DWORD reason, PVOID)
{
	big::g_hmodule = hmod;

	my_main();

	return true;
}
