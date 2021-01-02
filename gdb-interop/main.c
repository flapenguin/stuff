#include <stddef.h>
#include <stdint.h>
#include "syscall.inl"

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

void* mmap_alloc(uint64_t size) {
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

void _start() {
  // TODO: (lldb) doesn't look into _r_debug or doesn't like incorrectly filled entries
  //       Try shared executable like ld.so?

  _r_debug.r_state = RT_ADD;
  _dl_debug_state();

  static struct r_debug_link_map so1;
  so1.l_addr = (uint64_t)mmap_alloc(0x10000);
  so1.l_name = "/lib64/ld-linux-x86-64.so.2",
  so1.l_ld = (uint64_t[]){/*DT_NULL*/ 0, 0};

  _r_debug.r_state = RT_CONSISTENT;
  _dl_debug_state();

  _r_debug.r_state = RT_ADD;
  _dl_debug_state();

  static struct r_debug_link_map so2;
  so2.l_addr = (uint64_t)mmap_alloc(0x10000);
  so2.l_name = "/lib/x86_64-linux-gnu/libc.so.6",
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
