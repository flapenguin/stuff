#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
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

/* NOTE: xcb/xlib is optional, but surely make life easier */
#include <xcb/xcb.h>
#include <xcb/dri3.h>
#include <xcb/randr.h>

#define ioctlQ(fd, request, arg) ({ \
  int __ioctl0_ret = ioctl(fd, request, arg); \
  if (__ioctl0_ret == -1) perror("ioctl " #request " failed"); \
  __ioctl0_ret; \
})

static void die(const char* msg) { puts(msg); exit(1); }

static const int width = 640;
static const int height = 480;

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
      c, XCB_COPY_FROM_PARENT, window, s->root, 0, 0, width, height, 1,
      XCB_WINDOW_CLASS_INPUT_OUTPUT, s->root_visual, mask,
      values);
    window;
  });

  /* Find RandR provider, needed only for DRI3. */
  xcb_randr_provider_t provider = ({
    xcb_randr_get_providers_cookie_t cookie = xcb_randr_get_providers(c, window);
    xcb_generic_error_t* e;
    xcb_randr_get_providers_reply_t* reply = xcb_randr_get_providers_reply(c, cookie, &e);
    xcb_randr_provider_t* providers = xcb_randr_get_providers_providers(reply);
    size_t length = xcb_randr_get_providers_providers_length(reply);
    providers[0];
  });

  struct {
    int fd;
    const char* path;
    struct drm_version version;
    int chipset;

    size_t gem_size;
    uint32_t gem_handle;
    uint32_t* gem_mem;

    int dmabuf_fd;
    uint32_t dmabuf_pixmap;
  } dri;

  dri.gem_size = width * height * 4;

  /* Fetch fd for gpu via DRI3. */
  dri.fd = ({
    errno = 0;
    xcb_generic_error_t* e = 0;
    xcb_dri3_open_cookie_t cookie = xcb_dri3_open(c, window, provider);
    xcb_dri3_open_reply_t* reply = xcb_dri3_open_reply(c, cookie, &e);
    if (e) die("dri3");
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

  /* Fetch specific version of the gpu. */
  dri.chipset = ({
    int value;
    struct drm_i915_getparam param = {I915_PARAM_CHIPSET_ID, &value};
    ioctlQ(dri.fd, DRM_IOCTL_I915_GETPARAM, &param);
    value;
  });

  /* Allocate memory for "texture" (afaiu, it's just generic memory). */
  dri.gem_handle = ({
    struct drm_i915_gem_create req = {
      .size = dri.gem_size
    };

    ioctlQ(dri.fd, DRM_IOCTL_I915_GEM_CREATE, &req);
    req.handle;
  });

  /* Mmap "texture" */
  dri.gem_mem = ({
    struct drm_i915_gem_mmap req = {
      .handle = dri.gem_handle,
      .offset = 0,
      .size = dri.gem_size
    };

    ioctlQ(dri.fd, DRM_IOCTL_I915_GEM_MMAP, &req);

    (void*)req.addr_ptr;
  });

  /* Get fd for interop with x11 from "texture". */
  dri.dmabuf_fd = ({
    struct drm_prime_handle req = {
      .handle = dri.gem_handle
    };
    ioctlQ(dri.fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &req);
    req.fd;
  });

  /* Send fd of "texture" to x11 (via `man 3 cmsg`). */
  dri.dmabuf_pixmap = ({
    const uint32_t pixmap = xcb_generate_id(c);
    xcb_dri3_pixmap_from_buffer(c,
      pixmap, window,
      dri.gem_size, width, height,
      width*4, 24, 32,
      dri.dmabuf_fd
    );

    pixmap;
  });

  /* Printing ioctl numbers is easier than solving #define rebuses. Useful for strace and gdb. */ if (0) {
    printf("ioctls\n");
    #define X(Op) printf("\t" #Op " = 0x%lx\n", Op);
      X(DRM_IOCTL_VERSION);
      X(DRM_IOCTL_I915_GETPARAM);
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
  printf("dmabuf\n");
  printf("fd = %d\n", dri.dmabuf_fd);
  printf("\n");

  /* Dinally, draw actual graphics. */
  const float pi = acosf(-1);
  for (int x = 0; x < width; x++) {
    int y = sinf(x * 8.0f * pi / width) * height / 3 + height / 2;
    dri.gem_mem[x + y*width] = 0x00ff0000;
  }

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
        /* Tell X11 server to draw "texture". */
        xcb_copy_area(c, dri.dmabuf_pixmap, window, gc, 0, 0, 0, 0, width, height);
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
