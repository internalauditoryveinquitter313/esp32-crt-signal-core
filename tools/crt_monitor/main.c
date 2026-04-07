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
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

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
    /* Note: NOT using SIGCHLD=SIG_IGN because upload handler needs waitpid().
     * Analyze handler's fire-and-forget children become zombies until server exits,
     * but that's acceptable for a dev tool. */

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

    /* Basic auth from environment (CRT_AUTH_USER / CRT_AUTH_PASS) */
    s_app.auth_user = getenv("CRT_AUTH_USER");
    s_app.auth_pass = getenv("CRT_AUTH_PASS");
    if (s_app.auth_user && s_app.auth_pass) {
        printf("  auth        : basic auth enabled (user=%s)\n", s_app.auth_user);
    } else {
        fprintf(stderr,
                "WARNING: No authentication configured!\n"
                "  Set CRT_AUTH_USER and CRT_AUTH_PASS env vars for basic auth.\n");
    }

    /* Serial device for image upload to ESP32 */
    s_app.serial_fd = -1;
    {
        const char *esp_serial = getenv("CRT_SERIAL");
        s_app.serial_device = esp_serial ? esp_serial : NULL;
        if (s_app.serial_device) {
            /* Open serial port with O_NONBLOCK to avoid blocking on DCD,
             * then immediately disable DTR/RTS via ioctl to prevent ESP32 reset. */
            int sfd = open(s_app.serial_device, O_RDWR | O_NOCTTY | O_NONBLOCK);
            if (sfd >= 0) {
                /* Immediately clear DTR/RTS to prevent ESP32 reset */
                int bits = TIOCM_DTR | TIOCM_RTS;
                ioctl(sfd, TIOCMBIC, &bits);

                /* Switch back to blocking mode */
                int fl = fcntl(sfd, F_GETFL);
                fcntl(sfd, F_SETFL, fl & ~O_NONBLOCK);

                struct termios tty;
                tcgetattr(sfd, &tty);
                cfsetispeed(&tty, B115200);
                cfsetospeed(&tty, B115200);
                tty.c_cflag = B115200 | CS8 | CLOCAL | CREAD;
                tty.c_cflag &= ~(tcflag_t) HUPCL;
                tty.c_iflag = 0;
                tty.c_oflag = 0;
                tty.c_lflag = 0;
                tty.c_cc[VMIN] = 0;
                tty.c_cc[VTIME] = 100;
                tcsetattr(sfd, TCSANOW, &tty);
                tcflush(sfd, TCIOFLUSH);
                s_app.serial_fd = sfd;
                printf("  serial      : %s (fd=%d, kept open)\n", s_app.serial_device, sfd);
            } else {
                fprintf(stderr, "Warning: could not open serial %s: %s\n",
                        s_app.serial_device, strerror(errno));
            }
        }
    }
    s_app.img2fb_path = getenv("CRT_IMG2FB");
    if (!s_app.img2fb_path) s_app.img2fb_path = "../img2fb.py";

    /* Status module */
    status_init(device);

    /* Routes */
    routes_init(&s_app, s_app.capture, static_dir, captures_dir);

    /* Mongoose */
    struct mg_mgr mgr;
    mg_mgr_init(&mgr);

    char listen_url[64];
    /* Bind to localhost only — Cloudflare tunnel connects locally.
     * No port is exposed externally. Use CRT_BIND=0.0.0.0 to override. */
    const char *bind_addr = getenv("CRT_BIND");
    if (!bind_addr) bind_addr = "127.0.0.1";
    snprintf(listen_url, sizeof(listen_url), "http://%s:%d", bind_addr, port);

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
