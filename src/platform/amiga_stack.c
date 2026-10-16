/* SPDX-License-Identifier: Unlicense
   Keep libnix startup/exit on the original CLI or Workbench stack. Its legacy
   stack-copy constructor corrupts the return path with this toolchain. */
#undef main
#include <exec/memory.h>
#include <exec/tasks.h>
#include <proto/exec.h>
#include <stdio.h>

/* Individual programs may request more (the native regression suites do). */
unsigned long __stack __attribute__((weak)) = 262144;
extern int astralbound_main(int argc, char **argv);
extern int amiga_stack_call(struct StackSwapStruct *stack, int argc, char **argv);

int main(int argc, char **argv) {
    struct Task *task = FindTask(NULL);
    struct StackSwapStruct stack;
    ULONG size = (__stack + 15UL) & ~15UL;
    APTR memory;
    int result;
    if ((ULONG)task->tc_SPUpper - (ULONG)task->tc_SPLower >= size)
        return astralbound_main(argc, argv);
    memory = AllocMem(size, MEMF_ANY);
    if (!memory) {
        puts("Unable to allocate program stack.");
        return 20;
    }
    stack.stk_Lower = memory;
    stack.stk_Upper = (ULONG)memory + size;
    stack.stk_Pointer = (APTR)stack.stk_Upper;
    result = amiga_stack_call(&stack, argc, argv);
    /* The assembly trampoline has restored both SP and the task's bounds. */
    FreeMem(memory, size);
    return result;
}
