/* SPDX-License-Identifier: Unlicense; exercise entry/exit from small stacks,
   then give the larger world/save fixtures a 1 MiB child stack. */
#include <proto/dos.h>
#include <dos/dostags.h>
int main(void) {
    LONG result;
    /* Exercise both the allocated-stack and existing-stack entry paths. A
       completed SystemTags also verifies return through libnix to the shell. */
    result = SystemTags((CONST_STRPTR)"Astralbound-stack-tests.000 ok > stack-small-results.txt",
                        NP_StackSize, 4096, TAG_DONE);
    if (result) return 20;
    result = SystemTags((CONST_STRPTR)"Astralbound-stack-tests.000 ok > stack-large-results.txt",
                        NP_StackSize, 1048576, TAG_DONE);
    if (result) return 20;
    result = SystemTags((CONST_STRPTR)"Astralbound-stack-tests.000 ok 5 > stack-status-results.txt",
                        NP_StackSize, 4096, TAG_DONE);
    if (result != 5) return 20;
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
