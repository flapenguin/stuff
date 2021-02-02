#pragma once
#include <stdint.h>

enum r_state {
  /** After add or delete */ RT_CONSISTENT = 0,
  /** Before add          */ RT_ADD = 1,
  /** Before delete       */ RT_DELETE = 2,
};

/**
 * Communicates dynamic linker state with a debugger.
 * https://code.woboq.org/userspace/glibc/elf/link.h.html
 * https://code.woboq.org/userspace/glibc/elf/dl-debug.c.html#_r_debug
 */
struct r_debug {
  /** Version of the protocol. Should be 1 */
  int32_t r_version;
  /** Head of linked list of loaded objects. */
  struct r_debug_link_map* r_map;
  /** Address of internal function called before and after loading/unloading an object, so debugger can intercept it. */
  uint64_t r_brk;
  /** Current state when r_brk is called.  */
  enum r_state r_state;
  /** Base address of the linker. */
  uint64_t r_ldbase;
};

struct r_debug_link_map {
  /** Difference between the address in the ELF file and the address in memory. */
  uint64_t l_addr;
  /** Abosule file name. */
  char* l_name;
  /** Dynamic section. */
  uint64_t* l_ld;

  struct r_debug_link_map* l_next;
  struct r_debug_link_map* l_prev;
};
