@ Screens 16 to 18: the result of a misaligned load, in the ARM state and in the Thumb state.
@ See ../README.md, "Screens 16 to 18".
@
@ Each screen is one instruction: 16 is LDR, 17 is LDRH, and 18 is LDRSH. Each screen has four
@ rows: ARM at +1, ARM at +3, Thumb at +1, and Thumb at +3. Each row is the full 32-bit register
@ value after the load. Thus a row shows the upper bits, and not only the low byte.
@
@ The data is in WRAM, and not in FLASH1. WRAM permits 8-bit, 16-bit, and 32-bit access. Thus
@ the result is a property of the CPU alone, and not of the bus of a different region.
@
@ The loads run each time that the screen is drawn. They are deterministic, and they take
@ microseconds. Thus start.s needs no change, and each earlier routine keeps its address.
    .syntax unified
    .arm

    .include "constants.inc"

    .section .text, "ax"
    .global draw_load_screen

@ r0 = screen index (SCREEN_LOAD_LDR to SCREEN_LOAD_LDRSH). The caller cleared VRAM.
draw_load_screen:
    push {r4, r5, r6, lr}
    mov r5, r0

    @ The data: bytes 11 22 33 84 55 66 77 88, from WRAM_LOAD_DATA. Bit 7 of the byte at +3
    @ is set. Thus a sign extension of that byte is visible in the result.
    ldr r0, =WRAM_LOAD_DATA
    ldr r1, =0x84332211
    str r1, [r0]
    ldr r1, =0x88776655
    str r1, [r0, #4]

    @ The ARM results. Each row block is 16 bytes: ARM +1, ARM +3, Thumb +1, Thumb +3.
    ldr r4, =WRAM_LOAD_RESULTS
    ldr r1, [r0, #1]
    str r1, [r4, #0x00]
    ldr r1, [r0, #3]
    str r1, [r4, #0x04]
    ldrh r1, [r0, #1]
    str r1, [r4, #0x10]
    ldrh r1, [r0, #3]
    str r1, [r4, #0x14]
    ldrsh r1, [r0, #1]
    str r1, [r4, #0x20]
    ldrsh r1, [r0, #3]
    str r1, [r4, #0x24]

    @ The Thumb results. measure_loads_thumb keeps r0 and r4, and it returns with BX.
    ldr r1, =measure_loads_thumb
    mov lr, pc
    bx r1

    @ Row 0: the screen number in hex (10, 11, or 12).
    mov r0, #1
    mov r1, #0
    mov r2, #0
    bl draw_glyph
    sub r0, r5, #16
    mov r1, #0
    mov r2, #4
    bl draw_glyph

    @ Rows 7, 13, 19, and 25: the four results of this instruction.
    sub r5, r5, #SCREEN_LOAD_LDR
    ldr r4, =WRAM_LOAD_RESULTS
    add r4, r4, r5, lsl #4
    mov r6, #7
dls_row:
    ldr r0, [r4], #4
    mov r1, r6
    mov r2, #0
    bl draw_hex_u32
    add r6, r6, #6
    cmp r6, #31
    blo dls_row

    pop {r4, r5, r6, lr}
    bx lr
    .ltorg

@ r0 = WRAM_LOAD_DATA, r4 = WRAM_LOAD_RESULTS. Thumb has no LDRSH with an immediate offset,
@ thus this routine uses the register-offset form of each of the three instructions.
    .thumb
    .thumb_func
measure_loads_thumb:
    movs r2, #1
    movs r3, #3
    ldr r1, [r0, r2]
    str r1, [r4, #0x08]
    ldr r1, [r0, r3]
    str r1, [r4, #0x0C]
    ldrh r1, [r0, r2]
    str r1, [r4, #0x18]
    ldrh r1, [r0, r3]
    str r1, [r4, #0x1C]
    ldrsh r1, [r0, r2]
    str r1, [r4, #0x28]
    ldrsh r1, [r0, r3]
    str r1, [r4, #0x2C]
    bx lr
    .align 2
