#include "mongoose.h"
#include "capture.h"
#include "routes.h"
#include "status.h"
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ------------------------------------------------------------------ */
/* Globals                                                              */
/* ------------------------------------------------------------------ */
static volatile sig_atomic_t s_shutdown = 0;
static app_ctx_t              s_app;
static capture_ctx_t          s_cap;

/* ------------------------------------------------------------------ */
/* Signal handler                                                       */
/* ------------------------------------------------------------------ */
static void signal_handler(int sig)
{
    (void)sig;
    s_shutdown = 1;
}

/* ------------------------------------------------------------------ */
/* Frame timer callback (30 fps capture, ~3 fps WS broadcast)           */
/* ------------------------------------------------------------------ */
static int s_frame_tick = 0;

static void frame_timer(void *arg)
{
    struct mg_mgr *mgr = (struct mg_mgr *)arg;

    if (!s_app.capture) return;
    if (s_app.ws_client_count <= 0 && !s_app.recording) return;

    const uint8_t *jpg = NULL;
    size_t len = 0;
    if (capture_grab(s_app.capture, &jpg, &len) != 0 || jpg == NULL) return;

    s_frame_tick++;

    /* Recording: save every frame at 30 fps */
    if (s_app.recording) {
        routes_record_frame(jpg, len);
    }

    /* WS broadcast: throttle to ~3 fps (every 10th frame) */
    if (s_app.ws_client_count > 0 && (s_frame_tick % 10) == 0) {
        routes_broadcast_frame(mgr, jpg, len);
    }
}

/* ------------------------------------------------------------------ */
/* Usage                                                                */
/* ------------------------------------------------------------------ */
static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [options]\n"
        "  -p PORT         TCP port to listen on (default: 8080)\n"
        "  -d DEVICE       V4L2 device (default: /dev/video0)\n"
        "  -c CAPTURES_DIR Directory to store captured JPEGs (default: captures)\n"
        "  -s STATIC_DIR   Directory for static web files (default: www)\n"
        "  -h              Show this help\n",
        prog);
}

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */
int main(int argc, char *argv[])
{
    /* Defaults */
    int         port         = 8080;
    const char *device       = "/dev/video0";
    const char *captures_dir = "captures";
    const char *static_dir   = "static";

    int opt;
    while ((opt = getopt(argc, argv, "p:d:c:s:h")) != -1) {
        switch (opt) {
        case 'p': port         = atoi(optarg); break;
        case 'd': device       = optarg;       break;
        case 'c': captures_dir = optarg;       break;
        case 's': static_dir   = optarg;       break;
        case 'h': usage(argv[0]); return 0;
        default:  usage(argv[0]); return 1;
        }
    }

    /* Ensure captures dir exists */
    mkdir(captures_dir, 0755);

    /* Signals */
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    /* Camera — warn but continue so the server stays useful without HW */
    memset(&s_cap, 0, sizeof(s_cap));
    s_cap.fd = -1;
    if (capture_open(&s_cap, device,
                     CAPTURE_DEFAULT_WIDTH, CAPTURE_DEFAULT_HEIGHT) != 0) {
        fprintf(stderr, "Warning: could not open capture device %s"
                " — live feed disabled\n", device);
        s_app.capture = NULL;
    } else {
        s_app.capture = &s_cap;
    }

    /* Status module */
    status_init(device);

    /* Routes */
    routes_init(&s_app, s_app.capture, static_dir, captures_dir);

    /* Mongoose */
    struct mg_mgr mgr;
    mg_mgr_init(&mgr);

    char listen_url[64];
    snprintf(listen_url, sizeof(listen_url), "http://0.0.0.0:%d", port);

    struct mg_connection *lc = mg_http_listen(&mgr, listen_url,
                                               routes_handle, NULL);
    if (!lc) {
        fprintf(stderr, "Failed to bind on %s\n", listen_url);
        mg_mgr_free(&mgr);
        capture_close(&s_cap);
        return 1;
    }

    /* Frame timer — 30 fps capture, WS broadcast throttled to ~3 fps */
    mg_timer_add(&mgr, 33, MG_TIMER_REPEAT, frame_timer, &mgr);

    printf("CRT Monitor listening on %s\n", listen_url);
    printf("  device      : %s\n", device);
    printf("  captures_dir: %s\n", captures_dir);
    printf("  static_dir  : %s\n", static_dir);

    /* Main loop */
    while (!s_shutdown) {
        mg_mgr_poll(&mgr, 50);
    }

    /* Cleanup */
    capture_close(&s_cap);
    mg_mgr_free(&mgr);
    printf("Shutdown.\n");
    return 0;
}
