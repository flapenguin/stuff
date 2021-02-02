#include <stddef.h>
#include <stdint.h>

extern void imported_printf(const char* format, ...);

static int __entry_point_impl__() {
  imported_printf("hi from interpreted!");
  return 42;
}

#define NL "\n"
__asm__(
  ".global __entry_point__"          NL
  ".type __entry_point__, @function" NL
  "__entry_point__:"                 NL
  "   xor %rbp, %rbp"                NL
  "   mov %rsp, %rdi"                NL
  "   andq $-16, %rsp"               NL
  "   call __entry_point_impl__"     NL
);
#undef NL


