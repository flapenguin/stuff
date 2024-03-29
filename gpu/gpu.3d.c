#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <math.h>
#include <memory.h>

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/mman.h>

#include <linux/netlink.h>

#include <drm/drm.h>
#include <drm/i915_drm.h>
#include "i915.inl.c"
#include "rect.h"

/* NOTE: xcb/xlib is optional, but surely make life easier */
#include <xcb/xcb.h>
#include <xcb/dri3.h>
#include <xcb/randr.h>

#include "util.h"

#define ioctlQ(fd, request, arg) ({ \
  fputs("\033[2m", stderr); \
  int __linelen; \
  fprintf(stderr, ":%-4dioctl %-40s %n... \033[0m\n", __LINE__, #request, &__linelen); \
  int __ioctl0_ret = ioctl(fd, request, arg); \
  fprintf(stderr, "%*s", __linelen, ""); \
  if (__ioctl0_ret == -1) fprintf(stderr, "\033[0;91m... %s", strerror(errno)); else fputs("\033[2m... ok", stderr); \
  fputs("\033[0m\n", stderr); \
  if (__ioctl0_ret == -1) exit(1); \
  __ioctl0_ret; \
})

#define LOG(STR) do { fputs(STR, stderr); fputc('\n', stderr); } while (0)
#define SECTION(STR, ...) ({ LOG(STR); __VA_ARGS__; })
#define XSECTION(STR, ...) ({ LOG("(disabled) " STR); 0; })

#define DUMP(x) fprintf(stderr, DUMPF(x, #x " = ", "\n"), x)
#define DUMPEX(x, prefix) fprintf(stderr, DUMPF(x, prefix #x " = ", "\n"), x)

static uint32_t align_u32(uint32_t value, uint32_t alignment) {
  return ((value + alignment - 1) / alignment) * alignment;
}

static void die(const char* format, ...) {
  fprintf(stderr, "DIE ");

  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);

  exit(1);
}

static const int window_width = 512;
static const int window_height = 512;

static const char* get_path_for_fd(int fd) {
  char* path = 0;
  char buf[32];
  asprintf(&path, "/proc/self/fd/%d", fd);
  int n = readlink(path, buf, sizeof(buf));
  free(path);
  path = 0;
  asprintf(&path, "%.*s", n, buf);
  return path;
}

static i915surface_t load_ppm(const char* path, int dri_fd) {
  FILE* f = fopen(path, "rb");
  if (!f) die(strerror(errno));

  char* line = 0;
  size_t line_size = 0;

  getline(&line, &line_size, f);
  if (strcmp(line, "P6\n") != 0) die("bad ppm");

  uint32_t width = 0;
  uint32_t height = 0;
  getline(&line, &line_size, f);
  sscanf(line, "%u %u", &width, &height);

  getline(&line, &line_size, f);
  /* just skip max color */

  const size_t body_pitch = 3;
  const size_t body_size = width * height * body_pitch;
  uint8_t* body = malloc(body_size);
  if (fread(body, body_size, 1, f) != 1) die("failed to read whole file");

  free(line);
  fclose(f);

  const uint32_t pitch = align_u32(width, 8);
  printf("aligned width %u => %u\n", width, pitch);
  const uint32_t size = pitch * height * sizeof(uint32_t);
  const uint32_t handle = ({
    struct drm_i915_gem_create req = {.size = size};
    ioctlQ(dri_fd, DRM_IOCTL_I915_GEM_CREATE, &req);
    req.handle;
  });

  uint8_t* mem = ({
    struct drm_i915_gem_mmap req = {.handle = handle, .size = size};
    ioctlQ(dri_fd, DRM_IOCTL_I915_GEM_MMAP, &req);
    (void*)req.addr_ptr;
  });

  memset(mem, 0, size);

  for (uint32_t y = 0; y < height; y++) {
    for (uint32_t x = 0; x < width; x++) {
      uint8_t* const src = &body[(y*width + x) * body_pitch];
      uint8_t* const dst = &mem[(y*pitch + x) * sizeof(uint32_t)];

      /* argb */
      dst[0] = src[2]; /* b */
      dst[1] = src[1]; /* g */
      dst[2] = src[0]; /* r */
      dst[3] = 0; /* a */
    }
  }

  munmap(mem, size);
  free(body);

  return (i915surface_t){.gem = {.handle = handle, .delta = 0, .size = size}, .width = width, .height = height, .pitch = pitch * sizeof(uint32_t)};
}

int main(int argc, char* argv[]) {
  xcb_connection_t *const c = xcb_connect(NULL, NULL);
  if (xcb_connection_has_error(c) > 0) die("Cannot open display\n");

  xcb_screen_t* const s = xcb_setup_roots_iterator(xcb_get_setup(c)).data;

  /* Create window. */
  xcb_window_t window = ({
    const uint32_t mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
    uint32_t values[2] = {s->white_pixel, XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS};

    xcb_window_t window = xcb_generate_id(c);
    xcb_create_window(
      c, XCB_COPY_FROM_PARENT, window, s->root, 0, 0, window_width, window_height, 1,
      XCB_WINDOW_CLASS_INPUT_OUTPUT, s->root_visual, mask,
      values);
    window;
  });

  /* Find RandR provider, needed only for DRI3. */
  xcb_randr_provider_t provider = ({
    xcb_randr_get_providers_cookie_t cookie = xcb_randr_get_providers(c, window);
    xcb_generic_error_t* e;
    xcb_randr_get_providers_reply_t* reply = xcb_randr_get_providers_reply(c, cookie, &e);
    if (e) die("xcb_randr_get_providers %x", e->error_code);
    xcb_randr_provider_t* providers = xcb_randr_get_providers_providers(reply);
    xcb_randr_provider_t provider;
    if (reply->num_providers == 0) {
      fprintf(stderr, "No providers (probably quirk of Xwayland). Using 0, its usually ok\n");
      provider = 0;
    } else {
      provider = providers[0];
    }
    provider;
  });

  struct {
    int fd;
    const char* path;
    struct drm_version version;
    int chipset;

    int dmabuf_fd;
    uint32_t dmabuf_pixmap;
  } dri;

  /* Fetch fd for gpu via DRI3. */
  dri.fd = ({
    errno = 0;
    xcb_generic_error_t* e = 0;
    xcb_dri3_open_cookie_t cookie = xcb_dri3_open(c, window, provider);
    xcb_dri3_open_reply_t* reply = xcb_dri3_open_reply(c, cookie, &e);
    if (e) die("dri3 %x", e->error_code);
    int* fds = xcb_dri3_open_reply_fds(c, reply);
    fds[0];
  });

  dri.path = get_path_for_fd(dri.fd);

  /* Fetch information about the gpu. */
  dri.version = ({
    struct drm_version version = {0};
    ioctlQ(dri.fd, DRM_IOCTL_VERSION, &version);
    version.name = calloc(1, version.name_len + 1);
    version.desc = calloc(1, version.desc_len + 1);
    version.date = calloc(1, version.date_len + 1);
    ioctlQ(dri.fd, DRM_IOCTL_VERSION, &version);
    version;
  });

  /* All DRM_IOCTL_I915_*** calls are obviously driver specific. */
  if (strcmp(dri.version.name, "i915") != 0) {
    die("only i915 drivers are supported");
  }

  LOG("Fetch specific version of the gpu.");
  dri.chipset = ({
    int value;
    struct drm_i915_getparam param = {I915_PARAM_CHIPSET_ID, &value};
    ioctlQ(dri.fd, DRM_IOCTL_I915_GETPARAM, &param);
    value;
  });

  LOG("Allocate memory for 'texture' (afaiu, it's just generic memory).");
  i915surface_t screen = ({
    uint32_t width = window_width;
    uint32_t height = window_height;
    size_t size = width * height * sizeof(uint32_t);
    struct drm_i915_gem_create req = {.size = size};
    ioctlQ(dri.fd, DRM_IOCTL_I915_GEM_CREATE, &req);

    (i915surface_t){.gem = {.handle = req.handle, .delta = 0, .size = size}, .width = width, .height = height, .pitch = width * sizeof(uint32_t)};
  });

  LOG("Mark gem as render object."); {
    struct drm_i915_gem_set_domain req = {
      .handle = screen.gem.handle,
      .read_domains = I915_GEM_DOMAIN_WC,
      .write_domain = I915_GEM_DOMAIN_WC,
    };

    ioctlQ(dri.fd, DRM_IOCTL_I915_GEM_SET_DOMAIN, &req);
  }

  SECTION("Set tiling", {
    struct drm_i915_gem_set_tiling req = {
      .handle = screen.gem.handle,
      .tiling_mode = I915_TILING_NONE
    };

    ioctlQ(dri.fd, DRM_IOCTL_I915_GEM_SET_TILING, &req);
  });

  SECTION("Get tiling", {
    struct drm_i915_gem_get_tiling req = {.handle = screen.gem.handle};
    ioctlQ(dri.fd, DRM_IOCTL_I915_GEM_GET_TILING, &req);
  });

  LOG("Get fd for interop with x11 from 'texture'.");
  dri.dmabuf_fd = ({
    struct drm_prime_handle req = {.handle = screen.gem.handle};
    ioctlQ(dri.fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &req);
    req.fd;
  });

  LOG("Create context");
  const uint32_t context_id = ({
    struct drm_i915_gem_context_create req = {0};
    ioctlQ(dri.fd, DRM_IOCTL_I915_GEM_CONTEXT_CREATE, &req);
    req.ctx_id;
  });

  LOG("Send fd of 'texture' to x11 (via `man 3 cmsg`).");
  dri.dmabuf_pixmap = ({
    const uint32_t pixmap = xcb_generate_id(c);
    xcb_dri3_pixmap_from_buffer(c,
      pixmap, window,
      screen.gem.size, screen.width, screen.height,
      screen.pitch, /*depth*/24, /*bpp*/32,
      dri.dmabuf_fd
    );

    pixmap;
  });

  const uint64_t ppgt = ({
    uint64_t value = 0;
    struct drm_i915_getparam req =  {
      .param = I915_PARAM_HAS_ALIASING_PPGTT,
      .value = (void*)&value
    };
    ioctlQ(dri.fd, DRM_IOCTL_I915_GETPARAM, &req);
    value;
  });

  /*
    glxdemo does this:
    - I915_PARAM_CHIPSET_ID
    - I915_PARAM_MMAP_VERSION
    - I915_PARAM_HAS_EXEC_SOFTPIN
    - I915_PARAM_HAS_ALIASING_PPGTT
    - I915_PARAM_HAS_EXEC_NO_RELOC
    - I915_PARAM_MMAP_GTT_VERSION
  */

  /* Printing ioctl numbers is easier than solving #define rebuses. Useful for strace and gdb. */ if (1) {
    printf("ioctls\n");
    #define X(Op) printf("\t" #Op " = 0x%lx\n", Op);
      X(DRM_IOCTL_VERSION);
      X(DRM_IOCTL_I915_GETPARAM);
      X(DRM_IOCTL_I915_GEM_SET_DOMAIN);
      X(DRM_IOCTL_I915_GEM_SET_TILING);
      X(DRM_IOCTL_I915_REG_READ);
    #undef X
  }

  printf("DRI\n");
  printf("\tfd = %d\n", dri.fd);
  printf("\tpath = %s\n", dri.path);
  printf("Driver\n");
  printf("\tname = %.*s\n", (int)dri.version.name_len, dri.version.name);
  printf("\tdesc = %.*s\n", (int)dri.version.desc_len, dri.version.desc);
  printf("\tdate = %.*s\n", (int)dri.version.date_len, dri.version.date);
  printf("\tversion = %d.%d.%d\n", dri.version.version_major, dri.version.version_minor, dri.version.version_patchlevel);
  printf("\tchipset = 0x%x\n", dri.chipset);
  printf("\tppGTT = %s\n", ppgt == I915_GEM_PPGTT_NONE ? "none" : ppgt == I915_GEM_PPGTT_ALIASING ? "aliasing" : ppgt == I915_GEM_PPGTT_FULL ? "full" : "unknown");
  printf("dmabuf\n");
  printf("\tfd = %d\n", dri.dmabuf_fd);
  printf("screen gem\n");
  printf("\thandle = %u\n", screen.gem.handle);
  printf("\tsize = %lu\n", screen.gem.size);
  printf("\n");

  const i915surface_t tux = SECTION("Load tux.ppm", load_ppm("./tux.ppm", dri.fd));

  LOG("Creating command buffer");
  const uint64_t command_buffer_size = 4096;
  const uint32_t command_buffer_handle = ({
    struct drm_i915_gem_create req = {.size = command_buffer_size};
    ioctlQ(dri.fd, DRM_IOCTL_I915_GEM_CREATE, &req);

    struct drm_i915_gem_set_domain dreq = {.handle = req.handle, .read_domains = I915_GEM_DOMAIN_CPU};
    ioctlQ(dri.fd, DRM_IOCTL_I915_GEM_SET_DOMAIN, &dreq);

    req.handle;
  });

  i915cmd_buf_t buf = {};
  buf.data = buf.start = malloc(1024);

  i915cmd_MI_STORE_DATA_IMM_uint32(&buf, i915gem_with_delta(screen.gem, (screen.width *  5 + 64) * sizeof(uint32_t)), 0x00ffffff);
  i915cmd_MI_STORE_DATA_IMM_uint64(&buf, i915gem_with_delta(screen.gem, (screen.width *  8 + 64) * sizeof(uint32_t)), 0x00ffffff'0000ff00);
  i915cmd_MI_STORE_DATA_IMM_uint32(&buf, i915gem_with_delta(screen.gem, (screen.width * 11 + 64) * sizeof(uint32_t)), 0x00ffffff);
  i915cmd_MI_STORE_DATA_IMM(&buf, i915gem_with_delta(screen.gem, (screen.width * 14 + 64) * sizeof(uint32_t)), 8*4, (uint32_t[]){
    0x00ff0000, 0x00ff0000, 0x00ff0000, 0x00ff0000,  0x00ff0000, 0x00ff0000, 0x00ff0000, 0x00ff0000,
    0x000000ff, 0x000000ff, 0x000000ff, 0x000000ff,  0x000000ff, 0x000000ff, 0x000000ff, 0x000000ff,
    0x0000ff00, 0x0000ff00, 0x0000ff00, 0x0000ff00,  0x0000ff00, 0x0000ff00, 0x0000ff00, 0x0000ff00,
    0x00ff0000, 0x00ff0000, 0x00ff0000, 0x00ff0000,  0x00ff0000, 0x00ff0000, 0x00ff0000, 0x00ff0000,
  });
  i915cmd_XY_COLOR_BLT(&buf, screen, (u32rect_t){.top = 0, .left = 0, .bottom = 0x20, .right = 0x20}, 0x00ff0000);
  i915cmd_XY_FAST_COPY_BLT(&buf, screen, (u32rect_t){.top = 0, .left = screen.width - tux.width, .bottom = tux.height, .right = screen.width}, tux, (u32point_t){0, 0});
  i915cmd_MI_BATCH_BUFFER_END(&buf);

  SECTION("Writing commands to buffer", {
    struct drm_i915_gem_mmap req = {.handle = command_buffer_handle, .size = command_buffer_size};
    ioctlQ(dri.fd, DRM_IOCTL_I915_GEM_MMAP, &req);

    memcpy((void*)req.addr_ptr, buf.start, (buf.data - buf.start) * sizeof(buf.start[0]));
    munmap((void*)req.addr_ptr, command_buffer_size);
  });

  SECTION("Executing command buffer", {
    const uint32_t dummy_gem = ({
      struct drm_i915_gem_create req = {.size = 4096};
      ioctlQ(dri.fd, DRM_IOCTL_I915_GEM_CREATE, &req);
      req.handle;
    });

    struct drm_i915_gem_exec_object2 exec_objects[] = {
      // EMPERICAL: for some reason relocations doesn't work on the very first object, prepend dummy gem as a workaround.
      {.handle = dummy_gem,         .flags = EXEC_OBJECT_SUPPORTS_48B_ADDRESS},
      {.handle = tux.gem.handle,    .flags = EXEC_OBJECT_SUPPORTS_48B_ADDRESS},
      {.handle = screen.gem.handle, .flags = EXEC_OBJECT_SUPPORTS_48B_ADDRESS},
      // EMPERICAL: Command Buffer GEM must be the last one.
      {
        .handle = command_buffer_handle,
        .relocs_ptr = (uintptr_t)buf.relocations.values,
        .relocation_count = buf.relocations.size,
        .flags = 0
          | EXEC_OBJECT_SUPPORTS_48B_ADDRESS
          | EXEC_OBJECT_CAPTURE
      },
    };

    struct drm_i915_gem_execbuffer2 req = {
      .buffers_ptr = (uintptr_t)exec_objects,
      .buffer_count = sizeof(exec_objects) / sizeof(exec_objects[0]),
      .batch_start_offset = 0,
      .batch_len = (buf.data - buf.start) * sizeof(buf.start[0]),
      .flags = 0
        | I915_EXEC_BLT
        | I915_EXEC_DEFAULT
        | I915_EXEC_NO_RELOC
    };

    i915_execbuffer2_set_context_id(req, context_id);

    ioctlQ(dri.fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &req);
  });

  /* Create graphics context. */
  xcb_gcontext_t gc = ({
    const uint32_t mask = XCB_GC_FOREGROUND | XCB_GC_GRAPHICS_EXPOSURES;
    uint32_t values[2];
    values[0] = 0x000000; /* rgb */
    values[1] = 0;

    xcb_gcontext_t g = xcb_generate_id(c);
    xcb_create_gc(c, g, window, mask, values);
    g;
  });

  /* Show window. */
  xcb_map_window(c, window);
  xcb_flush(c);

  /* Main event loop. */
  bool done = false;
  xcb_generic_event_t* e;
  while (!done && (e = xcb_wait_for_event(c))) {
    switch (e->response_type) {
      case XCB_EXPOSE: {
        LOG("xcb_copy_area & xcb_flush");
        /* Tell X11 server to draw "texture". */
        xcb_copy_area(c, dri.dmabuf_pixmap, window, gc, 0, 0, 0, 0, screen.width, screen.height);
        xcb_flush(c);
        break;
      }
      /* Exit on escape. */
      case XCB_KEY_PRESS: {
        xcb_key_press_event_t* ke = (void*)e;

        if (ke->detail == 0x09) {
          done = 1;
        }
        break;
      }
    }

    free(e);
  }

  /* Close connection to X server. */
  xcb_disconnect(c);

  return 0;
}
