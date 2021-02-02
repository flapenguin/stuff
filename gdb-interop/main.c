#pragma GCC visibility push (hidden)

#include <stddef.h>
#include <stdint.h>
#include "r_debug.h"

#ifndef FAKES_DIR
#define FAKES_DIR "." // This is defined during build-time
#endif

#define NL "\n"
__asm__(
  ".text"                            NL
  ".global __entry_point__"          NL
  ".type __entry_point__, @function" NL
  "__entry_point__:"                 NL
  "   xor %rbp, %rbp"                NL
  "   mov %rsp, %rdi"                NL
  "   andq $-16, %rsp"               NL
  "   call __entry_point_impl__"     NL
// ".global syscall"                 NL // TODO: marking it global requires relocation for some reason
  ".type syscall, @function"         NL
  "syscall:"                         NL
  "   mov %rdi, %rax"                NL
  "   mov %rsi, %rdi"                NL
  "   mov %rdx, %rsi"                NL
  "   mov %rcx, %rdx"                NL
  "   mov %r8, %r10"                 NL
  "   mov %r9, %r8"                  NL
  "   mov 0x8(%rsp), %r9"            NL
  "   syscall"                       NL
  "   ret"                           NL
);
#undef NL
uint64_t syscall(uint64_t number, ...);

/**
 * Debugger sets breakpoint on this function to watch for changes in shared objects.
 * GDB: `set stop-on-solib-events 1`
 * LLDB: not implemented (?)
 *
 * GDB should not rely on the name and use _r_debug.r_brk instead, but it doesn't care.
 * https://code.woboq.org/userspace/glibc/elf/dl-debug.c.html#_dl_debug_state
 */
__attribute__((noinline)) void _dl_debug_state() { __asm__ volatile ("nop\n"); }

struct r_debug _r_debug = {
  .r_version = 1,
  .r_ldbase = 0x400000, // TODO: load dynamically
  .r_map = 0,
  .r_brk = (uint64_t)_dl_debug_state
};

static void* mmap_alloc(uint64_t size) {
  return (void*)syscall(
    /*SYS_mmap*/(uint64_t)9,
    /*addr    */(uint64_t)0x0,
    /*size    */(uint64_t)size,
    /*prot    */(uint64_t)(/*exec*/0x4),
    /*flags   */(uint64_t)(/*anonymous*/0x20 | /*private*/0x2),
    /*fd      */(int64_t)-1,
    /*offset  */(uint64_t)0
  );
}

extern void __relocate_self(uint64_t* sp);

static void __entry_point_impl__(uint64_t* sp) {
  __relocate_self(sp);

  _r_debug.r_state = RT_ADD;
  _dl_debug_state();

  static struct r_debug_link_map so1;
  so1.l_addr = (uint64_t)mmap_alloc(0x10000);
  so1.l_name = FAKES_DIR "/fake-1.so",
  so1.l_ld = (uint64_t[]){/*DT_NULL*/ 0, 0};

  _r_debug.r_state = RT_CONSISTENT;
  _dl_debug_state();

  _r_debug.r_state = RT_ADD;
  _dl_debug_state();

  // TODO: gdb expects first list entry to be ld.so and omits it

  static struct r_debug_link_map so2;
  so2.l_addr = (uint64_t)mmap_alloc(0x10000);
  so2.l_name = FAKES_DIR "/fake-2.so",
  so2.l_ld = (uint64_t[]){/*DT_NULL*/ 0, 0};

  _r_debug.r_state = RT_CONSISTENT;
  _dl_debug_state();

  so1.l_prev = 0;
  so1.l_next = &so2;
  so2.l_prev = &so1;
  so2.l_next = 0;

  _r_debug.r_map = &so1;

  __asm__("int $3\n"); // Manually trigger breakpoint.
  syscall(/*SYS_exit*/60, 0);
}
