@ Screens 19 to 28: the CPU rules of GBATEK, on real hardware.
@ See ../README.md, "Screens 19 to 28". Screen 29 is the probe of a misaligned write to R15. It
@ runs a probe only when a person presses Down. See probe_run below.
@
@ Each screen runs one test each time that the screen is drawn. The test writes four words to
@ WRAM_CPU_RESULTS, and the screen shows them as four rows. The tests take no interrupts, and they
@ write no flash. The test data is in WRAM, because FLASH1 permits no 8-bit read.
@
@ The assembler does not accept some of the tested encodings, or it gives a warning for them.
@ Those instructions are .word or .short values, each with the instruction in a comment.
@
@ This code is in the second 8KB block of the app (see link.ld). Thus each routine in the first
@ block keeps its address.
    .syntax unified
    .arm

    .include "constants.inc"

    .section .cpu, "ax"
    .global draw_cpu_screen

@ r0 = screen index (SCREEN_CPU_FIRST to SCREEN_LAST). The caller cleared VRAM.
draw_cpu_screen:
    push {r4, r5, r6, lr}
    mov r5, r0

    @ Clear the four rows. Thus a row that a test does not write shows 0, and not an old value.
    ldr r4, =WRAM_CPU_RESULTS
    mov r1, #0
    str r1, [r4]
    str r1, [r4, #4]
    str r1, [r4, #8]
    str r1, [r4, #12]

    @ Run the test of this screen. Each test gets r0 = WRAM_CPU_RESULTS.
    sub r1, r5, #SCREEN_CPU_FIRST
    ldr r2, =cpu_tests
    ldr r2, [r2, r1, lsl #2]
    mov r0, r4
    mov lr, pc
    bx r2

    @ Row 0: the screen number in hex (13 to 1D).
    mov r0, #1
    mov r1, #0
    mov r2, #0
    bl draw_glyph
    sub r0, r5, #16
    mov r1, #0
    mov r2, #4
    bl draw_glyph

    @ Rows 7, 13, 19, and 25: the four results.
    ldr r4, =WRAM_CPU_RESULTS
    mov r6, #7
dcs_row:
    ldr r0, [r4], #4
    mov r1, r6
    mov r2, #0
    bl draw_hex_u32
    add r6, r6, #6
    cmp r6, #31
    blo dcs_row

    pop {r4, r5, r6, lr}
    bx lr
    .ltorg

@ The tests, in screen order. Each test keeps r4 to r11, and it returns with BX.
cpu_tests:
    .word test_swp              @ 19
    .word test_pc_read          @ 20
    .word test_ldm_base_arm     @ 21
    .word test_ldm_base_thumb   @ 22
    .word test_push_misaligned  @ 23
    .word test_mul_thumb        @ 24
    .word test_mul_long         @ 25
    .word test_empty_list       @ 26
    .word test_misaligned_pc    @ 27
    .word test_undefined        @ 28
    .word probe_view            @ 29

@ r0 = address. Writes the bytes 11 22 33 84 55 66 77 88, which are the data of screens 16 to 18.
@ Uses r1 only.
put_load_data:
    ldr r1, =0x84332211
    str r1, [r0]
    ldr r1, =0x88776655
    str r1, [r0, #4]
    bx lr
    .ltorg

@ ---------------------------------------------------------------------------------------------
@ Screen 19: SWP at a misaligned address.
@ GBATEK: SWP reads the same way as LDR, thus it reads the aligned word and rotates it. It writes
@ the aligned word, with no rotation.
@ Row 1: the value that SWP reads at data + 1.
@ Row 2: the aligned word at data + 0 after that SWP. SWP writes A1B2C3D4.
@ Row 3: the aligned word at data + 4 after that SWP.
@ Row 4: the value that SWP reads at data + 3.
test_swp:
    push {r4, r5, lr}
    mov r4, r0
    ldr r5, =WRAM_CPU_DATA
    ldr r2, =0xA1B2C3D4

    mov r0, r5
    bl put_load_data
    add r3, r5, #1
    swp r1, r2, [r3]
    str r1, [r4]
    ldr r1, [r5]
    str r1, [r4, #4]
    ldr r1, [r5, #4]
    str r1, [r4, #8]

    mov r0, r5
    bl put_load_data
    add r3, r5, #3
    swp r1, r2, [r3]
    str r1, [r4, #12]

    pop {r4, r5, lr}
    bx lr
    .ltorg

@ ---------------------------------------------------------------------------------------------
@ Screen 20: the value of R15 as an operand, in the ARM state.
@ GBATEK: PC+12 for an ALU operand when the shift amount is in a register, and for the stored
@ register of STR and STRH. PC+8 for the other forms.
@ Each byte of a row is the value minus the address of the instruction.
@ Row 1, from the high byte: ADD r1,PC,r3,LSL r2. MOV r1,PC,LSL r2. ADD r1,PC,r3. MOV r1,PC.
@ Row 2, from the high byte: STR PC. STRH PC. STMIA {PC}. The low byte is not used.
@ Rows 3 and 4 are not used.
test_pc_read:
    push {r4, r5, r6, lr}
    mov r4, r0
    ldr r5, =WRAM_CPU_DATA
    mov r2, #0
    mov r3, #0

    mov r6, #0
    adr r0, 1f
1:  .word 0xE08F1213            @ add r1, pc, r3, lsl r2
    sub r1, r1, r0
    and r1, r1, #0xFF
    orr r6, r6, r1, lsl #24
    adr r0, 1f
1:  .word 0xE1A0121F            @ mov r1, pc, lsl r2
    sub r1, r1, r0
    and r1, r1, #0xFF
    orr r6, r6, r1, lsl #16
    adr r0, 1f
1:  add r1, pc, r3
    sub r1, r1, r0
    and r1, r1, #0xFF
    orr r6, r6, r1, lsl #8
    adr r0, 1f
1:  mov r1, pc
    sub r1, r1, r0
    and r1, r1, #0xFF
    orr r6, r6, r1
    str r6, [r4]

    mov r6, #0
    adr r0, 1f
1:  .word 0xE585F000            @ str pc, [r5]
    ldr r1, [r5]
    sub r1, r1, r0
    and r1, r1, #0xFF
    orr r6, r6, r1, lsl #24
    adr r0, 1f
1:  .word 0xE1C5F0B0            @ strh pc, [r5]
    ldrh r1, [r5]
    sub r1, r1, r0
    and r1, r1, #0xFF
    orr r6, r6, r1, lsl #16
    adr r0, 1f
1:  .word 0xE8858000            @ stmia r5, {pc}
    ldr r1, [r5]
    sub r1, r1, r0
    and r1, r1, #0xFF
    orr r6, r6, r1, lsl #8
    str r6, [r4, #4]

    pop {r4, r5, r6, lr}
    bx lr
    .ltorg

@ ---------------------------------------------------------------------------------------------
@ Screen 21: ARM LDM and STM with writeback, where the base register is also in the list.
@ GBATEK (ARMv4): LDM does no writeback. STM stores the old base if the base is the first register
@ of the list, and the new base if not.
@ The data is 11111111 22222222.
@ Row 1: LDMIA r0!,{r0,r1}: the value of r0. The loaded value is 11111111.
@ Row 2: LDMIA r1!,{r0,r1}: the value of r1. The loaded value is 22222222.
@ Row 3: STMIA r0!,{r0,r1}: the stored r0 minus the old base. 0 is the old base, and 8 is the new.
@ Row 4: STMIA r1!,{r0,r1}: the stored r1 minus the old base.
test_ldm_base_arm:
    push {r4, r5, lr}
    mov r4, r0
    ldr r5, =WRAM_CPU_DATA
    ldr r2, =0x11111111
    ldr r3, =0x22222222
    stmia r5, {r2, r3}

    mov r0, r5
    .word 0xE8B00003            @ ldmia r0!, {r0, r1}
    str r0, [r4]
    mov r1, r5
    .word 0xE8B10003            @ ldmia r1!, {r0, r1}
    str r1, [r4, #4]

    mov r0, r5
    mov r1, r3
    .word 0xE8A00003            @ stmia r0!, {r0, r1}
    ldr r1, [r5]
    sub r1, r1, r5
    str r1, [r4, #8]
    mov r0, r2
    mov r1, r5
    .word 0xE8A10003            @ stmia r1!, {r0, r1}
    ldr r1, [r5, #4]
    sub r1, r1, r5
    str r1, [r4, #12]

    pop {r4, r5, lr}
    bx lr
    .ltorg

@ ---------------------------------------------------------------------------------------------
@ Screen 22: the same as screen 21, with the Thumb LDMIA and STMIA.
@ GBATEK: the same rules as ARM. A Thumb LDM does no writeback on ARMv4 and on ARMv5.
test_ldm_base_thumb:
    push {r4, r5, lr}
    mov r4, r0
    ldr r5, =WRAM_CPU_DATA
    ldr r2, =0x11111111
    ldr r3, =0x22222222
    stmia r5, {r2, r3}
    ldr r0, =ldm_base_thumb
    mov lr, pc
    bx r0
    pop {r4, r5, lr}
    bx lr
    .ltorg

    .thumb
    .thumb_func
ldm_base_thumb:                 @ r2 = 11111111, r3 = 22222222, r4 = results, r5 = data
    adds r0, r5, #0
    .short 0xC803               @ ldmia r0!, {r0, r1}
    str r0, [r4, #0]
    adds r1, r5, #0
    .short 0xC903               @ ldmia r1!, {r0, r1}
    str r1, [r4, #4]

    adds r0, r5, #0
    adds r1, r3, #0
    .short 0xC003               @ stmia r0!, {r0, r1}
    ldr r1, [r5, #0]
    subs r1, r1, r5
    str r1, [r4, #8]
    adds r0, r2, #0
    adds r1, r5, #0
    .short 0xC103               @ stmia r1!, {r0, r1}
    ldr r1, [r5, #4]
    subs r1, r1, r5
    str r1, [r4, #12]
    bx lr
    .align 2
    .arm

@ ---------------------------------------------------------------------------------------------
@ Screen 23: a Thumb PUSH with a misaligned SP.
@ GBATEK: PUSH writes to the aligned address. The test sets SP to data + 6, and it pushes 11223344.
@ Thus the write address is data + 2. The data is 0 before the test.
@ Row 1: the aligned word at data + 0.
@ Row 2: the aligned word at data + 4.
@ Row 3: SP after the PUSH, minus data.
@ Row 4: the value that a POP then reads, from the same SP.
test_push_misaligned:
    push {r4, r5, lr}
    mov r4, r0
    ldr r5, =WRAM_CPU_DATA
    mov r1, #0
    str r1, [r5]
    str r1, [r5, #4]
    str r1, [r5, #8]
    ldr r1, =0x11223344
    mov r0, r5
    ldr r3, =push_misaligned_thumb
    mov lr, pc
    bx r3
    ldr r1, [r5]
    str r1, [r4]
    ldr r1, [r5, #4]
    str r1, [r4, #4]
    str r0, [r4, #8]
    str r2, [r4, #12]
    pop {r4, r5, lr}
    bx lr
    .ltorg

    .thumb
    .thumb_func
push_misaligned_thumb:          @ r0 = data, r1 = value -> r0 = SP after the PUSH minus data, r2 = POP value
    mov r12, sp
    adds r3, r0, #6
    mov sp, r3
    push {r1}
    mov r3, sp
    pop {r2}
    mov sp, r12
    subs r0, r3, r0
    bx lr
    .align 2
    .arm

@ ---------------------------------------------------------------------------------------------
@ Screen 24: the cost of a Thumb MUL.
@ GBATEK: the cost depends on the high bytes of the incoming Rd, because Thumb MUL Rd,Rs is ARM
@ MULS Rd,Rs,Rd. This emulator uses Rs. Each row is the Timer0 ticks of LOOP_N loops.
@ Row 1: Rd = 5, Rs = 12345678.
@ Row 2: Rd = 12345678, Rs = 5.
@ Row 3: Rd = 5, Rs = 5. This is the m = 1 reference.
@ Row 4: Rd = 12345678, Rs = 12345678. This is the m = 4 reference.
test_mul_thumb:
    push {r4, r5, r6, lr}
    mov r6, r0
    mov r5, #5
    ldr r2, =0x12345678
    bl time_mul_thumb
    str r0, [r6]
    ldr r5, =0x12345678
    mov r2, #5
    bl time_mul_thumb
    str r0, [r6, #4]
    mov r5, #5
    mov r2, #5
    bl time_mul_thumb
    str r0, [r6, #8]
    ldr r5, =0x12345678
    ldr r2, =0x12345678
    bl time_mul_thumb
    str r0, [r6, #12]
    pop {r4, r5, r6, lr}
    bx lr
    .ltorg

time_mul_thumb:                 @ r5 = Rd, r2 = Rs -> r0 = Timer0 ticks
    push {r4, r7, lr}
    ldr r3, =LOOP_N
    ldr r4, =TIMER0_COUNT
    ldr r0, =mul_loop_thumb
    ldr r7, [r4]
    mov lr, pc
    bx r0
    ldr r0, [r4]
    sub r0, r7, r0
    mov r0, r0, lsl #16
    mov r0, r0, lsr #16
    pop {r4, r7, lr}
    bx lr
    .ltorg

    .thumb
    .thumb_func
mul_loop_thumb:                 @ r2 = Rs, r3 = count, r5 = Rd
1:  adds r1, r5, #0
    muls r1, r2, r1             @ MUL Rd=r1, Rs=r2
    subs r3, r3, #1
    bne 1b
    bx lr
    .align 2
    .arm

@ ---------------------------------------------------------------------------------------------
@ Screen 25: the cost of UMULL and SMULL.
@ GBATEK: an unsigned multiply stops early only for high bytes of Rs that are all zero. A signed
@ multiply also stops early for high bytes that are all one. This emulator uses the signed rule
@ for both. Each row is the Timer0 ticks of LOOP_N loops.
@ Row 1: UMULL, Rs = 000000FF. This is the m = 1 reference.
@ Row 2: UMULL, Rs = FFFFFFFF.
@ Row 3: SMULL, Rs = FFFFFFFF. m = 1 for both rules.
@ Row 4: UMULL, Rs = 12345678. This is the m = 4 reference.
test_mul_long:
    push {r4, r5, r6, lr}
    mov r5, r0
    mov r6, #0xFF
    bl time_umull
    str r0, [r5]
    mvn r6, #0
    bl time_umull
    str r0, [r5, #4]
    mvn r6, #0
    bl time_smull
    str r0, [r5, #8]
    ldr r6, =0x12345678
    bl time_umull
    str r0, [r5, #12]
    pop {r4, r5, r6, lr}
    bx lr
    .ltorg

    .macro TIME_LONG_MUL name, insn
\name:                          @ r6 = Rs -> r0 = Timer0 ticks
    push {r4, r7, lr}
    ldr r3, =LOOP_N
    ldr r4, =TIMER0_COUNT
    mov r0, #0x55               @ Rm
    ldr r7, [r4]
1:  \insn r1, r2, r0, r6
    subs r3, r3, #1
    bne 1b
    ldr r0, [r4]
    sub r0, r7, r0
    mov r0, r0, lsl #16
    mov r0, r0, lsr #16
    pop {r4, r7, lr}
    bx lr
    .ltorg
    .endm

    TIME_LONG_MUL time_umull, umull
    TIME_LONG_MUL time_smull, smull

@ ---------------------------------------------------------------------------------------------
@ Screen 26: LDM and STM with an empty register list.
@ GBATEK (ARMv4): the instruction transfers R15, and the base moves by 0x40. The base is the
@ middle of a buffer of 0x80 bytes. Thus each address 0x40 below or above the base is in the
@ buffer.
@ In each row, the high half is the base after the instruction, minus the base before.
@ Row 1: ARM STMIA r0!,{}. The low half is the word at the base minus the address of the STM.
@        A low half of 0000 means that no store occurred at the base.
@ Row 2: ARM LDMIA r0!,{}. The low half is 1 if the instruction loaded the PC, and 0 if not.
@ Row 3: Thumb STMIA r0!,{}, the same as row 1.
@ Row 4: Thumb LDMIA r0!,{}, the same as row 2.
@ For an LDM, each word of the buffer holds the address of the target. Thus a load of the PC from
@ any word of the buffer goes to the target.
test_empty_list:
    push {r4, r5, r6, r7, lr}
    mov r4, r0
    ldr r5, =WRAM_CPU_BUF_MID

    mov r1, #0
    bl fill_cpu_buf
    mov r0, r5
    adr r6, 1f
1:  .word 0xE8A00000            @ stmia r0!, {}
    bl empty_row_store
    str r0, [r4]

    adr r1, el_arm_target
    bl fill_cpu_buf
    mov r7, #0
    mov r0, r5
    .word 0xE8B00000            @ ldmia r0!, {}
    b 2f
el_arm_target:
    mov r7, #1
2:  sub r0, r0, r5
    orr r0, r7, r0, lsl #16
    str r0, [r4, #4]

    mov r1, #0
    bl fill_cpu_buf
    mov r0, r5
    ldr r3, =empty_stm_thumb
    mov lr, pc
    bx r3
    ldr r6, =empty_stm_thumb_insn
    bic r6, r6, #1
    bl empty_row_store
    str r0, [r4, #8]

    ldr r1, =el_thumb_target
    bl fill_cpu_buf
    mov r0, r5
    ldr r3, =empty_ldm_thumb
    mov lr, pc
    bx r3
    sub r0, r0, r5
    orr r0, r7, r0, lsl #16
    str r0, [r4, #12]

    pop {r4, r5, r6, r7, lr}
    bx lr
    .ltorg

@ r1 = value. Writes r1 to each word of WRAM_CPU_BUF. Uses r2 and r3.
fill_cpu_buf:
    ldr r2, =WRAM_CPU_BUF
    mov r3, #32
1:  str r1, [r2], #4
    subs r3, r3, #1
    bne 1b
    bx lr
    .ltorg

@ r0 = base after the STM, r5 = base before, r6 = address of the STM -> r0 = row. Uses r1 and r2.
empty_row_store:
    sub r2, r0, r5
    ldr r1, [r5]
    cmp r1, #0
    subne r1, r1, r6
    mov r1, r1, lsl #16
    mov r1, r1, lsr #16
    orr r0, r1, r2, lsl #16
    bx lr

    .thumb
    .thumb_func
empty_stm_thumb:                @ r0 = base -> r0 = base after
empty_stm_thumb_insn:
    .short 0xC000               @ stmia r0!, {}
    bx lr

    .thumb_func
empty_ldm_thumb:                @ r0 = base -> r0 = base after, r7 = 1 if the PC was loaded
    movs r7, #0
    .short 0xC800               @ ldmia r0!, {}
    b 1f
    .thumb_func
el_thumb_target:
    movs r7, #1
1:  bx lr
    .align 2
    .arm

@ ---------------------------------------------------------------------------------------------
@ Screen 27: a write of a misaligned address to R15, in the ARM state.
@ GBATEK: R15 is always force aligned. Each test writes the address mpc_pad + 2 to R15. Each test
@ runs with the traps of screen 29 (see trap_run), thus no result stops the device.
@ Row 1: MOV PC,r2. Row 2: LDR PC,[r5]. Row 3: LDMIA r5,{PC}. Row 4: BX r2, with bit 0 clear.
@ In each row, bits 24 to 31 are the result of trap_run (01 = the test returned, 02 = undefined
@ instruction, and the other values of screen 29). Bits 0 to 23 are row 2 of that result: R15 minus
@ mpc_pad for a return, or LR minus mpc_pad for a trap.
@ The words at mpc_pad give a different row for each fetch rule:
@   - R15 force aligned: MOV r1,PC reads mpc_pad + 8, and BX r3 returns. 01000008.
@   - Bit 1 kept, and the aligned word: MOV r1,PC reads mpc_pad + A, and BX r3 returns. 0100000A.
@   - Bit 1 kept, and the halfword in both halves (FLASH1 on real hardware): E1A0E1A0, then
@     E12FE12F, then E7F0E7F0, which is undefined. A trap with LR = mpc_pad + 0xE. 0200000E.
test_misaligned_pc:
    push {r4, r5, r6, lr}
    mov r6, r0
    ldr r0, =mpc_mov
    bl trap_run
    bl mpc_row
    str r0, [r6]
    ldr r0, =mpc_ldr
    bl trap_run
    bl mpc_row
    str r0, [r6, #4]
    ldr r0, =mpc_ldm
    bl trap_run
    bl mpc_row
    str r0, [r6, #8]
    ldr r0, =mpc_bx
    bl trap_run
    bl mpc_row
    str r0, [r6, #12]
    @ trap_run wrote the rows of screen 29. Show "not run" on that screen again.
    bl probe_clear_rows
    pop {r4, r5, r6, lr}
    bx lr
    .ltorg

@ -> r0 = the row of screen 27: the result in bits 24 to 31, and row 2 of trap_run in bits 0 to 23.
mpc_row:
    ldr r1, =WRAM_PROBE_ROWS
    ldr r0, [r1]
    mov r0, r0, lsr #16
    and r0, r0, #0xFF
    ldr r2, [r1, #4]
    bic r2, r2, #0xFF000000
    orr r0, r2, r0, lsl #24
    bx lr
    .ltorg

mpc_mov:
    ldr r4, =mpc_pad
    add r2, r4, #2
    b probe_pc_mov
mpc_ldr:
    ldr r4, =mpc_pad
    add r2, r4, #2
    b probe_pc_ldr
mpc_ldm:
    ldr r4, =mpc_pad
    add r2, r4, #2
    b probe_pc_ldm
mpc_bx:
    ldr r4, =mpc_pad
    add r2, r4, #2
    b probe_pc_bx
    .ltorg

@ The target of screen 27. See test_misaligned_pc for the result of each fetch rule.
    .align 2
mpc_pad:
    .word 0xE1A0100F            @ mov r1, pc
    .word 0xE12FFF13            @ bx r3
    .word 0xE7F0E7F0, 0xE7F0E7F0, 0xE7F0E7F0, 0xE7F0E7F0

@ ---------------------------------------------------------------------------------------------
@ Screen 28: an undefined instruction.
@ GBATEK: the CPU enters the undefined-instruction exception, in Undefined mode, at the vector
@ 0x04. This test puts the address of und_handler in the kernel slot that the vector reads. It
@ restores the slot after the test. If the slot does not keep the written value, the test does
@ not run, and each row is EEEEEEEE.
@ Row 1: ARM 0xE7F000F0. LR_und minus the address of the instruction. 0 if the handler did not run.
@ Row 2: ARM 0xE7F000F0. Bits 8 to 15 are the low byte of CPSR in the handler. Bits 0 to 7 are
@        the low byte of SPSR_und.
@ Row 3: Thumb 0xDE00, the same as row 1.
@ Row 4: Thumb 0xDE00, the same as row 2.
test_undefined:
    push {r4, r5, r6, r7, r8, lr}
    mov r4, r0
    ldr r3, =KERNEL_UND_SLOT
    ldr r8, [r3]
    ldr r0, =und_handler
    str r0, [r3]
    ldr r1, [r3]
    cmp r1, r0
    bne und_no_slot

    @ A CPU that does not decode 0xE7F000F0 as undefined decodes it as LDRB r0,[r0,r0,ROR r0]!.
    @ r0 = 0 keeps that access in RAM.
    mov r5, #0
    mov r6, #0
    mov r7, #0
    mov r0, #0
    adr r2, 1f
1:  .word 0xE7F000F0            @ undefined
    bl und_rows
    str r0, [r4]
    str r1, [r4, #4]

    mov r5, #0
    mov r6, #0
    mov r7, #0
    ldr r0, =und_thumb
    mov lr, pc
    bx r0
    ldr r2, =und_thumb_insn
    bic r2, r2, #1
    bl und_rows
    str r0, [r4, #8]
    str r1, [r4, #12]
    b und_restore

und_no_slot:
    ldr r0, =0xEEEEEEEE
    str r0, [r4]
    str r0, [r4, #4]
    str r0, [r4, #8]
    str r0, [r4, #12]
und_restore:
    ldr r3, =KERNEL_UND_SLOT
    str r8, [r3]
    pop {r4, r5, r6, r7, r8, lr}
    bx lr
    .ltorg

@ r2 = address of the instruction, r5 = CPSR in the handler, r6 = SPSR_und, r7 = LR_und.
@ -> r0 = the LR_und row, r1 = the CPSR and SPSR row.
und_rows:
    cmp r7, #0
    subne r7, r7, r2
    and r1, r6, #0xFF
    and r5, r5, #0xFF
    orr r1, r1, r5, lsl #8
    mov r0, r7
    bx lr

@ The undefined-instruction handler. It runs in Undefined mode. It records its state in r5 to r7,
@ which Undefined mode shares with User mode. It returns to the next instruction.
und_handler:
    mrs r5, cpsr
    mrs r6, spsr
    mov r7, lr
    movs pc, lr

    .thumb
    .thumb_func
und_thumb:
und_thumb_insn:
    .short 0xDE00               @ undefined
    mov r8, r8                  @ A CPU that decodes 0xDE00 as a branch to +4 does not execute this.
    bx lr
    .align 2
    .arm

@ =============================================================================================
@ Screen 29: the probe of a misaligned write to R15.
@
@ The kernel sends the undefined-instruction exception, the prefetch abort, and the data abort to
@ 0x040001C8, which is a loop with no exit. Thus each one of these exceptions stops the device.
@
@ This screen runs one probe at a time. Up selects the next probe, and Down runs it. During a probe,
@ the UND, SWI, PABT, DABT, and IRQ slots of the kernel hold the trap handlers below, and Timer1
@ gives an IRQ after approximately 8ms as a watchdog. A trap records the exception, and it returns
@ to the app. After each probe, the app writes the old slots and the old Timer1 back.
@
@ Row 1: bits 24 to 31 are the probe number. Bits 16 to 23 are the result:
@   00  not run                   01  the probe returned        02  undefined instruction
@   03  SWI                       04  prefetch abort            05  data abort
@   06  the watchdog IRQ          EE  a slot did not keep the written value, and the probe did not run
@   FF  the probe is running. If the device stops, the LCD keeps this value.
@ Rows 2 to 4 after a trap: row 2 is the LR of the exception minus the target. Row 3 is the SPSR of
@ the exception, which is the CPSR at the exception. Row 4 is r1 at the exception minus the target,
@ or 0 if r1 is 0.
@
@ The probes:
@   0  control: MOV PC to an aligned target in FLASH1. Row 2 is 8.
@   1  control: the undefined instruction 0xE7F000F0. A trap, with row 2 = 4.
@   2  control: a loop with no exit. The watchdog trap, with row 2 = 4.
@   3  LDR from FLASH1 at an aligned word + 2. Row 2: that LDR. Rows 3 and 4: the aligned words at
@      +0 and +4. The data is 11 22 33 84 55 66 77 88.
@   4  the same, from BIOS ROM at 0x04000210.
@   5  the same, from WRAM, with the same data as probe 3.
@   6  MOV PC to a FLASH1 target + 2.        A  MOV PC to a WRAM target + 2.
@   7  LDR PC, the same.                     B  LDR PC, the same.
@   8  LDMIA {PC}, the same.                 C  LDMIA {PC}, the same.
@   9  BX with bit 0 clear, the same.        D  BX with bit 0 clear, the same.
@   E  the fetch rule: MOV PC to a FLASH1 target + 2, with probe_fetch_pad at the target.
@   F  the same, with the target in WRAM.
@  10  MOV PC to 0x02004000, with R15 aligned. In the default build of 2 blocks, that is the first
@      address after the app. In a build with BLOCKS=3, it is block3_target in the third block.
@  11  LDR from 0x02004000 + 2, + 0, and + 4, the same as probe 3.
@  12  MOV PC to 0x02006000, with R15 aligned. That is after the app in both builds.
@  13  LDR from 0x02006000, the same as probe 11.
@  14  the decode of three words in the encoding space of MSR CPSR,Rm, with bits 12 to 15 not all
@      1. Each row is the CPSR after one word, from MRS. See probe_msr_space.
@  15  MSR CPSR,Rm with an ASR, a ROR, and an RRX shift. See probe_msr_shifts.
@  16  the reserved bits 24 to 27 of the CPSR, and MSR CPSR,#imm with bits 12 to 15 clear. See
@      probe_msr_other.
@  17 to 1A  a word in the encoding space of BX, in WRAM, with r0 = bxs_land. Row 2: 1 if the word
@      branched to r0, and 0 if not. Row 3: LR minus the word, or EEEEEEEE if LR did not change.
@      Row 4: the CPSR after the word. See probe_bx_space.
@        17  E12FF150      18  E12FFF30      19  E12F0010      1A  E12FFF70
@  1B  in Supervisor mode: CPSR bits 8 to 23, the bits that the SPSR keeps, and the SPSR form of
@      E12FE120. See probe_priv_1b.
@  1C  in Supervisor mode: three more words of the SPSR form of MSR. See probe_priv_1c.
@  1D  E120FF10, and 1E  E12AFF10: the same as probes 17 to 1A, with bits 16 to 19 not all 1.
@  1F  E12F0010 and two words like it, with bit 4 set and bits 12 to 15 not all 1. Each row is the
@      CPSR after one word. See probe_msr_bit4.
@  20  in Supervisor mode: the mode bits that the SPSR keeps. See probe_priv_20.
@ In probes 0 and 6 to D, the word at the target is MOV r1,PC, and each other word of probe_pad is
@ BX r3. r3 holds the address of probe_pc_back. If the probe returns, row 2 is r1 minus the aligned
@ target: 8 if R15 was aligned, A if not, and 0 if MOV r1,PC did not execute.

@ r0 = WRAM_CPU_RESULTS. Copies the state of the probe to the rows. This function runs no probe.
probe_view:
    push {r4, lr}
    ldr r1, =WRAM_PROBE_SEL
    ldr r2, [r1]
    cmp r2, #PROBE_COUNT
    movhs r2, #0
    strhs r2, [r1]
    ldr r3, =WRAM_PROBE_ROWS
    ldr r4, [r3]
    cmp r2, r4, lsr #24
    beq 1f
    @ The rows hold no result of this probe. Show only its number.
    mov r4, r2, lsl #24
    str r4, [r3]
    mov r4, #0
    str r4, [r3, #4]
    str r4, [r3, #8]
    str r4, [r3, #12]
1:  ldr r4, [r3]
    str r4, [r0]
    ldr r4, [r3, #4]
    str r4, [r0, #4]
    ldr r4, [r3, #8]
    str r4, [r0, #8]
    ldr r4, [r3, #12]
    str r4, [r0, #12]
    pop {r4, lr}
    bx lr
    .ltorg

@ Sets the rows of screen 29 to "not run" for the selected probe.
probe_clear_rows:
    ldr r1, =WRAM_PROBE_SEL
    ldr r2, [r1]
    cmp r2, #PROBE_COUNT
    movhs r2, #0
    strhs r2, [r1]
    ldr r3, =WRAM_PROBE_ROWS
    mov r2, r2, lsl #24
    str r2, [r3]
    mov r2, #0
    str r2, [r3, #4]
    str r2, [r3, #8]
    str r2, [r3, #12]
    bx lr
    .ltorg

@ poll_buttons calls this function on screen 29. r5 = the button bits, r0 = the button bits of the
@ last poll. -> r1 = 1 if this function used a press, and 0 if not. Keeps r0 and r4 to r11.
    .global probe_buttons
probe_buttons:
    push {r0, r4, r5, r6, r7, r8, r9, r10, r11, lr}
    tst r5, #0x10               @ Up: the next probe
    beq pbn_down
    tst r0, #0x10
    bne pbn_down
    ldr r1, =WRAM_PROBE_SEL
    ldr r2, [r1]
    add r2, r2, #1
    cmp r2, #PROBE_COUNT
    movhs r2, #0
    str r2, [r1]
    mov r1, #1
    b pbn_done
pbn_down:
    tst r5, #8                  @ Down: run the selected probe
    beq pbn_none
    tst r0, #8
    bne pbn_none
    ldr r1, =WRAM_PROBE_SEL
    ldr r2, [r1]
    cmp r2, #PROBE_COUNT
    movhs r2, #0
    strhs r2, [r1]
    @ Show the result FF before the probe. If the device stops, the LCD keeps this screen.
    ldr r3, =WRAM_PROBE_ROWS
    mov r1, r2, lsl #24
    orr r1, r1, #0x00FF0000
    str r1, [r3]
    mov r1, #0
    str r1, [r3, #4]
    str r1, [r3, #8]
    str r1, [r3, #12]
    bl redraw_screen
    bl probe_run
    mov r1, #1
    b pbn_done
pbn_none:
    mov r1, #0
pbn_done:
    pop {r0, r4, r5, r6, r7, r8, r9, r10, r11, lr}
    bx lr
    .ltorg

@ Runs the probe WRAM_PROBE_SEL with the traps, and writes the rows. Keeps r4 to r11.
probe_run:
    ldr r0, =WRAM_PROBE_SEL
    ldr r0, [r0]
    ldr r1, =probe_table
    ldr r0, [r1, r0, lsl #2]

@ r0 = a routine. Runs it with the traps, and writes WRAM_PROBE_ROWS. The routine ends with a branch
@ to probe_finish (r0 = the result, r1 to r3 = rows 2 to 4), or a trap ends it. Keeps r4 to r11.
trap_run:
    push {r4, r5, r6, r7, r8, r9, r10, r11, lr}
    mov r7, r0
    ldr r0, =WRAM_PROBE_SP
    str sp, [r0]

    @ The WRAM copy of the target, and the WRAM data of probe 5.
    ldr r0, =WRAM_PROBE_PAD
    ldr r1, =probe_pad
    mov r2, #8
    bl memcpy_words
    ldr r0, =WRAM_CPU_DATA
    bl put_load_data

    @ Keep the old Timer1.
    ldr r4, =TIMER1_BASE
    ldr r5, =WRAM_PROBE_T1
    ldr r0, [r4]
    str r0, [r5]
    ldr r0, [r4, #4]
    str r0, [r5, #4]
    ldr r0, [r4, #8]
    str r0, [r5, #8]

    @ Keep the old slots, and write the trap handlers. r6 is not 0 if a slot did not keep its value.
    ldr r5, =WRAM_PROBE_SLOTS
    mov r6, #0
    mov r0, #KERNEL_UND_SLOT
    ldr r1, =trap_und
    bl probe_set_slot
    mov r0, #KERNEL_SWI_SLOT
    ldr r1, =trap_swi
    bl probe_set_slot
    mov r0, #KERNEL_PABT_SLOT
    ldr r1, =trap_pabt
    bl probe_set_slot
    mov r0, #KERNEL_DABT_SLOT
    ldr r1, =trap_dabt
    bl probe_set_slot
    mov r0, #KERNEL_IRQ_SLOT
    ldr r1, =trap_irq
    bl probe_set_slot
    cmp r6, #0
    beq 1f
    mov r0, #0xEE
    mov r1, #0
    mov r2, #0
    mov r3, #0
    b probe_finish

    @ The watchdog. Mask and acknowledge each source, then unmask only Timer1.
1:  ldr r0, =INTC_MASK
    mvn r1, #0
    str r1, [r0]
    ldr r0, =INTC_BASE
    str r1, [r0, #0x10]
    mov r1, #0
    str r1, [r4, #8]
    ldr r1, =PROBE_WATCHDOG_PERIOD
    str r1, [r4]
    str r1, [r4, #4]
    mov r1, #PROBE_WATCHDOG_CTRL
    str r1, [r4, #8]
    ldr r0, =INTC_ENABLE
    mov r1, #INT_TIMER1_BIT
    str r1, [r0]

    bx r7
    .ltorg

@ r0 = slot, r1 = handler, r5 = the save address -> r5 + 4. Sets r6 to 1 if the slot did not keep
@ r1. Uses r2.
probe_set_slot:
    ldr r2, [r0]
    str r2, [r5], #4
    str r1, [r0]
    ldr r2, [r0]
    cmp r2, r1
    movne r6, #1
    bx lr

probe_table:
    .word probe_aligned         @ 0
    .word probe_undefined       @ 1
    .word probe_watchdog        @ 2
    .word probe_data_flash      @ 3
    .word probe_data_bios       @ 4
    .word probe_data_wram       @ 5
    .word probe_mov_flash       @ 6
    .word probe_ldr_flash       @ 7
    .word probe_ldm_flash       @ 8
    .word probe_bx_flash        @ 9
    .word probe_mov_wram        @ A
    .word probe_ldr_wram        @ B
    .word probe_ldm_wram        @ C
    .word probe_bx_wram         @ D
    .word probe_fetch_flash     @ E
    .word probe_fetch_wram      @ F
    .word probe_after_app       @ 10
    .word probe_data_after_app  @ 11
    .word probe_after_block3    @ 12
    .word probe_data_after_block3 @ 13
    .word probe_msr_space       @ 14
    .word probe_msr_shifts      @ 15
    .word probe_msr_other       @ 16
    .word probe_bx_f150         @ 17
    .word probe_bx_ff30         @ 18
    .word probe_bx_0010         @ 19
    .word probe_bx_ff70         @ 1A
    .word probe_priv_1b         @ 1B
    .word probe_priv_1c         @ 1C
    .word probe_bx_0f10         @ 1D
    .word probe_bx_af10         @ 1E
    .word probe_msr_bit4        @ 1F
    .word probe_priv_20         @ 20

probe_aligned:
    ldr r4, =probe_pad + 4
    mov r2, r4
    b probe_pc_mov
probe_mov_flash:
    ldr r4, =probe_pad + 4
    add r2, r4, #2
    b probe_pc_mov
probe_ldr_flash:
    ldr r4, =probe_pad + 4
    add r2, r4, #2
    b probe_pc_ldr
probe_ldm_flash:
    ldr r4, =probe_pad + 4
    add r2, r4, #2
    b probe_pc_ldm
probe_bx_flash:
    ldr r4, =probe_pad + 4
    add r2, r4, #2
    b probe_pc_bx
probe_mov_wram:
    ldr r4, =WRAM_PROBE_PAD + 4
    add r2, r4, #2
    b probe_pc_mov
probe_ldr_wram:
    ldr r4, =WRAM_PROBE_PAD + 4
    add r2, r4, #2
    b probe_pc_ldr
probe_ldm_wram:
    ldr r4, =WRAM_PROBE_PAD + 4
    add r2, r4, #2
    b probe_pc_ldm
probe_bx_wram:
    ldr r4, =WRAM_PROBE_PAD + 4
    add r2, r4, #2
    b probe_pc_bx

@ r4 = the aligned target, r2 = the value for R15. Each write to R15 below is the last instruction
@ before the target. Thus no PC-relative load comes after a misaligned R15.
probe_pc_mov:
    bl probe_pc_setup
    mov pc, r2
probe_pc_ldr:
    bl probe_pc_setup
    ldr r5, =WRAM_CPU_DATA
    str r2, [r5]
    ldr pc, [r5]
probe_pc_ldm:
    bl probe_pc_setup
    ldr r5, =WRAM_CPU_DATA
    str r2, [r5]
    .word 0xE8958000            @ ldmia r5, {pc}
probe_pc_bx:
    bl probe_pc_setup
    bx r2

@ r4 = the aligned target -> r1 = 0, r3 = probe_pc_back.
probe_pc_setup:
    ldr r1, =WRAM_PROBE_TARGET
    str r4, [r1]
    mov r1, #0
    ldr r3, =probe_pc_back
    bx lr
    .ltorg

@ The target returns here with BX r3. r1 = the value of MOV r1,PC, or 0.
probe_pc_back:
    ldr r2, =WRAM_PROBE_TARGET
    ldr r2, [r2]
    cmp r1, #0
    subne r1, r1, r2
    mov r2, #0
    mov r3, #0
    mov r0, #1
    b probe_finish

probe_undefined:
    adr r4, 1f
    ldr r1, =WRAM_PROBE_TARGET
    str r4, [r1]
    mov r0, #0                  @ for a CPU that decodes the word as LDRB r0,[r0,r0,ROR r0]!
    mov r1, #0
1:  .word 0xE7F000F0            @ undefined
    mov r1, #0
    mov r2, #0
    mov r3, #0
    mov r0, #1
    b probe_finish

@ Probes E and F: a misaligned R15 at a target that probe_fetch_pad holds. Each fetch rule executes
@ a different instruction, and then an undefined instruction or an SWI stops the probe with a trap.
@ See probe_fetch_pad below for the instruction of each rule. Before the jump, Z = 0 and C = 1,
@ r0 = WRAM_CPU_DATA, and r5 = r7 = 0. This emulator decodes E7F0E7F0 as LDRB lr,[r0,r0,ROR r7]!,
@ thus r7 = 0 keeps that access predictable.
probe_fetch_flash:
    ldr r4, =probe_fetch_pad
    b probe_fetch
probe_fetch_wram:
    ldr r0, =WRAM_PROBE_PAD
    ldr r1, =probe_fetch_pad
    mov r2, #4
    bl memcpy_words
    ldr r4, =WRAM_PROBE_PAD
probe_fetch:                    @ r4 = the aligned target
    ldr r1, =WRAM_PROBE_TARGET
    str r4, [r1]
    add r2, r4, #2
    mov r5, #0
    mov r7, #0
    mov r1, #0
    ldr r0, =WRAM_CPU_DATA
    cmp r0, #0                  @ Z = 0 and C = 1
    mov pc, r2

@ Probe 10: an aligned fetch at 0x02004000. With BLOCKS=3, block3_target is at that address, and the
@ probe returns with row 2 = 8.
probe_after_app:
    ldr r4, =0x02004000
    mov r2, r4
    b probe_pc_mov

@ Probe 11: data reads at 0x02004000.
probe_data_after_app:
    ldr r4, =0x02004000
    b probe_data

@ Probe 12: an aligned fetch at 0x02006000, after the app in both builds.
probe_after_block3:
    ldr r4, =0x02006000
    mov r2, r4
    b probe_pc_mov

@ Probe 13: data reads at 0x02006000.
probe_data_after_block3:
    ldr r4, =0x02006000
    b probe_data
    .ltorg

@ Probe 14. Each word is in the encoding space of MSR CPSR_fsxc,Rm (cond 00010 0 10 1111), but bits
@ 12 to 15 are not all 1. This emulator decodes each one as TEQ with no flag write. An MSR in User
@ mode writes only the flags. Thus the flags after each word show the decode:
@   Row 2: E12FE12F, Rm = R15, with NZCV = 1111 before it. An MSR gives the high bits of R15.
@   Row 3: E12FE120, Rm = r0 = A0000000, with NZCV = 0000 before it. An MSR gives NZCV = 1010.
@   Row 4: E12F0000, Rm = r0 = 50000000, with NZCV = 0000 before it. An MSR gives NZCV = 0101.
@ Probes 6 to 9 execute E12FE12F as the halfword E12F in both halves.
probe_msr_space:
    adr r4, 1f
    ldr r1, =WRAM_PROBE_TARGET
    str r4, [r1]
    msr cpsr_f, #0xF0000000
1:  .word 0xE12FE12F
    mrs r1, cpsr
    ldr r0, =0xA0000000
    msr cpsr_f, #0
    .word 0xE12FE120
    mrs r2, cpsr
    ldr r0, =0x50000000
    msr cpsr_f, #0
    .word 0xE12F0000
    mrs r3, cpsr
    mov r0, #1
    b probe_finish
    .ltorg

@ Probe 15. Each word is a correct MSR CPSR_fsxc,Rm (bits 12 to 15 all 1), with a shift in bits 4
@ to 11. Probe 14 shows an LSR shift there. In User mode, an MSR writes only the flags:
@   Row 2: E12FF0C0, Rm ASR #1, Rm = r0 = 80000000, NZCV = 0000 before it. The shift gives
@          C0000000, and no shift gives 80000000.
@   Row 3: E12FF260, Rm ROR #4, Rm = r0 = 00000005, NZCV = 0000 before it. The shift gives
@          50000000, and no shift gives 00000005.
@   Row 4: E12FF060, Rm RRX, Rm = r0 = 20000000, NZCV = 0010 before it. The shift gives 90000000,
@          and no shift gives 20000000.
probe_msr_shifts:
    adr r4, 1f
    ldr r1, =WRAM_PROBE_TARGET
    str r4, [r1]
    mov r0, #0x80000000
    msr cpsr_f, #0
1:  .word 0xE12FF0C0
    mrs r1, cpsr
    mov r0, #5
    msr cpsr_f, #0
    .word 0xE12FF260
    mrs r2, cpsr
    mov r0, #0x20000000
    msr cpsr_f, #0x20000000
    .word 0xE12FF060
    mrs r3, cpsr
    mov r0, #1
    b probe_finish
    .ltorg

@ Probe 16:
@   Row 2: E128F000, a correct MSR CPSR_f,r0, with r0 = 5F000000. Bits 24 to 27 show if the CPSR
@          keeps the reserved bits.
@   Row 3: E3280450, MSR CPSR_f,#50000000, with bits 12 to 15 clear, and NZCV = 0000 before it.
@   Row 4: not used.
probe_msr_other:
    adr r4, 1f
    ldr r1, =WRAM_PROBE_TARGET
    str r4, [r1]
    ldr r0, =0x5F000000
    msr cpsr_f, #0
1:  .word 0xE128F000
    mrs r1, cpsr
    msr cpsr_f, #0
    .word 0xE3280450
    mrs r2, cpsr
    mov r3, #0
    mov r0, #1
    b probe_finish
    .ltorg

@ Probes 17 to 1A. Each copies one word to WRAM_PROBE_PAD, then MOV r1,#0, then BX r3, and it
@ jumps to the word with r0 = bxs_land, LR = EEEEEEEE, and NZCV = 0000. A branch to r0 goes to
@ bxs_land, with r1 = 1. Else the word falls through to BX r3, with r1 = 0.
@   Row 2: r1, which is 1 if the word branched to r0.
@   Row 3: LR minus the word, or EEEEEEEE if LR did not change. A BLX gives 4.
@   Row 4: the CPSR after the word.
probe_bx_f150:
    ldr r8, =0xE12FF150
    b probe_bx_space
probe_bx_ff30:
    ldr r8, =0xE12FFF30
    b probe_bx_space
probe_bx_0010:
    ldr r8, =0xE12F0010
    b probe_bx_space
probe_bx_0f10:
    ldr r8, =0xE120FF10
    b probe_bx_space
probe_bx_af10:
    ldr r8, =0xE12AFF10
    b probe_bx_space
probe_bx_ff70:
    ldr r8, =0xE12FFF70
probe_bx_space:                 @ r8 = the word
    ldr r4, =WRAM_PROBE_PAD
    str r8, [r4]
    ldr r1, =0xE3A01000         @ mov r1, #0
    str r1, [r4, #4]
    ldr r1, =0xE12FFF13         @ bx r3
    str r1, [r4, #8]
    ldr r1, =WRAM_PROBE_TARGET
    str r4, [r1]
    adr r0, bxs_land
    adr r3, bxs_back
    ldr lr, =0xEEEEEEEE
    msr cpsr_f, #0
    bx r4
bxs_land:
    mov r1, #1
bxs_back:
    mrs r3, cpsr
    ldr r2, =0xEEEEEEEE
    cmp lr, r2
    ldrne r2, =WRAM_PROBE_PAD
    subne r2, lr, r2
    mov r0, #1
    b probe_finish
    .ltorg

@ Probes 1B and 1C run their words in Supervisor mode, where an SPSR is present. trap_run already
@ keeps the old SWI slot. The probe writes its own handler to that slot, and it executes SWI. The
@ handler keeps SPSR_svc in r12, executes the words, writes SPSR_svc back, and returns with MOVS.
@ Probe 1B:
@   Row 2: the CPSR AND 00FFFF00, after a correct MSR CPSR_sx,r0 with r0 = 00FFFF00. It shows if the
@          CPSR keeps bits 8 to 23.
@   Row 3: the SPSR after a correct MSR SPSR_fsxc,r0 with r0 = FFFFFFFF. It shows the bits that the
@          SPSR keeps.
@   Row 4: the SPSR after E16FE120, which is E12FE120 for the SPSR, with r0 = A0000000 and
@          SPSR = 00000010 before it. MSR SPSR_fsxc,r0,LSR #2 gives 28000000 or 20000000.
probe_priv_1b:
    ldr r4, =priv_1b
    b probe_priv
@ Probe 1C. The SPSR is 00000010 before each word:
@   Row 2: the SPSR after E3680450, MSR SPSR_f,#50000000 with bits 12 to 15 clear.
@   Row 3: the SPSR after E16F0000, MSR SPSR_fsxc,r0 with bits 12 to 15 clear, r0 = 50000000.
@   Row 4: the SPSR after E16FF0C0, MSR SPSR_fsxc,r0,ASR #1, r0 = 80000000.
@ Probe 20. Each word is a correct MSR SPSR_fsxc,r0 or MSR SPSR_c,r0, with no shift:
@   Row 2: the SPSR after MSR SPSR_fsxc,r0 with r0 = 00000005. Mode bit 4 forced to 1 gives 15.
@   Row 3: the SPSR after MSR SPSR_fsxc,r0 with r0 = 00000000.
@   Row 4: the SPSR after MSR SPSR_c,r0 with r0 = 000000C3, from SPSR = 00000010.
probe_priv_20:
    ldr r4, =priv_20
    b probe_priv
probe_priv_1c:
    ldr r4, =priv_1c
probe_priv:                     @ r4 = the handler
    adr r1, 1f
    ldr r0, =WRAM_PROBE_TARGET
    str r1, [r0]
    mov r0, #KERNEL_SWI_SLOT
    str r4, [r0]
1:  swi 0
    mov r0, #1
    b probe_finish
    .ltorg

priv_1b:
    mrs r12, spsr
    ldr r0, =0x00FFFF00
    .word 0xE126F000            @ msr cpsr_sx, r0
    mrs r1, cpsr
    and r1, r1, r0
    mvn r0, #0
    msr spsr_fsxc, r0
    mrs r2, spsr
    mov r0, #0x10
    msr spsr_fsxc, r0
    mov r0, #0xA0000000
    .word 0xE16FE120
    mrs r3, spsr
    msr spsr_fsxc, r12
    movs pc, lr
    .ltorg

priv_20:
    mrs r12, spsr
    mov r0, #0x05
    msr spsr_fsxc, r0
    mrs r1, spsr
    mov r0, #0
    msr spsr_fsxc, r0
    mrs r2, spsr
    mov r0, #0x10
    msr spsr_fsxc, r0
    mov r0, #0xC3
    msr spsr_c, r0
    mrs r3, spsr
    msr spsr_fsxc, r12
    movs pc, lr
    .ltorg

priv_1c:
    mrs r12, spsr
    mov r0, #0x10
    msr spsr_fsxc, r0
    .word 0xE3680450
    mrs r1, spsr
    mov r0, #0x10
    msr spsr_fsxc, r0
    mov r0, #0x50000000
    .word 0xE16F0000
    mrs r2, spsr
    mov r0, #0x10
    msr spsr_fsxc, r0
    mov r0, #0x80000000
    .word 0xE16FF0C0
    mrs r3, spsr
    msr spsr_fsxc, r12
    movs pc, lr
    .ltorg

@ Probe 1F. NZCV = 0000 before each word. A branch to r0 causes a prefetch abort, which the trap
@ records.
@   Row 2: E12F0010 (Rs = r0, LSL), r0 = 50000000. r0 AND FF = 0, thus an MSR gives 50000000.
@   Row 3: E12F1010 (Rs = r1, LSL), r0 = 05000000, r1 = 4. An MSR with the register shift gives
@          50000000. An MSR with no shift gives 05000000, which writes no flag.
@   Row 4: E12FE110, the same as row 3, with bits 12 to 15 = E.
probe_msr_bit4:
    adr r4, 1f
    ldr r1, =WRAM_PROBE_TARGET
    str r4, [r1]
    mov r0, #0x50000000
    msr cpsr_f, #0
1:  .word 0xE12F0010
    mrs r12, cpsr
    mov r0, #0x05000000
    mov r1, #4
    msr cpsr_f, #0
    .word 0xE12F1010
    mrs r2, cpsr
    mov r0, #0x05000000
    mov r1, #4
    msr cpsr_f, #0
    .word 0xE12FE110
    mrs r3, cpsr
    mov r1, r12
    mov r0, #1
    b probe_finish
    .ltorg

@ The IRQ return address is the address of the interrupted instruction + 4.
probe_watchdog:
    adr r4, 1f
    ldr r1, =WRAM_PROBE_TARGET
    str r4, [r1]
    mov r1, #0
1:  b 1b

probe_data_flash:
    ldr r4, =probe_flash_data
    b probe_data
probe_data_bios:
    ldr r4, =0x04000210
    b probe_data
probe_data_wram:
    ldr r4, =WRAM_CPU_DATA
@ r4 = an aligned address -> row 2 = LDR at +2, rows 3 and 4 = the words at +0 and +4.
probe_data:
    ldr r1, =WRAM_PROBE_TARGET
    str r4, [r1]
    ldr r1, [r4, #2]
    ldr r2, [r4]
    ldr r3, [r4, #4]
    mov r0, #1
    b probe_finish
    .ltorg

@ The trap handlers. Each one runs in the mode of its exception, with no stack.
trap_und:
    mov r0, #2
    b trap_common
trap_swi:
    mov r0, #3
    b trap_common
trap_pabt:
    mov r0, #4
    b trap_common
trap_dabt:
    mov r0, #5
    b trap_common
trap_irq:
    mov r0, #6
trap_common:                    @ r0 = the result
    ldr r12, =WRAM_PROBE_TARGET
    ldr r12, [r12]
    cmp r1, #0
    subne r1, r1, r12           @ row 4
    sub r2, lr, r12             @ row 2
    mrs r3, spsr                @ row 3
    @ Stop the watchdog, and mask and acknowledge each source. If not, the IRQ line stays asserted
    @ after the return.
    ldr r12, =TIMER1_BASE
    mov r11, #0
    str r11, [r12, #8]
    ldr r12, =INTC_MASK
    mvn r11, #0
    str r11, [r12]
    ldr r12, =INTC_BASE
    str r11, [r12, #0x10]
    @ Return to probe_trapped in User mode and in the ARM state, whatever the state at the exception.
    mrs r12, spsr
    bic r12, r12, #0xFF
    orr r12, r12, #0x10
    msr spsr_fsxc, r12
    ldr lr, =probe_trapped
    movs pc, lr
    .ltorg

@ User mode. r0 = the result, r1 = row 4, r2 = row 2, r3 = row 3.
probe_trapped:
    mov r12, r1
    mov r1, r2
    mov r2, r3
    mov r3, r12

@ User mode. r0 = the result, r1 to r3 = rows 2 to 4. Loads the SP of the caller again, writes the
@ rows, writes the old slots and the old Timer1 back, and returns from probe_run.
probe_finish:
    ldr r12, =WRAM_PROBE_SP
    ldr sp, [r12]
    ldr r12, =WRAM_PROBE_ROWS
    str r1, [r12, #4]
    str r2, [r12, #8]
    str r3, [r12, #12]
    ldr r1, =WRAM_PROBE_SEL
    ldr r1, [r1]
    mov r1, r1, lsl #24
    orr r1, r1, r0, lsl #16
    str r1, [r12]

    ldr r4, =TIMER1_BASE
    mov r0, #0
    str r0, [r4, #8]
    ldr r0, =INTC_MASK
    mvn r1, #0
    str r1, [r0]
    ldr r0, =INTC_BASE
    str r1, [r0, #0x10]

    ldr r5, =WRAM_PROBE_SLOTS
    ldr r0, [r5]
    mov r1, #KERNEL_UND_SLOT
    str r0, [r1]
    ldr r0, [r5, #4]
    mov r1, #KERNEL_SWI_SLOT
    str r0, [r1]
    ldr r0, [r5, #8]
    mov r1, #KERNEL_PABT_SLOT
    str r0, [r1]
    ldr r0, [r5, #12]
    mov r1, #KERNEL_DABT_SLOT
    str r0, [r1]
    ldr r0, [r5, #16]
    mov r1, #KERNEL_IRQ_SLOT
    str r0, [r1]

    ldr r5, =WRAM_PROBE_T1
    ldr r0, [r5]
    str r0, [r4]
    ldr r0, [r5, #4]
    str r0, [r4, #4]
    ldr r0, [r5, #8]
    str r0, [r4, #8]

    pop {r4, r5, r6, r7, r8, r9, r10, r11, lr}
    bx lr
    .ltorg

@ The target of probes 0 and 6 to D. The target is probe_pad + 4. probe_run copies these words to
@ WRAM_PROBE_PAD for probes A to D.
    .align 2
probe_pad:
    .word 0xE12FFF13            @ bx r3
    .word 0xE1A0100F            @ mov r1, pc
    .word 0xE12FFF13, 0xE12FFF13, 0xE12FFF13, 0xE12FFF13, 0xE12FFF13, 0xE12FFF13

@ The target of probes E and F. The rules, for a fetch at the target + 2:
@   - The halfword at that address, in both halves: 13A513A5, MOVNE r1,#0x94000002. Then E7F0E7F0,
@     which is undefined. A trap 02, with row 2 = A, and r1 = 0x94000002.
@   - The aligned word, with bit 1 of R15 kept: 13A52F01, MOVNE r2,#4. Then E7F0E7F0. A trap 02,
@     with row 2 = A, and r1 = 0.
@   - The aligned word, with R15 force aligned: the same, but row 2 = 8.
@   - The bytes at the target + 2 to + 5: E7F013A5, LDRB r1,[r0,r5,ASR #7]!, a read of one byte of
@     WRAM_CPU_DATA. Then E7F0E7F0. A trap 02, with row 2 = A, and r1 = 0x11.
@   - The aligned word, rotated by 16: 2F0113A5, SWICS. A trap 03, with row 2 = 6.
    .align 2
probe_fetch_pad:
    .word 0x13A52F01
    .word 0xE7F0E7F0, 0xE7F0E7F0, 0xE7F0E7F0

@ The FLASH1 data of probe 3: the bytes 11 22 33 84 55 66 77 88.
probe_flash_data:
    .word 0x84332211, 0x88776655

@ The third block, only in a build with BLOCKS=3 (see the Makefile). It holds the target of probe 10,
@ at 0x02004000. The target is the same as at probe_pad + 4: MOV r1,PC, and then BX r3.
    .if BLOCKS >= 3
    .section .block3, "ax"
    .arm
block3_target:
    .word 0xE1A0100F            @ mov r1, pc
    .word 0xE12FFF13            @ bx r3
    .endif
