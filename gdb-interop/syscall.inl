#include <stdint.h>

#define NL ";\n\t"
__asm__(
  ".global syscall" NL
  ".type syscall, @function" NL
  "syscall:" NL
  "   mov %rdi, %rax" NL
  "   mov %rsi, %rdi" NL
  "   mov %rdx, %rsi" NL
  "   mov %rcx, %rdx" NL
  "   mov %r8, %r10" NL
  "   mov %r9, %r8" NL
  "   mov 0x8(%rsp), %r9" NL
  "   syscall" NL
  "   ret" NL
);
#undef NL

extern uint64_t syscall(uint64_t number, ...);
