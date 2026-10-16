/* SPDX-License-Identifier: Unlicense. Run from a default small-stack CLI and
   again with Stack 1048576, then execute another shell command after each run. */
#include <exec/tasks.h>
#include <proto/exec.h>
#include <assert.h>
#include <stdio.h>
unsigned long __stack = 262144;
static void exercise_stack(void) {
    volatile unsigned char scratch[65536];
    struct Task *task = FindTask(NULL);
    unsigned i;
    assert((ULONG)task->tc_SPUpper - (ULONG)task->tc_SPLower >= __stack);
    assert((ULONG)&scratch[0] >= (ULONG)task->tc_SPLower);
    assert((ULONG)&scratch[sizeof(scratch)] <= (ULONG)task->tc_SPUpper);
    for (i = 0; i < sizeof(scratch); i++) scratch[i] = (unsigned char)i;
    for (i = 0; i < sizeof(scratch); i++) assert(scratch[i] == (unsigned char)i);
}
int main(int argc, char **argv) {
    assert(argc == 2 || argc == 3);
    assert(argv && argv[1][0] == 'o' && argv[1][1] == 'k' && argv[1][2] == 0);
    exercise_stack();
    puts("Native stack and arguments OK");
    return argc == 3 && argv[2][0] == '5' ? 5 : 0;
}
