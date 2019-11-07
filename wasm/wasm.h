#pragma once

#include <stddef.h>
#include <stdint.h>

enum {
  wasm__vec_max = 32
};

// https://webassembly.github.io/spec/core/binary/types.html#value-types
enum wasm_valtype {
  wasm_i32 = 0x7f,
  wasm_i64 = 0x7e,
  wasm_f32 = 0x7d,
  wasm_f64 = 0x7c,
};

// https://webassembly.github.io/spec/core/binary/modules.html#export-section
enum wasm_exportdesc {
  wasm_exportdesc_funcidx   = 0x00,
  wasm_exportdesc_tableidx  = 0x01,
  wasm_exportdesc_memidx    = 0x02,
  wasm_exportdesc_globalidx = 0x03,
};

struct wasm_functype {
  uint32_t params_length;
  enum wasm_valtype params[wasm__vec_max];
  uint32_t results_length;
  enum wasm_valtype results[wasm__vec_max];
};

struct wasm_type_section {
  uint32_t types_length;
  struct wasm_functype types[wasm__vec_max];
};

struct wasm_function_section {
  uint32_t types_length;
  uint32_t types[wasm__vec_max];
};

struct wasm_export {
  uint32_t name_length;
  const uint8_t* name;
  enum wasm_exportdesc type;
  uint32_t idx;
};

struct wasm_export_section {
  uint32_t exports_length;
  struct wasm_export exports[wasm__vec_max];
};

struct wasm_code_locals {
  enum wasm_valtype valtype;
  uint32_t count;
};

struct wasm_code {
  uint32_t size;
  uint8_t* code;
  uint32_t locals_length;
  struct wasm_code_locals locals[wasm__vec_max];
};

struct wasm_code_section {
  uint32_t code_length;
  struct wasm_code code[wasm__vec_max];
};

struct wasm_module {
  struct wasm_type_section type_section;
  struct wasm_function_section function_section;
  struct wasm_export_section export_section;
  struct wasm_code_section code_section;
};

#ifdef __GNUC__
#define WASM_FORMAT_ATTRIBUTE __attribute__((format (printf, 1, 2)))
#else
#define WASM_FORMAT_ATTRIBUTE
#endif

extern struct wasm_module parse_module(uint8_t* src, size_t length);
extern int wasm_die(const char* format, ...) WASM_FORMAT_ATTRIBUTE;
extern int wasm_log(const char* format, ...) WASM_FORMAT_ATTRIBUTE;
