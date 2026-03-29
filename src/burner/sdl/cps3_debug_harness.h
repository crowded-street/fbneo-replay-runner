#pragma once

#include "burnint.h"
#include "sh2_intf.h"

struct Cps3DebugBreakpointContext {
	UINT32 current_pc;
	UINT32 branch_target_pc;
	UINT32 delay_slot_pc;
	const UINT8* main_ram;
	INT32 main_ram_size;
	Sh2RegisterSnapshot regs;
};

extern volatile UINT32 gCps3DebugBreakpointPc;

void Cps3DebugHarnessOnExec(UINT32 current_pc, UINT32 branch_target_pc, UINT32 delay_slot_pc);
