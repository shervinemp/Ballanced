// Main thread and exception stacks. libogc's crt0 takes them from these
// symbols, with a 128 KB main stack by default; the engine was written for
// Windows' 1 MB stack, and the physics engine allocates its temporary
// buffers there (alloca). The stack has no guard, so an overflow would
// silently overwrite memory.
//
// Link this file into each executable: crt0 lives in libogc, which is
// scanned last, so a definition in a static library would not be picked up.

#include <gctypes.h>

static u8 s_MainStack[1024 * 1024] __attribute__((aligned(32)));
static u8 s_ExceptionStack[16 * 1024] __attribute__((aligned(32)));

void *__ppc_main_sp = s_MainStack + sizeof(s_MainStack);
void *__ppc_excpt_sp = s_ExceptionStack + sizeof(s_ExceptionStack);
