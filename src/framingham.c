#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <math.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <xdg-shell-client-protocol.h>
#define G_LOG_USE_STRUCTURED
#include <glib.h>

const uint8_t format_width = 4;
const uint32_t initial_width = 200;
const uint32_t initial_height = 200;
const double offsets[4] = {0, 0.66666, 1.33333, 2};

static int loop_bool = 1;

struct buffer {
  int memfd;
  int pool_len;
  struct wl_shm_pool *pool;
  struct wl_buffer *wlbuff;
  uint8_t *data;
  int32_t width;
  int32_t height;
};

int buffer_resize(struct buffer *buffer, int32_t width, int32_t height) {
  if (buffer->width == width && buffer->height == height) {
    return 0;
  }
  int32_t new_len = width * height * format_width;
  if (buffer->pool_len >= new_len) {
    wl_buffer_destroy(buffer->wlbuff);
    buffer->wlbuff = wl_shm_pool_create_buffer(buffer->pool, 0,
                                               width, height, width * format_width,
                                               WL_SHM_FORMAT_XRGB8888);
    buffer->width = width; //TODO clean up code duplicates
    buffer->height = height;
    return 1;
  }
  else {
    wl_buffer_destroy(buffer->wlbuff);
    int trunc_result = ftruncate(buffer->memfd, new_len);
    if (trunc_result == -1) {
      g_error("Failed to resize memfd");
      return -1;
    }
    buffer->data = (uint8_t *) mremap((void *) buffer->data, buffer->pool_len, new_len, MREMAP_MAYMOVE);
    wl_shm_pool_resize(buffer->pool, new_len);
    buffer->pool_len = new_len;
    buffer->wlbuff = wl_shm_pool_create_buffer(buffer->pool, 0,
                                               width, height, width * format_width,
                                               WL_SHM_FORMAT_XRGB8888);
    buffer->width = width; //TODO clean up code duplicates
    buffer->height = height;
    return 2;
  }
}

struct state {
  struct wl_display *disp;
  struct wl_registry *reg;
  struct wl_compositor *comp;
  struct wl_shm *shm;
  struct xdg_wm_base *shell;
  struct buffer buffers[2];
  struct wl_surface *wlsurf;
  struct wl_callback *frame_callback;
  struct xdg_surface *xdgsurf;
  struct xdg_toplevel *toplevel;
  int next_buffer;
  int desired_width;
  int desired_height;
  uint32_t last_time;
  int gen_counter;
};

static void SIGINT_handler(int sig) {
  loop_bool = 0;
}

int memfd_new(int size) {
  int fd = memfd_create("buf", 0);
  if (fd < 0) {
    g_error("Could not create memefd :(");
    return fd;
  }

  int trunc_result = ftruncate(fd, size);
  if (trunc_result == -1) {
    g_error("Failed to resize memfd");
    return -1;
  }

  return fd;
}

static void pong(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial) {
  xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
  .ping = pong,
};

static void global_handler(void *data,
                           struct wl_registry *wl_registry,
                           uint32_t name,
                           const char *interface,
                           uint32_t version) {
  struct state *state = (struct state *) data;

  if (!strcmp(interface, wl_compositor_interface.name)) {
    state->comp = (struct wl_compositor *) wl_registry_bind(state->reg, name, &wl_compositor_interface, 5);
  }
  if (!strcmp(interface, wl_shm_interface.name)) {
    state->shm = (struct wl_shm *) wl_registry_bind(state->reg, name, &wl_shm_interface, 1);
  }
  if (!strcmp(interface, xdg_wm_base_interface.name)) {
    state->shell = (struct xdg_wm_base *) wl_registry_bind(state->reg, name, &xdg_wm_base_interface, 6);
    xdg_wm_base_add_listener(state->shell, &xdg_wm_base_listener, NULL);
  }
}

static void global_remove_handler(void *data,
                                  struct wl_registry *wl_registry,
                                  uint32_t name) {
  // TODO actually handle things?
  // maybe its not needed since we dont require anything fancy
  g_debug("Removed global: %u", name);
}

static const struct wl_registry_listener reg_listener = {
  .global = global_handler,
  .global_remove = global_remove_handler
};

static void new_frame(struct state *state, uint32_t time) {
  struct buffer *buff = &state->buffers[state->next_buffer];
  buffer_resize(buff, state->desired_width, state->desired_height); // FIXME no error checking

  for (int i = 0; i < buff->height; i++) {
    for (int j = 0; j < buff->width; j++) {
      for (int p = 0; p < format_width; p++) {
        double step = sin(time * 2 * 0.000976562 + i * 6 * 0.0078125 + M_PI * 4 * offsets[p] /* + j * 0.045 */);
        double scaled_step = (step + 1) / 2;
        unsigned char color = pow(scaled_step, 0.6) * 255;
        int offset = (i * format_width * buff->width) + (j * format_width) + p;
        *(buff->data + offset) = color;
      }
    }
  }
  wl_surface_attach(state->wlsurf, buff->wlbuff, 0, 0);
  state->next_buffer = state->next_buffer ^ 1;
  wl_surface_damage_buffer(state->wlsurf, 0, 0,
                           buff->width,
                           buff->height);
  wl_surface_commit(state->wlsurf);
}

static void surface_configure(void *data,
                              struct xdg_surface *xdg_surface,
                              uint32_t serial) {
  struct state *state = (struct state *) data;
  g_debug("got configure event: %d", serial);
  xdg_surface_ack_configure(xdg_surface, serial);
  new_frame(state, state->last_time);
  g_debug("acked configure: %d", serial);
}

static const struct xdg_surface_listener surface_handler = {
  .configure = surface_configure
};

static void toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel,
                               int32_t width,
                               int32_t height,
                               struct wl_array *states) {
  struct state *state = (struct state *) data;
  g_debug("configure event to width:%d, height:%d, and whatever", width, height);
  if (width != 0) {
    state->desired_width = width;
  }
  if (height != 0) {
    state->desired_height = height;
  }
}

static void toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel) {
  loop_bool = 0;
}

static void toplevel_configure_bounds(void *data, struct xdg_toplevel *xdg_toplevel,
                                      int32_t width,
                                      int32_t height) {
  g_debug("bounds event to width:%d, height:%d", width, height);
}

static void toplevel_wmcaps(void *data,
                            struct xdg_toplevel *xdg_toplevel,
                            struct wl_array *capabilities) {
  g_debug("got wmcaps");
}

static const struct xdg_toplevel_listener toplevel_handler = {
  .configure = toplevel_configure,
  .close = toplevel_close,
  .configure_bounds = toplevel_configure_bounds,
  .wm_capabilities = toplevel_wmcaps
};

static const struct wl_callback_listener render_listener;

static void frame_time(void *data, struct wl_callback *wl_callback, uint32_t callback_data) {
  struct state *state = (struct state *) data;
  state->last_time = callback_data;
  wl_callback_destroy(wl_callback);
  state->frame_callback = wl_surface_frame(state->wlsurf);
  wl_callback_add_listener(state->frame_callback, &render_listener, state);
  new_frame(state, callback_data);
}

static const struct wl_callback_listener render_listener = {
  .done = frame_time,
};

int main(int argc, char *argv[]) {
  struct state state = {
    .buffers = {
      {
        .memfd = -1,
        .pool_len = -1,
        .width = -1,
        .height = -1,
      },
      {
        .memfd = -1,
        .pool_len = -1,
        .width = -1,
        .height = -1,
      }
    },
    .next_buffer = 0,
    .desired_width = 200,
    .desired_height = 200,
    .last_time = 0,
    .gen_counter = 0,
  };

  signal(SIGINT, SIGINT_handler);

  state.disp = wl_display_connect(NULL);
  if (state.disp == NULL) {
    g_error("no disp");
    return 1;
  }

  state.reg = wl_display_get_registry(state.disp);
  wl_registry_add_listener(state.reg, &reg_listener, &state);
  wl_display_roundtrip(state.disp);

  int init_size = initial_width * initial_height * format_width;

  for (int b = 0; b < 2; b++) { // TODO convert to a single pool
                                // we might need to keep ~3 buffer sizes around
                                // keep track of the layout and move buffers left
                                // buffer swapping might make it fine
   struct buffer *buff = &state.buffers[b];

    buff->width = initial_width;
    buff->height = initial_height;

    buff->memfd = memfd_new(init_size);
    if (buff->memfd < 0) {
      g_error("memfd not created");
      goto destroy;
    }
    buff->pool_len = init_size;
    buff->pool = wl_shm_create_pool(state.shm, buff->memfd, buff->pool_len);
    buff->wlbuff = wl_shm_pool_create_buffer(buff->pool, 0,
                                              buff->height, buff->width,
                                              buff->width * format_width,
                                              WL_SHM_FORMAT_XRGB8888);
    buff->data = (uint8_t *) mmap(NULL, buff->pool_len, PROT_READ | PROT_WRITE, MAP_SHARED, buff->memfd, 0);
  }

  state.wlsurf = wl_compositor_create_surface(state.comp);
  state.xdgsurf = xdg_wm_base_get_xdg_surface(state.shell, state.wlsurf);
  xdg_surface_add_listener(state.xdgsurf, &surface_handler, &state);
  state.toplevel = xdg_surface_get_toplevel(state.xdgsurf);
  xdg_toplevel_add_listener(state.toplevel, &toplevel_handler, &state);
  xdg_toplevel_set_title(state.toplevel, "gay window");
  state.frame_callback = wl_surface_frame(state.wlsurf);
  wl_callback_add_listener(state.frame_callback, &render_listener, &state);
  new_frame(&state, 0);

  do {
    wl_display_dispatch(state.disp);
  } while (loop_bool);

 destroy: //FIXME actually destroy everything properly
  /* if (close(memfd) < 0) { */
  /*   g_error("Failure closing memfd"); */
  /* } */
  wl_registry_destroy(state.reg);
  wl_display_disconnect(state.disp);
  return 0;
}
