#define _POSIX_C_SOURCE 200809L

/* libc */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* POSIX */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <unistd.h>
#include <fcntl.h>

/* XCB. NO CHEATING: it's only for structs and constants, i.e. for X11 protocol.*/
#include <xcb/xproto.h>

#define XK_MISCELLANY
#include <X11/keysymdef.h>

typedef struct {
  size_t size;
  char data[];
} buffer;

typedef struct {
  uint16_t family;
  uint16_t address_length;
  char* address;
  uint16_t number_length;
  char* number;
  uint16_t name_length;
  char* name;
  uint16_t data_length;
  char* data;

  buffer* owning_buffer;
} _x_auth_t;

static buffer* buffer_new(size_t size);
static size_t _x_pad(size_t x) { return (4 - (x % 4)) % 4; }

static _x_auth_t _read_xauthority(char const* filename);
static int _x_connect(char const* display);
static xcb_setup_t _x_setup(int xfd, _x_auth_t xauth, xcb_window_t* root, xcb_visualid_t* visual);

static uint8_t pad[4];

int main() {
  _x_auth_t xauth = _read_xauthority(getenv("XAUTHORITY"));

  int xfd = _x_connect(getenv("DISPLAY"));
  fputs("x11 connection opened\n", stdout);

  xcb_window_t root;
  xcb_visualid_t visual;
  xcb_setup_t setup = _x_setup(xfd, xauth, &root, &visual);

  uint32_t id = 0;
  uint32_t wid = setup.resource_id_base + (id++ & setup.resource_id_mask);

  /* CreateWindow */ {
    xcb_create_window_request_t req;
    req.major_opcode = XCB_CREATE_WINDOW;
    req.depth = XCB_COPY_FROM_PARENT;
    req.x = 10;
    req.y = 10;
    req.width = 100;
    req.height = 100;
    req.border_width = 1;
    req.wid = wid;
    req.parent = root;
    req._class = XCB_WINDOW_CLASS_INPUT_OUTPUT;
    req.visual = visual;
    req.value_mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;

    req.length = 8 + __builtin_popcount(req.value_mask);

    uint32_t back_pixel = 0x00000088;
    uint32_t event_mask =
      XCB_EVENT_MASK_EXPOSURE |
      XCB_EVENT_MASK_KEY_PRESS |
      XCB_EVENT_MASK_KEYMAP_STATE;

    struct iovec iov[] = {
      { (void*)&req, sizeof(req) },
      { &back_pixel, sizeof(back_pixel) },
      { &event_mask, sizeof(event_mask) }
    };

    assert(writev(xfd, iov, 3) == iov[0].iov_len + iov[1].iov_len + iov[2].iov_len);
  }

  /* MapWindow */ {
    xcb_map_window_request_t req;
    req.major_opcode = XCB_MAP_WINDOW;
    req.pad0 = 0;
    req.window = wid;
    req.length = 2;

    assert(write(xfd, (void*)&req, sizeof(req)) == sizeof(req));
  }

  #define _X_KEYCODE_MIN 8
  #define _X_KEYCODE_MAX 256
  #define _X_KEYCODE_TOTAL (_X_KEYCODE_MAX - _X_KEYCODE_MIN)

  /* GetKeyboardMapping */ ({
    xcb_get_keyboard_mapping_request_t req = {0};
    req.major_opcode = XCB_GET_KEYBOARD_MAPPING;
    req.first_keycode = _X_KEYCODE_MIN;
    req.count = _X_KEYCODE_TOTAL;
    req.length = 2;

    struct iovec iov[] = {
      { (void*)&req, sizeof(req) },
      { pad, _x_pad(sizeof(req)) }
    };

    assert(writev(xfd, iov, 2) == iov[0].iov_len + iov[1].iov_len);
  });

  size_t keysyms_per_keycode;
  xcb_keysym_t* keysym_by_keycode = 0;

  bool done = false;
  while (!done) {
    uint8_t response_type;
    if (read(xfd, &response_type, sizeof(response_type)) == 0) {
      break;
    }

    switch (response_type) {
      case 1: /* Reply */ {
        xcb_get_keyboard_mapping_reply_t reply = { response_type };
        assert(read(xfd, (char*)&reply + 1, sizeof(reply) - 1) == sizeof(reply) - 1);

        keysyms_per_keycode = reply.keysyms_per_keycode;
        keysym_by_keycode = realloc(keysym_by_keycode, 4*reply.length);
        assert(read(xfd, keysym_by_keycode, 4*reply.length) == 4*reply.length);
        break;
      }
      case XCB_KEYMAP_NOTIFY: {
        xcb_keymap_notify_event_t e = { response_type };
        assert(read(xfd, (char*)&e + 1, sizeof(e) - 1) == sizeof(e) - 1);
        break;
      }
      case XCB_EXPOSE: {
        xcb_expose_event_t e = { response_type };
        assert(read(xfd, (char*)&e + 1, sizeof(e) - 1) == sizeof(e) - 1);

        printf("@Expose x=%d y=%d w=%d h=%d\n", e.x, e.y, e.width, e.height);
        break;
      }
      case XCB_KEY_PRESS: {
        xcb_key_press_event_t e = { response_type };
        assert(read(xfd, (char*)&e + 1, sizeof(e) - 1) == sizeof(e) - 1);

        const xcb_keysym_t keysym = keysym_by_keycode[(e.detail - _X_KEYCODE_MIN) * keysyms_per_keycode];
        printf("@KeyPress detail=%d keysym=%x x=%d y=%d\n", e.detail, keysym, e.event_x, e.event_y);

        if (keysym == XK_Escape) done = true;
        break;
      }
    }
  }

  shutdown(xfd, SHUT_RDWR);
  close(xfd);

  fputs("x11 connection closed\n", stdout);

  return 0;
}

static _x_auth_t _read_xauthority(char const* filename) {
  int fd = open(filename, O_RDONLY);
  assert(fd != -1);

  lseek(fd, 0, SEEK_END);
  long size = lseek(fd, 0, SEEK_CUR);
  lseek(fd, 0, SEEK_SET);

  buffer *buf = buffer_new(size);
  read(fd, buf->data, buf->size);

  close(fd);

  _x_auth_t xauth;
  char* p = buf->data;
  xauth.family = p[0] * 256 + p[1];
  p += 2;
  xauth.address_length = p[0] * 256 + p[1];
  p += 2;
  xauth.address = p;
  p += xauth.address_length;
  xauth.number_length = p[0] * 256 + p[1];
  p += 2;
  xauth.number = p;
  p += xauth.number_length;
  xauth.name_length = p[0] * 256 + p[1];
  p += 2;
  xauth.name = p;
  p += xauth.name_length;
  xauth.data_length = p[0] * 256 + p[1];
  p += 2;
  xauth.data = p;

  xauth.owning_buffer = buf;

  return xauth;
}

static int _x_connect(char const* display) {
  assert(display && display[0] == ':' && display[2] == '\0');

  char path[] = "/tmp/.X11-unix/X?";
  *strchr(path, '?') = display[1];

  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  assert(fd != -1);

  struct sockaddr_un addr;
  strcpy(addr.sun_path, path);
  addr.sun_family = AF_UNIX;

  int status = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
  assert(status != -1);

  return fd;
}

static xcb_setup_t _x_setup(int xfd, _x_auth_t xauth, xcb_window_t* root, xcb_visualid_t* visual) {
  struct iovec parts[5];
  struct iovec* part = &parts[0];

  xcb_setup_request_t req = {
    .byte_order = __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__ ? 'B' : 'l',
    .pad0 = 0,
    .protocol_major_version = X_PROTOCOL,
    .protocol_minor_version = X_PROTOCOL_REVISION,
    .authorization_protocol_name_len = xauth.name_length,
    .authorization_protocol_data_len = xauth.data_length,
    .pad1 = { 0, 0 }
  };

  part->iov_base = (void*)&req;
  part->iov_len = sizeof(req);
  part++;

  part->iov_base = xauth.name;
  part->iov_len = xauth.name_length;
  part++;

  part->iov_base = (void*)pad;
  part->iov_len = _x_pad(xauth.name_length);
  part++;

  part->iov_base = xauth.data;
  part->iov_len = xauth.data_length;
  part++;

  part->iov_base = (void*)pad;
  part->iov_len = _x_pad(xauth.data_length);
  part++;

  if (writev(xfd, parts, part - parts) == -1) {
    perror("writev");
    exit(1);
  }

  xcb_setup_t response;
  ssize_t b = read(xfd, (void*)&response, sizeof(response));
  if (b != sizeof(response)) {
    perror("read");
    exit(1);
  }
  if (response.status == 0) {
    fputs("error setup\n", stderr);
    exit(1);
  }
  if (response.status == 2) {
    fputs("authentication needed\n", stderr);
    exit(1);
  }

  const size_t vendor_size = response.vendor_len + _x_pad(response.vendor_len);
  char* vendor = malloc(vendor_size);
  assert(read(xfd, vendor, vendor_size) != -1);

  const size_t formats_size = sizeof(xcb_format_t) * response.pixmap_formats_len;
  xcb_format_t* formats = malloc(formats_size);
  assert(read(xfd, formats, formats_size) != -1);

  printf("@Setup (Supports=%d.%d Vendor=\"%.*s\")\n",
    response.protocol_major_version, response.protocol_minor_version,
    response.vendor_len, vendor);

  *root = 0;
  *visual = 0;

  for (int screen_ix = 0; screen_ix < response.roots_len; screen_ix++) {
    xcb_screen_t screen;
    assert(read(xfd, &screen, sizeof(screen)) != -1);

    if (!*root) *root = screen.root;

    for (int depth_ix = 0; depth_ix < screen.allowed_depths_len; depth_ix++) {
      xcb_depth_t depth;
      assert(read(xfd, &depth, sizeof(depth)) != -1);

      for (int visual_ix = 0; visual_ix < depth.visuals_len; visual_ix++) {
        xcb_visualtype_t visualtype;
        assert(read(xfd, &visualtype, sizeof(visualtype)) != -1);

        if (!*visual) *visual = visualtype.visual_id;
      }
    }
  }

  return response;
}

static buffer* buffer_new(size_t size) {
  buffer *buf = malloc(sizeof(buffer) + size);
  buf->size = size;
  return buf;
}
