#include <stddef.h>
#include <stdint.h>

// weak attribute is needed to prevent linker from complaining about undefined symbols.
// Simply ignoring them during link time is not working and removing functions from relocations.
//  (via --warn-unresolved-symbols or --unresolved-symbols=ignore-in-object-files)
extern void imported_printf(const char* format, ...) __attribute__((weak));

int __entry_point_impl__() {
  imported_printf("hi from interpreted!\n");
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
  "   ret"                           NL
);
#undef NL


