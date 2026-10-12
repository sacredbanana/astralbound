; SPDX-License-Identifier: Unlicense
; Amiga trackdisk boot entry: return the resident DOS initializer in A0.
        move.l  4.w,a6
        lea     dosname(pc),a1
        jsr     -96(a6)        ; exec.library/FindResident
        tst.l   d0
        beq.s   failed
        move.l  d0,a0
        move.l  22(a0),a0      ; Resident.rt_Init
        moveq   #0,d0
        rts
failed:
        moveq   #-1,d0
        rts
dosname:
        dc.b    'dos.library',0
        even
