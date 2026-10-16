/* SPDX-License-Identifier: Unlicense
   Exec StackSwap moves only the return address, not C locals or call frames.
   Preserve the caller on its original stack and run a fresh main call on the
   allocated stack. All state needed across switches lives in saved registers. */
    .text
    .even
    .globl _amiga_stack_call
_amiga_stack_call:
    movem.l %d2-%d3/%a2-%a3/%a6,-(%sp)
    move.l 24(%sp),%a2
    move.l 28(%sp),%d2
    move.l 32(%sp),%a3
    move.l 4.w,%a6
    move.l %a2,%a0
    jsr -732(%a6)
    move.l %a3,-(%sp)
    move.l %d2,-(%sp)
    jsr _astralbound_main
    addq.l #8,%sp
    move.l %d0,%d3
    move.l %a2,%a0
    jsr -732(%a6)
    move.l %d3,%d0
    movem.l (%sp)+,%d2-%d3/%a2-%a3/%a6
    rts
