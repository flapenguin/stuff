#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>

#include <stdio.h>
#include <stdlib.h>

#include <unistd.h>

#include <sys/mman.h>

#define DIE(FORMAT, ...) do { fprintf(stderr, "%s: " FORMAT "\n", __func__, ##__VA_ARGS__); exit(1); } while(0)

// 3.1.1.1 Opcode Column in the Instruction Summary Table (Instructions without VEX Prefix)
// /digit — A digit between 0 and 7 indicates that the ModR/M byte of the instruction uses only the r/m (register
//          or memory) operand. The reg field contains the digit that provides an extension to the instruction's opcode.
// /r     — Indicates that the ModR/M byte of the instruction contains a register operand and an r/m operand.
// cb, cw, cd, cp, co, ct — A 1-byte (cb), 2-byte (cw), 4-byte (cd), 6-byte (cp), 8-byte (co) or 10-byte (ct) value
//          following the opcode. This value is used to specify a code offset and possibly a new value for the code segment
//          register
// ib, iw, id, io — A 1-byte (ib), 2-byte (iw), 4-byte (id) or 8-byte (io) immediate operand to the instruction that
//          follows the opcode, ModR/M bytes or scale-indexing bytes. The opcode determines if the operand is a signed
//          value. All words, doublewords and quadwords are given with the low-order byte first.
// +rb, +rw, +rd, +ro — Indicated the lower 3 bits of the opcode byte is used to encode the register operand
//          without a modR/M byte

// 3.1.1.3 Instruction Column in the Opcode Summary Table
// rel8   — A relative address in the range from 128 bytes before the end of the instruction to 127 bytes after the
//          end of the instruction.
// r32    — One of the doubleword general-purpose registers: EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI; or one of
//          the doubleword registers (R8D - R15D) available when using REX.R in 64-bit mode
// r64    — One of the quadword general-purpose registers: RAX, RBX, RCX, RDX, RDI, RSI, RBP, RSP, R8–R15.
//          These are available when using REX.R and 64-bit mode.
// imm8   — An immediate byte value. The imm8 symbol is a signed number between –128 and +127 inclusive.
// imm32  — An immediate doubleword value used for instructions whose operand-size attribute is 32 bits.
// r/m8   — A byte operand that is either the contents of a byte general-purpose register ... or a byte from memory.
// r/m64  — A quadword general-purpose register or memory operand used for instructions whose operand-size attribute is 64 bits when using REX.W

// TODO: REX
// Table 2-4. REX Prefix Fields [BITS: 0100WRXB]
// *p++ = 0b01000000;

// Vol. 2A 2-5. Table 2-2. 32-Bit Addressing Forms with the ModR/M Byte
typedef enum jj_reg jj_reg;
enum jj_reg {
  /** Passed as a placeholder when register is unused. */
  jj_rNONE = 0b11111111,

  /** Passed as a mask for /digit arguments. */
  jj__rDIGIT = 0b10000000,

  jj_rax = 0b000,
  jj_rcx = 0b001,
  jj_rdx = 0b010,
  jj_rbx = 0b011,
  jj_rsp = 0b100,
  jj_rbp = 0b101,
  jj_rsi = 0b110,
  jj_rdi = 0b111
};

typedef enum jj_scale jj_scale;
enum jj_scale {
  jj_s1 = 0b00000000,
  jj_s2 = 0b01000000,
  jj_s4 = 0b10000000,
  jj_s8 = 0b11000000
};

typedef struct jj_op jj_op;
struct jj_op {
  char type; // 'r' - reg, 'm' - effective memory address, 'i' - immediate value
  union {
    jj_reg reg;
    uint64_t imm;
    struct {
      jj_reg base;
      jj_reg index;
      jj_scale scale;
      int32_t disp;
    } mem;
  };
};

typedef struct jj_ctx jj_ctx;
struct jj_ctx {
  uint8_t* ip;
  uint8_t* base;
};

jj_op jj_mkreg(jj_reg reg) {
  return (jj_op){.type = 'r', .reg = reg};
}
jj_op jj_mkimm(uint64_t imm) {
  return (jj_op){.type = 'i', .imm = imm};
}
jj_op jj_mkmem(jj_reg base, jj_reg index, jj_scale scale, int32_t disp) {
  return (jj_op){.type ='m', .mem = {.base = base, .index = index, .scale = scale, .disp = disp}};
}

void jj__ib(jj_ctx* ctx, int8_t imm) {
  *ctx->ip++ = (uint8_t)imm;
}

void jj__id(jj_ctx* ctx, int32_t imm) {
  const uint32_t uimm = (uint32_t)imm;
  *ctx->ip++ = (uimm >>  0) & 0xff;
  *ctx->ip++ = (uimm >>  8) & 0xff;
  *ctx->ip++ = (uimm >> 16) & 0xff;
  *ctx->ip++ = (uimm >> 24) & 0xff;
}

void jj__modrmsib(jj_ctx* ctx, jj_reg reg, jj_op rm) {
  if (rm.type == 'i') DIE("rm cannot be immediate");

  reg = reg & ~jj__rDIGIT;
  if (rm.type == 'r') {
    *ctx->ip++ = 0b11000000 | (reg << 3) | rm.reg;
    return;
  }

  // 2.1.5 Addressing-Mode Encoding of ModR/M and SIB Bytes
  if (rm.mem.scale != jj_s1 && rm.mem.index == jj_rsp) DIE("rsp cannot be used as base for effective address");

  const bool has_sib = rm.mem.index != jj_rNONE || rm.mem.scale != jj_s1 || rm.mem.base == jj_rsp;
  const uint8_t disp_size = rm.mem.disp == 0 ? 0 : (uint32_t)rm.mem.disp <= 255 ? 1 : 4;
  const uint8_t mod =
    disp_size == 0 ? 0b00000000 :
    disp_size == 1 ? 0b01000000 :
                     0b10000000;

  *ctx->ip++ = mod | (reg << 3) | (has_sib ? 0b100 : rm.mem.base);

  if (has_sib) {
    *ctx->ip++ = rm.mem.scale
      | ((rm.mem.index == jj_rNONE ? 0b100 : rm.mem.index) << 3)
      | (rm.mem.base == jj_rNONE ? 0b101 : rm.mem.base);
  }

  if (disp_size == 1) {
    jj__ib(ctx, rm.mem.disp);
  } else if (disp_size == 4) {
    jj__id(ctx, rm.mem.disp);
  }
}

void jj_mov(jj_ctx* ctx, jj_op dst, jj_op src) {
  if ((dst.type == 'r' || dst.type == 'm') && src.type == 'r') {
    // REX.W + 89 /r | MOV r/m64,r64 | Move r64 to r/m64.
    *ctx->ip++ = 0x48; // TODO: REX
    *ctx->ip++ = 0x89;
    jj__modrmsib(ctx, src.reg, dst);
    return;
  }

  DIE("bad operands. supported: MOV r/m64,r64");
}

void jj_lea(jj_ctx* ctx, jj_op dst, jj_op src) {
  if (dst.type == 'r' && src.type == 'm') {
    // REX.W + 8D /r | LEA r64,m | Store effective address for m in register r64.
    *ctx->ip++ = 0x48; // TODO: REX
    *ctx->ip++ = 0x8d;
    jj__modrmsib(ctx, dst.reg, src);
    return;
  }

  DIE("bad operands. supported: LEA r64,m");
}

void jj_xor(jj_ctx* ctx, jj_op dst, jj_op src) {
  if ((dst.type == 'r' || dst.type == 'm') && src.type == 'r') {
    // REX.W + 31 /r | XOR r/m64, r64 | r/m64 XOR r64
    *ctx->ip++ = 0x48; // TODO: REX
    *ctx->ip++ = 0x31;
    jj__modrmsib(ctx, src.reg, dst);
    return;
  }

  DIE("bad operands. supported: XOR r/m64, r64");
}

void jj_add(jj_ctx* ctx, jj_op dst, jj_op src) {
  if ((dst.type == 'r' || dst.type == 'm') && src.type == 'r') {
    // REX.W + 01 /r | ADD r/m64, r64 | Add r64 to r/m64.
    *ctx->ip++ = 0x48; // TODO: REX
    *ctx->ip++ = 0x01;
    jj__modrmsib(ctx, src.reg, dst);
    return;
  }

  if ((dst.type == 'r' || dst.type == 'm') && src.type == 'i') {
    // REX.W + 81 /0 id | ADD r/m64, imm32 | Add imm32 sign-extended to 64-bits to r/m64.
    *ctx->ip++ = 0x48; // TODO: REX
    *ctx->ip++ = 0x81;
    jj__modrmsib(ctx, jj__rDIGIT | 0, dst);
    jj__id(ctx, src.imm);
    return;
  }

  DIE("bad operands. ADD r/m64, r64 and ADD r/m64, imm32 are supported");
}

void jj_sub(jj_ctx* ctx, jj_op dst, jj_op src) {
  if ((dst.type == 'r' || dst.type == 'm') && src.type == 'r') {
    // REX.W + 29 /r | SUB r/m64, r64 | Subtract r64 from r/m64.
    *ctx->ip++ = 0x48; // TODO: REX
    *ctx->ip++ = 0x29;
    jj__modrmsib(ctx, src.reg, dst);
    return;
  }

  if ((dst.type == 'r' || dst.type == 'm') && src.type == 'i') {
    // REX.W + 81 /5 id | SUB r/m64, imm32 | Subtract imm32 sign-extended to 64-bits from r/m64
    *ctx->ip++ = 0x48; // TODO: REX
    *ctx->ip++ = 0x81;
    jj__modrmsib(ctx, jj__rDIGIT | 5, dst);
    jj__id(ctx, src.imm);
    return;
  }

  DIE("bad operands. SUB r/m64, r64 and SUB r/m64, imm32 are supported");
}

void jj_push(jj_ctx* ctx, jj_op op) {
  if (op.type == 'r') {
    // 50+rd | PUSH r64 | Push r64
    *ctx->ip++ = 0x50 + op.reg;
    return;
  }

  if (op.type == 'i') {
    if (op.imm <= 0xff) {
      // 6A ib | PUSH imm8 | Push imm8.
      *ctx->ip++ = 0x6a;
      jj__ib(ctx, op.imm);
    } else {
      // 68 id | PUSH imm32 | Push imm32.
      *ctx->ip++ = 0x68;
      jj__id(ctx, op.imm);
    }
    return;
  }

  if (op.type == 'm') {
    // FF /6 | PUSH r/m64 | Push r/m64
    *ctx->ip++ = 0xff;
    jj__modrmsib(ctx, jj__rDIGIT | 6, op);
  }
}

void jj_pop(jj_ctx* ctx, jj_op op) {
  if (op.type == 'r') {
    // 58+ rd | POP r64 | Pop top of stack into r64; increment stack pointer.
    *ctx->ip++ = 0x58 + op.reg;
    return;
  }

  DIE("bad operands. POP r64 is supported");
}

void jj_ret(jj_ctx* ctx) {
  // C3 | RET | Near return to calling procedure
  *ctx->ip++ = 0xc3;
}

void jj_leave(jj_ctx* ctx) {
  // C9 | LEAVE | Set RSP to RBP, then pop RBP
  *ctx->ip++ = 0xc9;
}

void jj_prologue(jj_ctx* ctx, uint32_t size) {
  jj_push(ctx, jj_mkreg(jj_rbp));
  jj_mov(ctx, jj_mkreg(jj_rbp), jj_mkreg(jj_rsp));
  jj_sub(ctx, jj_mkreg(jj_rsp), jj_mkimm(size));
}

void jj_epilogue(jj_ctx* ctx) {
  jj_leave(ctx);
  jj_ret(ctx);
}

void jj_dump_disas(jj_ctx* ctx) {
  FILE* out = fopen("./.jj.dump", "wb");
  fwrite(ctx->base, 1, ctx->ip - ctx->base, out);
  fclose(out);

  system(
    "objdump -D -b binary -M intel,x86-64 -m i386 -j .data ./.jj.dump | awk -F'\n' '$0 ~ /<\\.data>/,0';"
    "rm ./.jj.dump;"
  );
  printf("\n");
}

int main() {
  uint32_t page_size = getpagesize();

  const uint64_t prot = PROT_READ | PROT_WRITE | PROT_EXEC;
  const uint64_t flags = MAP_PRIVATE | MAP_ANONYMOUS;
  uint8_t* const page = mmap(0, page_size, prot, flags, -1, 0);
  if (page == MAP_FAILED) {
    perror("mmap");
    return 1;
  }

  jj_ctx __ctx = {page, page};
  jj_ctx* ctx = &__ctx;

  jj_prologue(ctx, 0x100);
  jj_sub(ctx, jj_mkmem(jj_rsi, jj_rNONE, jj_s1, 0), jj_mkimm(0x700));
  jj_xor(ctx, jj_mkreg(jj_rax), jj_mkreg(jj_rax));
  jj_add(ctx, jj_mkreg(jj_rax), jj_mkreg(jj_rdi));
  jj_lea(ctx, jj_mkreg(jj_rax), jj_mkmem(jj_rax, jj_rax, jj_s4, 0));
  jj_lea(ctx, jj_mkreg(jj_rax), jj_mkmem(jj_rax, jj_rax, jj_s1, 0));
  jj_epilogue(ctx);

  jj_dump_disas(ctx);

  uint64_t (*fn)(uint64_t x, uint64_t* y) = (void*)ctx->base;
  uint64_t arg = 0x900;
  uint64_t ret = fn(100, &arg);
  printf("ret = %" PRIu64 "\narg = 0x%" PRIx64 "\n", ret, arg);

  return 0;
}
