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
// imm8   — An immediate byte value. The imm8 symbol is a signed number between –128 and +127 inclusive.
// imm32  — An immediate doubleword value used for instructions whose operand-size attribute is 32 bits.
// r/m8   — A byte operand that is either the contents of a byte general-purpose register ... or a byte from memory.
// r/m32  — A doubleword general-purpose register or memory operand used for instructions whose operandsize attribute is 32 bits

// TODO: REX
// Table 2-4. REX Prefix Fields [BITS: 0100WRXB]
// *p++ = 0b01000000;

// Vol. 2A 2-5. Table 2-2. 32-Bit Addressing Forms with the ModR/M Byte
typedef enum jj_reg jj_reg;
enum jj_reg {
  /** Passed as a placeholder when register is unused. */
  jj_r00 = 0b11111111,

  jj__sib = 0x04,
  jj__sib_no_base = 0b101,

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
  jj_s0 = 0b11111111,
  jj_s1 = 0b00000000,
  jj_s2 = 0b01000000,
  jj_s4 = 0b10000000,
  jj_s8 = 0b11000000
};

typedef struct jj_addrmode jj_addrmode;
struct jj_addrmode {
  char type; // 'r' - reg, 'e' - effective address, 'i' - immediate value
  union {
    jj_reg reg;
    uint64_t imm;
    struct {
      jj_reg base;
      jj_reg index;
      jj_scale scale;
      uint32_t disp;
    } eff;
  };
};

typedef struct jj_ctx jj_ctx;
struct jj_ctx {
  uint8_t* ip;
  uint32_t last_disp;
  uint32_t last_disp_size;
};

jj_addrmode jj_mkreg(jj_reg reg) {
  return (jj_addrmode){.type = 'r', .reg = reg};
}
jj_addrmode jj_mkimm(uint64_t imm) {
  return (jj_addrmode){.type = 'i', .imm = imm};
}
jj_addrmode jj_mkeff(jj_reg base, jj_reg index, jj_scale scale, uint32_t disp) {
  return (jj_addrmode){.type ='e', .eff = {.base = base, .index = index, .scale = scale, .disp = disp}};
}

void jj__modrmsib(jj_ctx* ctx, jj_reg reg, jj_addrmode src, uint8_t opcode_ext) {
  if (src.type == 'i' && opcode_ext <= 0b111) {
    *ctx->ip++ = 0b11000000 | (opcode_ext << 3) | reg;
    ctx->last_disp = src.imm;
    ctx->last_disp_size = 4;
    return;
  }

  if (opcode_ext != 0xff) DIE("opcode extensions (/digit) are supproted only in immediate forms");

  if (src.type == 'r') {
    *ctx->ip++ = 0b11000000 | (reg << 3) | src.reg;
    ctx->last_disp = 0;
    ctx->last_disp_size = 0;
    return;
  }

  const jj_reg base = src.eff.base;
  const jj_reg index = src.eff.index;
  const jj_scale scale = src.eff.scale;
  const uint32_t disp = src.eff.disp;

  const bool scaled = scale != jj_s0;
  uint8_t disp_size = scaled ? 4 : disp <= 255 ? 1 : 4;

  // 2.1.5 Addressing-Mode Encoding of ModR/M and SIB Bytes
  if (scaled && base == jj_rsp) DIE("rsp cannot be used as base for effective address");

  const uint8_t mod =
    disp_size == 1 ? 0b01000000 :
    disp == 0      ? 0b00000000 :
                     0b10000000;

  const uint8_t rm = (scaled ? jj__sib : base);

  *ctx->ip++ = mod | (reg << 3) | rm;

  if (scaled) {
    if (base == jj_r00) {
      // 2.1.5 Addressing-Mode Encoding of ModR/M and SIB Bytes
      // The [*] nomenclature means a disp32 with no base if the MOD is 00B. Otherwise, [*] means disp8 or disp32 + [EBP].
      // 01 [scaled index] + disp8 + [EBP]
      // 10 [scaled index] + disp32 + [EBP]
      if (mod != 0b00000000) DIE("crazy addressing modes with extra ebp are not supported");
      disp_size = 4;
    } else {
      if (disp != 0) DIE("displacement is not supported when both index and base are specified");
      disp_size = 0;
    }

    *ctx->ip++ = scale | (index << 3) | (base == jj_r00 ? jj__sib_no_base : base);
  }

  ctx->last_disp = disp;
  ctx->last_disp_size = disp_size;
}

void jj__ib(jj_ctx* ctx, uint8_t imm) {
  *ctx->ip++ = imm;
}

void jj__id(jj_ctx* ctx, uint32_t imm) {
  *ctx->ip++ = (imm >>  0) & 0xff;
  *ctx->ip++ = (imm >>  8) & 0xff;
  *ctx->ip++ = (imm >> 16) & 0xff;
  *ctx->ip++ = (imm >> 24) & 0xff;
}

void jj__disp(jj_ctx* ctx) {
  const uint32_t disp = ctx->last_disp;
  if (ctx->last_disp_size == 1) {
    jj__ib(ctx, disp);
  } else if (ctx->last_disp_size == 4) {
    jj__id(ctx, disp);
  }
}

void jj_mov(jj_ctx* ctx, jj_addrmode dst, jj_addrmode src) {
  if (dst.type != 'r') DIE("dst must be register");
  if (src.type != 'r') DIE("src must be register");

  // 89 /r | MOV r/m32,r32 | Move r32 to r/m32.
  *ctx->ip++ = 0x89;
  jj__modrmsib(ctx, src.reg, dst, 0xff);
  jj__disp(ctx);
}

void jj_lea(jj_ctx* ctx, jj_addrmode dst, jj_addrmode src) {
  if (dst.type != 'r') DIE("dst must be register");
  if (src.type != 'e') DIE("src must be effective address");

  // 8D /r | LEA r32,m | Store effective address for m in register r32
  *ctx->ip++ = 0x8d;
  jj__modrmsib(ctx, dst.reg, src, 0xff);
  jj__disp(ctx);
}

void jj_xor(jj_ctx* ctx, jj_addrmode dst, jj_addrmode src) {
  if (dst.type != 'r') DIE("dst must be register");

  // 31 /r | XOR r/m32, r32 | r/m32 XOR r32.
  *ctx->ip++ = 0x31;
  jj__modrmsib(ctx, dst.reg, src, 0xff);
  jj__disp(ctx);
}

void jj_add(jj_ctx* ctx, jj_addrmode dst, jj_addrmode src) {
  if (dst.type != 'r' && dst.type != 'e') DIE("dst must be register or effective address");
  if (src.type != 'i') DIE("src other than immediate is not implemented");

  if (src.imm == 0) return;

  // 81 /0 id | ADD r/m32, imm32 | Add imm32 to r/m32.
  *ctx->ip++ = 0x81;
  jj__modrmsib(ctx, dst.reg, src, 0);
  jj__disp(ctx);
}

void jj_sub(jj_ctx* ctx, jj_addrmode dst, jj_addrmode src) {
  if (src.type != 'i') DIE("src other than immediate is not implemented");

  if (src.imm == 0) return;

  if (dst.type == 'r') {
    // 81 /5 id | SUB r/m32, imm32 | Subtract imm32 from r/m32.
    *ctx->ip++ = 0x81;
    jj__modrmsib(ctx, dst.reg, src, 5);
    jj__disp(ctx);
    return;
  }

  DIE("effective address as dst is not implemented");
}

void jj_push(jj_ctx* ctx, jj_addrmode op) {
  if (op.type == 'r') {
    // 50+rd | PUSH r32 | Push r32.
    *ctx->ip++ = 0x50 + op.reg;
    return;
  }

  if (op.type == 'i') {
    if (op.imm <= 255) {
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

  DIE("effective addresses are not implemented");
}

void jj_pop(jj_ctx* ctx, jj_addrmode op) {
  if (op.type == 'r') {
    // 58+ rd | POP r32 | Pop top of stack into r32; increment stack pointer.
    *ctx->ip++ = 0x58 + op.reg;
    return;
  }

  DIE("effective addresses are not implemented");
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
  *ctx->ip++ = 0x48; // TODO: REX
  jj_mov(ctx, jj_mkreg(jj_rbp), jj_mkreg(jj_rsp));
  *ctx->ip++ = 0x48; // TODO: REX
  jj_sub(ctx, jj_mkreg(jj_rsp), jj_mkimm(size));
}

void jj_epilogue(jj_ctx* ctx) {
  jj_leave(ctx);
  jj_ret(ctx);
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

  uint8_t* const fn_p = page;
  jj_ctx st_ctx = {fn_p, 0, 0}, *ctx = &st_ctx;

  if (0) {
    // jj_mov(ctx, jj_mkreg(jj_rax), jj_mkreg(jj_rdi)); // ~~ = edi
    jj_lea(ctx, jj_mkreg(jj_rax), jj_mkeff(jj_rdi, jj_r00, jj_s0, 0x104)); // ~~ +0x104
    // jj_lea(ctx, jj_mkreg(jj_rax), jj_mkeff(jj_r00, jj_rdi, jj_s4, 0x00));  // ~~ *4
    // jj_lea(ctx, jj_mkreg(jj_rax), jj_mkeff(jj_rdi, jj_rdi, jj_s4, 0x00));  // ~~ *5
    jj_ret(ctx);
  }

  if (1) {
    jj_prologue(ctx, 0x404);
    jj_xor(ctx, jj_mkreg(jj_rax), jj_mkreg(jj_rax));
    // jj_sub(ctx, jj_mkeff(jj_rsi, jj_r00, jj_s0, 0), jj_mkimm(0xf));
    jj_epilogue(ctx);
  }

  FILE* out = fopen("./jitted", "wb");
  fwrite(fn_p, 1, ctx->ip - page, out);
  fclose(out);
  system("objdump -D -b binary -M intel,x86-64 -m i386 ./jitted; rm ./jitted");

  uint64_t (*fn)(uint64_t x, uint64_t* y) = (void*)fn_p;
  uint64_t y = 0x100;
  uint64_t ret = fn(1, &y);
  printf("result = 0x%" PRIx64 " y = 0x%" PRIx64 "\n", ret, y);

  return 0;
}
