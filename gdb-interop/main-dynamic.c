// Tell linker it's ok to link all non-static functions from multiple files in compile time.
#pragma GCC visibility push (hidden)

#include <stdint.h>
#include <stdarg.h>
#include <elf.h> // Elf64_* DT_* AT_*
#include "r_debug.h"

// Linked adds those for us. We can reference them.
extern uint64_t _GLOBAL_OFFSET_TABLE_[] __attribute__((visibility("hidden")));
extern uint64_t _DYNAMIC[] __attribute__((visibility("hidden")));

static const uint64_t _DYNAMIC_DT_DEBUG[2] __attribute__((section(".dynamic"))) = {DT_DEBUG, 0};

extern uint64_t syscall(uint64_t number, ...);

static int xx_strlen(const char* str) {
  int r = 0;
  while (*str++) r++;
  return r;
}

static int xx_strcmp(const char* lhs, const char* rhs) {
  while (*lhs && *lhs == *rhs) { lhs++; rhs++; }
  return *lhs - *rhs;
}

static void xx_printf(const char* format, ...) {
  va_list args;
  va_start(args, format);
  while (1) {
    const char* start = format;
    while (*format && *format != '%') format++;
    syscall(/*SYS_write*/1, 1, start, format - start);
    if (!*format) break;
    format++;
    switch (*format++) {
      case 's': {
        const char* arg = va_arg(args, char*);
        syscall(/*SYS_write*/1, 1, arg, xx_strlen(arg)); break;
        break;
      }
      case 'p': {
        uint64_t arg = va_arg(args, uint64_t);
        static char str[21] = "0x????_????_????_????";
        static const char map[16] = "0123456789abcdef";
        int i = sizeof(str) - 1;
        while (i >= 2) {
          str[i--] = map[arg & 0xf];
          if (i == 6 || i == 11 || i == 16) i--;
          arg = arg >> 4;
        }
        syscall(/*SYS_write*/1, 1, str, sizeof(str)); break;
        break;
      }
      case 'x': {
        uint64_t arg = va_arg(args, uint64_t);
        static char str[16] = "????????????????";
        static const char map[16] = "0123456789abcdef";
        int i = sizeof(str) - 2;
        while (1) {
          str[i + 1] = map[arg & 0xf];
          arg = arg >> 4;
          str[i] = map[arg & 0xf];
          arg = arg >> 4;
          if (!arg) break;
          i -= 2;
        }
        syscall(/*SYS_write*/1, 1, str + i, sizeof(str) - i); break;
        break;
      }
      case 'd': {
        uint64_t arg = va_arg(args, uint64_t);
        static char str[21];
        char* p = str + sizeof(str) - 1;
        while (1) {
          *p = '0' + arg % 10;
          arg /= 10;
          if (!arg) break;
          p--;
        }
        syscall(/*SYS_write*/1, 1, p, str + sizeof(str) - p); break;
        break;
      }
      default: {
        break;
      }
    }
  }
  va_end(args);
}

#define die(...) do { xx_printf(__VA_ARGS__); syscall(/*SYS_exit*/60, 1); } while(0)

static void reloc(const char* type, Elf64_Ehdr* ehdr, uint64_t dynv[static DT_NUM], Elf64_Rela* rela, int rela_len);
extern struct r_debug _r_debug;

#define AT_NUM 64
void __relocate_self(uint64_t* sp) {
  const uint64_t argc = sp[0];
  char** const argv = (void*)&sp[1];
  char** const env = (void*)(argv + argc + 1 /*trailing null*/);
  const uint64_t envc = ({uint64_t x; while (env[x]) {x++;} x;});
  uint64_t* const raw_auxv = (void*)(env + envc + 1 /*trailing null*/);
  uint64_t* const raw_dynv = _DYNAMIC;

  // TODO: fixup executable's dynv, not linker's
  for (int i = 0; raw_dynv[i]; i += 2) {
    if (raw_dynv[i] == DT_DEBUG) {
      raw_dynv[i + 1] = (uint64_t)&_r_debug;
      xx_printf("fixed up DT_DEBUG to %p\n", raw_dynv[i + 1]);
      break;
    }
  }

  for (uint64_t i = 0; i < argc; i++)
    xx_printf("arg = %s\n", argv[i]);
  xx_printf("env: %d vars\n", envc);

  xx_printf("auxv: %p\n", (uint64_t)raw_auxv);

  uint64_t auxv[AT_NUM] = {0};
  for (int i = 0; raw_auxv[i]; i += 2) {
    xx_printf("auxv[%d]: %d = 0x%x\n", i / 2, raw_auxv[i], raw_auxv[i + 1]);
    if (raw_auxv[i] < AT_NUM) auxv[raw_auxv[i]] = raw_auxv[i + 1];
  }

  uint64_t dynv[DT_NUM] = {0};
  for (int i = 0; raw_dynv[i]; i += 2) {
    xx_printf("dynv[%d]: %d = 0x%x\n", i / 2, raw_dynv[i], raw_dynv[i + 1]);
    if (raw_dynv[i] < DT_NUM) dynv[raw_dynv[i]] = raw_dynv[i + 1];
  }

  const uint64_t phnum = auxv[AT_PHNUM];
  const uint64_t phent = auxv[AT_PHENT];

  Elf64_Ehdr* ehdr = (void*)auxv[AT_BASE];
  Elf64_Phdr* phdr = 0;
  if (ehdr) {
    phdr = (void*)ehdr + ehdr->e_phoff;
  } else {
    // AT_BASE is populated by kernel only for interpreters.
    phdr = (void*)auxv[AT_PHDR];
    for (int i = 0; i < phnum; i++) {
      const Elf64_Phdr* ph = (void*)phdr + phent*i;
      if (ph->p_type == PT_DYNAMIC) {
        ehdr = (void*)raw_dynv - ph->p_vaddr;
        break;
      }
    }
  }

  xx_printf("ehdr = %p\n", ehdr);

  xx_printf("relocating {\n");
  if (dynv[DT_REL]) die("ERROR: DT_REL should not be used in x86_64\n");
  if (dynv[DT_PLTREL] && dynv[DT_PLTREL] != DT_RELA) die("ERROR: DT_PLTREL should not use DT_REL in x86_64\n");
  if (dynv[DT_RELA])   reloc("RELA  ", ehdr, dynv, (void*)ehdr + dynv[DT_RELA],   dynv[DT_RELASZ]   / dynv[DT_RELAENT]);
  if (dynv[DT_PLTREL]) reloc("JMPREL", ehdr, dynv, (void*)ehdr + dynv[DT_JMPREL], dynv[DT_PLTRELSZ] / dynv[DT_RELAENT]);
  xx_printf("}\n");
}

static void reloc(const char* type, Elf64_Ehdr* ehdr, uint64_t dynv[static DT_NUM], Elf64_Rela* rela, int rela_len) {
  const char* strtab = (void*)ehdr + dynv[DT_STRTAB];
  const Elf64_Sym* symtab = (void*)ehdr + dynv[DT_SYMTAB];

  const uint32_t* hashtab = (void*)ehdr + dynv[DT_HASH];
  const uint32_t symlen = hashtab[1]; // nchain from DT_HASH is effectively the symtab length

  for (int i = 0; i < rela_len; i++) {
    const char* wanted_symname = strtab + symtab[ELF64_R_SYM(rela[i].r_info)].st_name;

    const uint64_t addend = rela[i].r_addend;
    xx_printf("  %s looking up '%s + 0x%x'\n", type, wanted_symname, addend);

    const Elf64_Sym* found_sym = 0;
    if (*wanted_symname) {
      // Linear lookup via DT_HASH for simplicity.
      // In real life it's better to use DT_HASH properly or even better use DT_GNU_HASH.
      // https://flapenguin.me/elf-dt-hash
      // https://flapenguin.me/elf-dt-gnu-hash
      for (uint32_t i = 0; i < symlen; i++) {
        const Elf64_Sym* sym = &symtab[i];
        const char* symname = strtab + sym->st_name;
        if (xx_strcmp(symname, wanted_symname) == 0) {
          found_sym = sym;
          break;
        }
      }

      if (!found_sym) {
        xx_printf("    not found\n");
        continue;
      }
    }

    const uint64_t symval = found_sym ? (uint64_t)((void*)ehdr + found_sym->st_value) : 0;
    xx_printf("    found at %p\n", symval);

    void* target = (void*)ehdr + rela[i].r_offset;
    switch (ELF64_R_TYPE(rela[i].r_info)) {
      case R_X86_64_NONE: break;
      case R_X86_64_RELATIVE: {
        *(uint64_t*)target = (uint64_t)((void*)ehdr + addend);
        break;
      }
      case /* GOT */ R_X86_64_GLOB_DAT:
      case /* PLT */ R_X86_64_JUMP_SLOT: {
        *(uint64_t*)target = symval;
        break;
      }
      case R_X86_64_64: {
        *(uint64_t*)target = symval + addend;
        break;
      }
    }
  }
}
