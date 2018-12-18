/* libc */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

/* POSIX */
#include <sys/types.h>

/* X11 */
#define XK_LATIN1
#include <X11/keysymdef.h>

/* XCB */
#include <xcb/xcb.h>

int main(int argc, char **argv) {
  /* Open connection with the server. */
  xcb_connection_t *const c = xcb_connect(NULL, NULL);
  if (xcb_connection_has_error(c) > 0) {
    printf("Cannot open display\n");
    return 1;
  }

  xcb_screen_t* const s = xcb_setup_roots_iterator(xcb_get_setup(c)).data;

  /* Create window. */
  xcb_window_t w = ({
    const uint32_t mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
    uint32_t values[2];
    values[0] = s->white_pixel;
    values[1] = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS;

    xcb_window_t w = xcb_generate_id(c);
    xcb_create_window(
      c, XCB_COPY_FROM_PARENT, w, s->root, 10, 10, 100, 100, 1,
      XCB_WINDOW_CLASS_INPUT_OUTPUT, s->root_visual, mask,
      values);

    w;
  });

  /* Create graphics context. */
  xcb_gcontext_t g = ({
    const uint32_t mask = XCB_GC_FOREGROUND | XCB_GC_GRAPHICS_EXPOSURES;
    uint32_t values[2];
    values[0] = 0x000000; // rgb
    values[1] = 0;

    xcb_gcontext_t g = xcb_generate_id(c);
    xcb_create_gc(c, g, w, mask, values);
    g;
  });

  /* map (show) the window */
  xcb_map_window(c, w);

  xcb_flush(c);

  xcb_rectangle_t r = {20, 20, 60, 60};

  /* event loop */
  bool done = false;
  xcb_generic_event_t* e;
  while (!done && (e = xcb_wait_for_event(c))) {
    switch (e->response_type) {
      /* Redraw the window. */
      case XCB_EXPOSE: {
        printf("EXPOSE\n");
        xcb_poly_fill_rectangle(c, w, g, 1, &r);
        xcb_flush(c);
        break;
      }

      /* Exit on escape. */
      case XCB_KEY_PRESS: {
        xcb_key_press_event_t* ke = (void*)e;
        printf("Keycode: %d\n", ke->detail);

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
