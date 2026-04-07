#ifndef ROUTES_H
#define ROUTES_H
#include "mongoose.h"
#include "capture.h"
#include <stdbool.h>

typedef struct {
    capture_ctx_t *capture;
    const char *static_dir;
    const char *captures_dir;
    const char *auth_user; /* Basic auth username (NULL = no auth) */
    const char *auth_pass; /* Basic auth password */
    const char *serial_device; /* ESP32 serial port for image upload */
    const char *img2fb_path; /* Path to img2fb.py */
    int serial_fd; /* Persistent serial fd (-1 = not open) */
    int ws_client_count;
    /* Recording state */
    bool recording;
    int  rec_frames_left;
    int  rec_frames_saved;
    char rec_dir[512];
} app_ctx_t;

void routes_init(app_ctx_t *app, capture_ctx_t *capture,
                 const char *static_dir, const char *captures_dir);
void routes_handle(struct mg_connection *c, int ev, void *ev_data);
void routes_broadcast_frame(struct mg_mgr *mgr, const uint8_t *jpg, size_t len);
void routes_record_frame(const uint8_t *jpg, size_t len);
#endif
