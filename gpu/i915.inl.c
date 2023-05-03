#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <drm/drm.h>
#include <drm/i915_drm.h>

#include "rect.h"

typedef struct i915cmd_buf_t i915cmd_buf_t;
struct i915cmd_buf_t {
  uint32_t* start;
  uint32_t* data;
  struct {
    struct drm_i915_gem_relocation_entry* values;
    size_t size;
  } relocations;
};

typedef struct i915gem_t i915gem_t;
struct i915gem_t {
  uint32_t handle;
  uint32_t delta;
  uint32_t size;
};

typedef struct i915surface_t i915surface_t;
struct i915surface_t {
  i915gem_t gem;
  uint32_t width;
  uint32_t height;
  uint32_t pitch;
};

static i915gem_t i915gem_with_delta(i915gem_t self, uint32_t delta) {
  i915gem_t copy = self;
  copy.delta = delta;
  return copy;
}

static void i915cmd_MI_NOOP(i915cmd_buf_t* buf) {
  *buf->data++ = 0x00;
}

static void i915cmd_MI_BATCH_BUFFER_END(i915cmd_buf_t* buf) {
  *buf->data++ = 0xA << 23;
  i915cmd_MI_NOOP(buf);
}

static void i915cmd_buf_address(i915cmd_buf_t* buf, i915gem_t gem) {
  buf->relocations.values = realloc(buf->relocations.values, sizeof(struct drm_i915_gem_relocation_entry) * (buf->relocations.size + 1));
  buf->relocations.values[buf->relocations.size] = (struct drm_i915_gem_relocation_entry){
    .offset = (buf->data - buf->start) * sizeof(uint32_t),
    .target_handle = gem.handle,
    .delta = gem.delta
  };
  buf->relocations.size++;

  *buf->data++ = 0xcafedead;
  *buf->data++ = 0xcafedead;
}

static void i915cmd_MI_STORE_DATA_IMM(i915cmd_buf_t* buf, i915gem_t gem, uint32_t size, uint32_t* values) {
  *buf->data++ = (0x20 << 23) | /* Store Dword */ (1 + size);
  i915cmd_buf_address(buf, gem);
  memcpy(buf->data, values, size * sizeof(values[0]));
  buf->data += size;

  // Minimum 2 DWords, one may be unused.
  if (size == 1) *buf->data++ = 0;

  // Padding
  if (size % 2 == 0) i915cmd_MI_NOOP(buf);
}

static inline void i915cmd_MI_STORE_DATA_IMM_uint32(i915cmd_buf_t* buf, i915gem_t gem, uint32_t value) {
  i915cmd_MI_STORE_DATA_IMM(buf, gem, 1, (uint32_t[]){value});
}

static inline void i915cmd_MI_STORE_DATA_IMM_uint64(i915cmd_buf_t* buf, i915gem_t gem, uint64_t value) {
  i915cmd_MI_STORE_DATA_IMM(buf, gem, 2, (uint32_t[]){
    (value >>  0) & 0xffffffff,
    (value >> 32) & 0xffffffff
  });
}

static void i915cmd_XY_COLOR_BLT(i915cmd_buf_t* buf, i915surface_t target, u32rect_t target_rect, uint32_t color) {
  *buf->data++ = (0x2 << 29) | (0x50 << 22) | /* write alpha  */(0x1 << 21) | /* write rgb */(0x1 << 20) | /* no tiling */(0x0 << 11) | /* length */(0x5);
  *buf->data++ = /* no clipping */ (0b0 << 30) | /* - 32 bit color */(0b11 << 24) | /* raster op: P */(0xf0 << 16) | target.pitch;
  *buf->data++ = (target_rect.top << 16) | target_rect.left;
  *buf->data++ = (target_rect.bottom << 16) | target_rect.right;
  i915cmd_buf_address(buf, target.gem);
  *buf->data++ = color;

  i915cmd_MI_NOOP(buf);
}

static void i915cmd_XY_FAST_COPY_BLT(i915cmd_buf_t* buf, i915surface_t target,  u32rect_t target_rect, i915surface_t source, u32point_t source_point) {
  *buf->data++ = ((0x2 << 29) | (0x42 << 22)) | /* no tiling src */(0x0 << 20) | /* no tiling dst */(0x0 << 13) | /* length */(0x8);
  *buf->data++ = /* 32bit color */(0b011 << 24) | target.pitch;
  *buf->data++ = (target_rect.top << 16) | target_rect.left;
  *buf->data++ = (target_rect.bottom << 16) | target_rect.right;
  i915cmd_buf_address(buf, target.gem);
  *buf->data++ = (source_point.y << 16) | source_point.x;
  *buf->data++ = source.pitch;
  i915cmd_buf_address(buf, source.gem);
}
