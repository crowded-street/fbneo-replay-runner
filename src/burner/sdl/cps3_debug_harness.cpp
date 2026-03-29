#include "cps3_debug_harness.h"
#include "burner.h"
#include "cps3.h"

#include <SDL.h>

#include <signal.h>

static const UINT32 main_ram_base = 0x02000000;

static UINT8 read_u8(const Cps3DebugBreakpointContext* context, UINT32 sh2_address) {
    if (context == NULL || context->main_ram == NULL || context->main_ram_size <= 0) {
        return 0;
    }

    if (sh2_address < main_ram_base) {
        return 0;
    }

    const UINT32 offset = sh2_address - main_ram_base;
    if (offset >= (UINT32)context->main_ram_size) {
        return 0;
    }

#ifdef LSB_FIRST
    return context->main_ram[offset ^ 0x03];
#else
    return context->main_ram[offset];
#endif
}

static UINT16 read_u16(const Cps3DebugBreakpointContext* context, UINT32 sh2_address) {
    if (context == NULL || context->main_ram == NULL || context->main_ram_size <= 0) {
        return 0;
    }

    if (sh2_address < main_ram_base) {
        return 0;
    }

    const UINT32 offset = sh2_address - main_ram_base;
    if (offset + sizeof(UINT16) > (UINT32)context->main_ram_size) {
        return 0;
    }

#ifdef LSB_FIRST
    return *(const UINT16*)(context->main_ram + (offset ^ 0x02));
#else
    return *(const UINT16*)(context->main_ram + offset);
#endif
}

static UINT32 read_u32(const Cps3DebugBreakpointContext* context, UINT32 sh2_address) {
    if (context == NULL || context->main_ram == NULL || context->main_ram_size <= 0) {
        return 0;
    }

    if (sh2_address < main_ram_base) {
        return 0;
    }

    const UINT32 offset = sh2_address - main_ram_base;
    if (offset + sizeof(UINT32) > (UINT32)context->main_ram_size) {
        return 0;
    }

    return *(const UINT32*)(context->main_ram + offset);
}

void Cps3DebugHarnessOnExec(UINT32 current_pc, UINT32 branch_target_pc, UINT32 delay_slot_pc) {
    Cps3DebugBreakpointContext context;
    context.current_pc = current_pc;
    context.branch_target_pc = branch_target_pc;
    context.delay_slot_pc = delay_slot_pc;
    context.main_ram_size = cps3GetMainRamSize();
    context.main_ram = cps3GetMainRam();

    if (Sh2GetRegisters(0, &context.regs) != 0) {
        memset(&context.regs, 0, sizeof(context.regs));
    }

	// Write code here to debug the CPS3 executable
}
