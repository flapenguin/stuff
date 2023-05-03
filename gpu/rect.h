#pragma once
#include <stdint.h>

typedef struct u32point_t u32point_t;
struct u32point_t {
  uint32_t x;
  uint32_t y;
};

typedef struct u32rect_t u32rect_t;
struct u32rect_t {
  union {uint32_t x1; uint32_t left;};
  union {uint32_t y1; uint32_t top;};
  union {uint32_t x2; uint32_t right;};
  union {uint32_t y2; uint32_t bottom;};
};
