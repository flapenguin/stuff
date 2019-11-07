#include "wasm.h"

#include <stddef.h>
#include <stdbool.h>
#include <string.h>

const uint8_t* g_src;
const uint8_t* g_end;

static int32_t peek_byte() {
  return g_src < g_end ? *g_src : -1;
}

static uint8_t consume_u8() {
  return *(g_src++);
}

static uint32_t consume_u32() {
  uint32_t result = 0;
  uint32_t shift = 0;

  while (true) {
    const uint8_t byte = consume_u8();
    result |= ((byte & 0x7f) << shift);

    if (byte & 0x80) shift += 7;
    else break;
  }

  return result;
}

static int consume_const_bytes(const uint8_t* bytes, size_t length) {
  if (g_src > g_end - length) return 1;
  int r = memcmp(g_src, bytes, length);
  if (r == 0) g_src += length;
  return r == 0 ? 0 : -1;
}

static void assert_vec_length(uint32_t length, const uint8_t* ctx) {
  if (length > wasm__vec_max) {
    wasm_die("error: maximum vector length (%d) exceeded @ %s", wasm__vec_max, ctx);
  }
}

static enum wasm_valtype parse_valtype() {
  switch (consume_u8()) {
    case wasm_i32: return wasm_i32;
    case wasm_i64: return wasm_i64;
    case wasm_f32: return wasm_f32;
    case wasm_f64: return wasm_f64;
    default: wasm_die("bad valtype");
  }
}

static struct wasm_functype parse_functype() {
  if (consume_u8() != 0x60) wasm_die("bad functype");
  struct wasm_functype result;

  uint32_t params_length = result.params_length = consume_u32();
  assert_vec_length(params_length, "functype/vec(params)");
  while (params_length) result.params[--params_length] = parse_valtype();

  uint32_t results_length = result.results_length = consume_u32();
  assert_vec_length(results_length, "functype/vec(results)");
  while (results_length) result.results[--results_length] = parse_valtype();

  return result;
}

static struct wasm_type_section parse_type_section() {
  struct wasm_type_section result;
  uint32_t length = result.types_length = consume_u32();
  assert_vec_length(length, "type_section/vec(functype)");
  while (length) result.types[--length] = parse_functype();

  return result;
}

static struct wasm_function_section parse_function_section() {
  struct wasm_function_section result;
  uint32_t length = result.types_length = consume_u32();
  assert_vec_length(length, "function_section/vec(idx)");
  while (length) result.types[--length] = consume_u32();

  return result;
}

static struct wasm_export parse_export() {
  struct wasm_export result;

  result.name_length = consume_u32();
  result.name = g_src;
  g_src += result.name_length;

  result.type = consume_u8();
  result.idx = consume_u32();

  return result;
}

static struct wasm_export_section parse_export_section() {
  struct wasm_export_section result;
  uint32_t length = result.exports_length = consume_u32();
  assert_vec_length(length, "export_section/vec(export)");
  while (length) result.exports[--length] = parse_export();

  return result;
}

static struct wasm_code parse_code() {
  struct wasm_code result;

  result.size = consume_u32();
  const uint8_t* const func_begin = g_src;
  uint32_t locals_length = result.locals_length = consume_u32();
  assert_vec_length(locals_length, "code/func/vec(locals)");
  while (locals_length) {
    const uint32_t count = consume_u32();
    const enum wasm_valtype valtype = parse_valtype();
    result.locals[--locals_length] = (struct wasm_code_locals){
      .count = count,
      .valtype = valtype,
    };
  }

  g_src = func_begin + result.size;

  // https://webassembly.github.io/spec/core/binary/instructions.html#binary-expr
  if (g_src[-1] != 0x0b) wasm_die("code/func/expr is not ended with `end` (0x0B)");

  return result;
}

static struct wasm_code_section parse_code_section() {
  struct wasm_code_section result;
  uint32_t length = result.code_length = consume_u32();
  assert_vec_length(length, "code_section/vec(code)");
  while (length) result.code[--length] = parse_code();

  return result;
}

struct wasm_module parse_module(uint8_t* src, size_t length) {
  g_src = src;
  g_end = src + length;

  if (consume_const_bytes("\0asm\x01\x00\x00\x00", 8) < 0) wasm_die("malformed magic");

  struct wasm_module result;
  while (peek_byte() >= 0) {
    uint8_t type = consume_u8();
    uint32_t size = consume_u32();
    switch (type) {
    case  0: g_src += size; wasm_log("[not implemented] custom section");  break;
    case  1: result.type_section = parse_type_section();                   break;
    case  2: g_src += size; wasm_log("[not implemented] import section");  break;
    case  3: result.function_section = parse_function_section();           break;
    case  4: g_src += size; wasm_log("[not implemented] table section");   break;
    case  5: g_src += size; wasm_log("[not implemented] memory section");  break;
    case  6: g_src += size; wasm_log("[not implemented] global section");  break;
    case  7: result.export_section = parse_export_section();               break;
    case  8: g_src += size; wasm_log("[not implemented] start section");   break;
    case  9: g_src += size; wasm_log("[not implemented] element section"); break;
    case 10: result.code_section = parse_code_section();                   break;
    case 11: g_src += size; wasm_log("[not implemented] data section");    break;
    default: wasm_log("[not implemented] section %d\n", type);             break;
    }
  }

  return result;
}
