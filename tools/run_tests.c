/* SPDX-License-Identifier: Unlicense; a bare CLI starts with only a small stack.
   Give test children room for world/save fixtures without swapping at exit. */
#include <proto/dos.h>
#include <dos/dostags.h>
int main(void) {
    LONG result;
    result = SystemTags((CONST_STRPTR)"Space-presentation-tests.020 > presentation-results.txt",
                        NP_StackSize, 262144, TAG_DONE);
    if (result) return 20;
    result = SystemTags((CONST_STRPTR)"Space-tests.020 --quick > core-results.txt",
                        NP_StackSize, 262144, TAG_DONE);
    if (result) return 20;
    result = SystemTags((CONST_STRPTR)"Space-ui-tests.020 > ui-results.txt",
                        NP_StackSize, 262144, TAG_DONE);
    if (result) return 20;
    result = SystemTags((CONST_STRPTR)"Space-career-tests.020 > career-results.txt",
                        NP_StackSize, 262144, TAG_DONE);
    if (result) return 20;
    result = SystemTags((CONST_STRPTR)"Space-Icons > icons-results.txt",
                        NP_StackSize, 262144, TAG_DONE);
    return result ? 20 : 0;
}
