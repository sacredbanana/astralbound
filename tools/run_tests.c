/* SPDX-License-Identifier: Unlicense; a bare CLI starts with only a small stack.
   Give test children room for world/save fixtures without swapping at exit. */
#include <proto/dos.h>
#include <dos/dostags.h>
int main(void) {
    LONG result;
    result = SystemTags((CONST_STRPTR)"Astralbound-galaxy-tests.020 > galaxy-results.txt",
                        NP_StackSize, 1048576, TAG_DONE);
    if (result) return 20;
    result = SystemTags((CONST_STRPTR)"Astralbound-music-tests.020 > music-results.txt",
                        NP_StackSize, 1048576, TAG_DONE);
    if (result) return 20;
    result = SystemTags((CONST_STRPTR)"Astralbound-pres-tests.020 > presentation-results.txt",
                        NP_StackSize, 1048576, TAG_DONE);
    if (result) return 20;
    result = SystemTags((CONST_STRPTR)"Astralbound-tests.020 --quick > core-results.txt",
                        NP_StackSize, 1048576, TAG_DONE);
    if (result) return 20;
    result = SystemTags((CONST_STRPTR)"Astralbound-ui-tests.020 > ui-results.txt",
                        NP_StackSize, 1048576, TAG_DONE);
    if (result) return 20;
    result = SystemTags((CONST_STRPTR)"Astralbound-career-tests.020 > career-results.txt",
                        NP_StackSize, 1048576, TAG_DONE);
    if (result) return 20;
    result = SystemTags((CONST_STRPTR)"Astralbound-Icons > icons-results.txt",
                        NP_StackSize, 1048576, TAG_DONE);
    return result ? 20 : 0;
}
