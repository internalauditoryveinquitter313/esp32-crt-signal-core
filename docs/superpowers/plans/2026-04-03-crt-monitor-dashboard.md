# CRT Monitor Dashboard — MVP Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a retro-terminal-styled browser dashboard (Mongoose C backend + static HTML frontend) for live CRT monitoring, capture, and gallery browsing via Tailscale on Android.

**Architecture:** Single-threaded C server using Mongoose 7.21 event loop. V4L2 MJPEG capture pushed to browser via WebSocket binary frames. Static HTML/CSS/JS served from `static/` directory. Captures saved as timestamped JPGs in `captures/`.

**Tech Stack:** C11, Mongoose 7.21 (vendored), V4L2, MJPEG native, HTML/CSS/JS (vanilla, no frameworks)

---

## File Map

| File | Responsibility |
|------|----------------|
| `tools/crt_monitor/Makefile` | Build system |
| `tools/crt_monitor/main.c` | Entry point, CLI args, Mongoose event loop |
| `tools/crt_monitor/capture.h` | V4L2 capture API (open, grab, close) |
| `tools/crt_monitor/capture.c` | V4L2 MJPEG capture implementation |
| `tools/crt_monitor/routes.h` | Route handler declarations |
| `tools/crt_monitor/routes.c` | HTTP/WS route handlers (live, capture, gallery, status) |
| `tools/crt_monitor/status.h` | Status module API |
| `tools/crt_monitor/status.c` | Server/signal status (uptime, config, device info) |
| `tools/crt_monitor/mongoose.c` | Vendored Mongoose 7.21 |
| `tools/crt_monitor/mongoose.h` | Vendored Mongoose 7.21 header |
| `tools/crt_monitor/static/index.html` | SPA shell with tab navigation |
| `tools/crt_monitor/static/style.css` | Retro terminal theme |
| `tools/crt_monitor/static/app.js` | WebSocket client, gallery, capture button |

---

### Task 1: Scaffold and Vendor Mongoose

**Files:**
- Create: `tools/crt_monitor/Makefile`
- Create: `tools/crt_monitor/mongoose.c` (vendored)
- Create: `tools/crt_monitor/mongoose.h` (vendored)
- Create: `tools/crt_monitor/main.c`

- [ ] **Step 1: Create directory structure**

```bash
mkdir -p tools/crt_monitor/static tools/crt_monitor/captures
```

- [ ] **Step 2: Download Mongoose 7.21**

```bash
cd tools/crt_monitor
curl -sL https://raw.githubusercontent.com/cesanta/mongoose/7.21/mongoose.c -o mongoose.c
curl -sL https://raw.githubusercontent.com/cesanta/mongoose/7.21/mongoose.h -o mongoose.h
```

Verify: `head -5 mongoose.h` should show version 7.21.

- [ ] **Step 3: Write Makefile**

```makefile
CC = gcc
CFLAGS = -Wall -Wextra -Wno-unused-parameter -O2 -std=c11 -I. -DMG_ENABLE_LINES=0
LDFLAGS = -lpthread

SRCS = main.c capture.c routes.c status.c mongoose.c
TARGET = crt_monitor

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRCS) capture.h routes.h status.h
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)

clean:
	rm -f $(TARGET)
```

- [ ] **Step 4: Write minimal main.c (hello world server)**

```c
#include "mongoose.h"

#include <signal.h>
#include <stdio.h>

static volatile sig_atomic_t s_shutdown = 0;

static void signal_handler(int sig)
{
    (void)sig;
    s_shutdown = 1;
}

static void ev_handler(struct mg_connection *c, int ev, void *ev_data)
{
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;
        mg_http_reply(c, 200, "Content-Type: text/plain\r\n", "CRT Monitor OK\n");
    }
}

int main(int argc, char *argv[])
{
    struct mg_mgr mgr;
    const char *listen_url = "http://0.0.0.0:8080";

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    mg_mgr_init(&mgr);
    mg_http_listen(&mgr, listen_url, ev_handler, NULL);
    printf("CRT Monitor listening on %s\n", listen_url);

    while (!s_shutdown) {
        mg_mgr_poll(&mgr, 100);
    }

    mg_mgr_free(&mgr);
    printf("Shutdown.\n");
    return 0;
}
```

- [ ] **Step 5: Create stub files so it compiles**

`capture.h`:
```c
#ifndef CAPTURE_H
#define CAPTURE_H

#include <stddef.h>
#include <stdint.h>

int capture_open(const char *device, int width, int height);
int capture_grab(uint8_t **jpg_buf, size_t *jpg_len);
void capture_close(void);

#endif
```

`capture.c`:
```c
#include "capture.h"
#include <stdio.h>

int capture_open(const char *device, int width, int height)
{
    (void)device; (void)width; (void)height;
    fprintf(stderr, "[capture] stub: not implemented\n");
    return -1;
}

int capture_grab(uint8_t **jpg_buf, size_t *jpg_len)
{
    (void)jpg_buf; (void)jpg_len;
    return -1;
}

void capture_close(void) {}
```

`routes.h`:
```c
#ifndef ROUTES_H
#define ROUTES_H

#include "mongoose.h"

void routes_handle(struct mg_connection *c, int ev, void *ev_data);

#endif
```

`routes.c`:
```c
#include "routes.h"

void routes_handle(struct mg_connection *c, int ev, void *ev_data)
{
    (void)c; (void)ev; (void)ev_data;
}
```

`status.h`:
```c
#ifndef STATUS_H
#define STATUS_H

#include <stddef.h>

size_t status_to_json(char *buf, size_t buf_size);

#endif
```

`status.c`:
```c
#include "status.h"
#include <stdio.h>
#include <time.h>

static time_t s_start_time = 0;

size_t status_to_json(char *buf, size_t buf_size)
{
    if (s_start_time == 0) s_start_time = time(NULL);
    long uptime = (long)(time(NULL) - s_start_time);
    return (size_t)snprintf(buf, buf_size,
        "{\"video_standard\":\"NTSC\",\"color_enabled\":true,"
        "\"active_lines\":240,\"sample_rate_hz\":14318180,"
        "\"capture_device\":\"/dev/video0\","
        "\"capture_resolution\":\"1280x720\","
        "\"uptime_s\":%ld}", uptime);
}
```

- [ ] **Step 6: Build and test**

```bash
cd tools/crt_monitor && make
```

Expected: compiles with no errors. `./crt_monitor` prints `CRT Monitor listening on http://0.0.0.0:8080`.

Test: `curl http://localhost:8080/` returns `CRT Monitor OK`.

- [ ] **Step 7: Commit**

```bash
git add tools/crt_monitor/
git commit -m "feat(monitor): scaffold Mongoose server with stubs"
```

---

### Task 2: V4L2 MJPEG Capture Module

**Files:**
- Modify: `tools/crt_monitor/capture.h`
- Modify: `tools/crt_monitor/capture.c`

- [ ] **Step 1: Write capture.h with full API**

```c
#ifndef CAPTURE_H
#define CAPTURE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define CAPTURE_NUM_BUFFERS 4
#define CAPTURE_DEFAULT_WIDTH 1280
#define CAPTURE_DEFAULT_HEIGHT 720

typedef struct {
    void *start;
    size_t length;
} capture_buffer_t;

typedef struct {
    int fd;
    capture_buffer_t buffers[CAPTURE_NUM_BUFFERS];
    int buffer_count;
    bool streaming;
} capture_ctx_t;

/* Open device and start MJPEG streaming. Returns 0 on success. */
int capture_open(capture_ctx_t *ctx, const char *device, int width, int height);

/* Grab one MJPEG frame. Sets jpg_buf/jpg_len to point into mmap'd buffer.
   Caller must consume data before next grab call. Returns 0 on success. */
int capture_grab(capture_ctx_t *ctx, const uint8_t **jpg_buf, size_t *jpg_len);

/* Stop streaming and close device. */
void capture_close(capture_ctx_t *ctx);

#endif
```

- [ ] **Step 2: Write capture.c with V4L2 implementation**

```c
#include "capture.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <unistd.h>

static int xioctl(int fd, unsigned long request, void *arg)
{
    int r;
    do {
        r = ioctl(fd, request, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

int capture_open(capture_ctx_t *ctx, const char *device, int width, int height)
{
    struct v4l2_capability cap;
    struct v4l2_format fmt;
    struct v4l2_requestbuffers req;

    memset(ctx, 0, sizeof(*ctx));
    ctx->fd = -1;

    ctx->fd = open(device, O_RDWR | O_NONBLOCK);
    if (ctx->fd < 0) {
        fprintf(stderr, "[capture] open %s: %s\n", device, strerror(errno));
        return -1;
    }

    if (xioctl(ctx->fd, VIDIOC_QUERYCAP, &cap) < 0) {
        fprintf(stderr, "[capture] QUERYCAP: %s\n", strerror(errno));
        goto fail;
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf(stderr, "[capture] %s: not a capture device\n", device);
        goto fail;
    }

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = (unsigned)width;
    fmt.fmt.pix.height = (unsigned)height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (xioctl(ctx->fd, VIDIOC_S_FMT, &fmt) < 0) {
        fprintf(stderr, "[capture] S_FMT MJPEG: %s\n", strerror(errno));
        goto fail;
    }

    if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG) {
        fprintf(stderr, "[capture] device does not support MJPEG\n");
        goto fail;
    }

    printf("[capture] format: %ux%u MJPEG\n",
           fmt.fmt.pix.width, fmt.fmt.pix.height);

    memset(&req, 0, sizeof(req));
    req.count = CAPTURE_NUM_BUFFERS;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (xioctl(ctx->fd, VIDIOC_REQBUFS, &req) < 0) {
        fprintf(stderr, "[capture] REQBUFS: %s\n", strerror(errno));
        goto fail;
    }

    ctx->buffer_count = (int)req.count;

    for (int i = 0; i < ctx->buffer_count; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = (unsigned)i;

        if (xioctl(ctx->fd, VIDIOC_QUERYBUF, &buf) < 0) {
            fprintf(stderr, "[capture] QUERYBUF %d: %s\n", i, strerror(errno));
            goto fail;
        }

        ctx->buffers[i].length = buf.length;
        ctx->buffers[i].start = mmap(NULL, buf.length,
                                      PROT_READ | PROT_WRITE, MAP_SHARED,
                                      ctx->fd, buf.m.offset);
        if (ctx->buffers[i].start == MAP_FAILED) {
            fprintf(stderr, "[capture] mmap %d: %s\n", i, strerror(errno));
            goto fail;
        }

        if (xioctl(ctx->fd, VIDIOC_QBUF, &buf) < 0) {
            fprintf(stderr, "[capture] QBUF %d: %s\n", i, strerror(errno));
            goto fail;
        }
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(ctx->fd, VIDIOC_STREAMON, &type) < 0) {
        fprintf(stderr, "[capture] STREAMON: %s\n", strerror(errno));
        goto fail;
    }

    ctx->streaming = true;
    printf("[capture] streaming started on %s\n", device);
    return 0;

fail:
    if (ctx->fd >= 0) close(ctx->fd);
    ctx->fd = -1;
    return -1;
}

int capture_grab(capture_ctx_t *ctx, const uint8_t **jpg_buf, size_t *jpg_len)
{
    struct v4l2_buffer buf;

    if (!ctx->streaming) return -1;

    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (xioctl(ctx->fd, VIDIOC_DQBUF, &buf) < 0) {
        if (errno == EAGAIN) return -1; /* no frame ready */
        fprintf(stderr, "[capture] DQBUF: %s\n", strerror(errno));
        return -1;
    }

    *jpg_buf = (const uint8_t *)ctx->buffers[buf.index].start;
    *jpg_len = buf.bytesused;

    if (xioctl(ctx->fd, VIDIOC_QBUF, &buf) < 0) {
        fprintf(stderr, "[capture] re-QBUF: %s\n", strerror(errno));
    }

    return 0;
}

void capture_close(capture_ctx_t *ctx)
{
    if (ctx->streaming) {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        xioctl(ctx->fd, VIDIOC_STREAMOFF, &type);
        ctx->streaming = false;
    }

    for (int i = 0; i < ctx->buffer_count; i++) {
        if (ctx->buffers[i].start && ctx->buffers[i].start != MAP_FAILED) {
            munmap(ctx->buffers[i].start, ctx->buffers[i].length);
        }
    }

    if (ctx->fd >= 0) {
        close(ctx->fd);
        ctx->fd = -1;
    }

    printf("[capture] closed\n");
}
```

- [ ] **Step 3: Build to verify compilation**

```bash
cd tools/crt_monitor && make
```

Expected: compiles with no errors (capture module has no callers yet, but links).

- [ ] **Step 4: Commit**

```bash
git add tools/crt_monitor/capture.c tools/crt_monitor/capture.h
git commit -m "feat(monitor): V4L2 MJPEG capture module"
```

---

### Task 3: Route Handlers (HTTP + WebSocket)

**Files:**
- Modify: `tools/crt_monitor/routes.h`
- Modify: `tools/crt_monitor/routes.c`
- Modify: `tools/crt_monitor/main.c`

- [ ] **Step 1: Write routes.h**

```c
#ifndef ROUTES_H
#define ROUTES_H

#include "mongoose.h"
#include "capture.h"

typedef struct {
    capture_ctx_t *capture;
    const char *static_dir;
    const char *captures_dir;
    int ws_client_count;
} app_ctx_t;

void routes_init(app_ctx_t *app, capture_ctx_t *capture,
                 const char *static_dir, const char *captures_dir);

void routes_handle(struct mg_connection *c, int ev, void *ev_data);

/* Call from timer to push frames to WS clients */
void routes_broadcast_frame(struct mg_mgr *mgr, const uint8_t *jpg, size_t len);

#endif
```

- [ ] **Step 2: Write routes.c**

```c
#include "routes.h"
#include "status.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include <unistd.h>

static app_ctx_t *s_app = NULL;

void routes_init(app_ctx_t *app, capture_ctx_t *capture,
                 const char *static_dir, const char *captures_dir)
{
    memset(app, 0, sizeof(*app));
    app->capture = capture;
    app->static_dir = static_dir;
    app->captures_dir = captures_dir;
    s_app = app;
}

static void handle_ws_live(struct mg_connection *c, struct mg_http_message *hm)
{
    mg_ws_upgrade(c, hm, NULL);
    c->data[0] = 'W'; /* mark as WebSocket live client */
    s_app->ws_client_count++;
    printf("[ws] client connected (total: %d)\n", s_app->ws_client_count);
}

static void handle_api_capture(struct mg_connection *c)
{
    const uint8_t *jpg;
    size_t jpg_len;
    char filename[128];
    char filepath[256];
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);

    if (capture_grab(s_app->capture, &jpg, &jpg_len) != 0) {
        mg_http_reply(c, 500, "Content-Type: application/json\r\n",
                      "{\"ok\":false,\"error\":\"capture failed\"}");
        return;
    }

    snprintf(filename, sizeof(filename),
             "capture_%04d%02d%02d_%02d%02d%02d.jpg",
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
             tm->tm_hour, tm->tm_min, tm->tm_sec);
    snprintf(filepath, sizeof(filepath), "%s/%s",
             s_app->captures_dir, filename);

    FILE *f = fopen(filepath, "wb");
    if (!f) {
        mg_http_reply(c, 500, "Content-Type: application/json\r\n",
                      "{\"ok\":false,\"error\":\"write failed\"}");
        return;
    }
    fwrite(jpg, 1, jpg_len, f);
    fclose(f);

    mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                  "{\"ok\":true,\"name\":\"%s\",\"size\":%lu}",
                  filename, (unsigned long)jpg_len);
    printf("[capture] saved %s (%zu bytes)\n", filename, jpg_len);
}

static void handle_api_gallery(struct mg_connection *c)
{
    DIR *d = opendir(s_app->captures_dir);
    struct dirent *entry;
    struct stat st;
    char path[256];
    char json[8192];
    size_t off = 0;
    int count = 0;

    off += (size_t)snprintf(json + off, sizeof(json) - off, "[");

    if (d) {
        while ((entry = readdir(d)) != NULL) {
            if (entry->d_name[0] == '.') continue;
            const char *ext = strrchr(entry->d_name, '.');
            if (!ext || strcasecmp(ext, ".jpg") != 0) continue;

            snprintf(path, sizeof(path), "%s/%s",
                     s_app->captures_dir, entry->d_name);
            if (stat(path, &st) != 0) continue;

            if (count > 0) off += (size_t)snprintf(json + off, sizeof(json) - off, ",");
            off += (size_t)snprintf(json + off, sizeof(json) - off,
                "{\"name\":\"%s\",\"timestamp\":%ld,\"size\":%ld}",
                entry->d_name, (long)st.st_mtime, (long)st.st_size);
            count++;
        }
        closedir(d);
    }

    off += (size_t)snprintf(json + off, sizeof(json) - off, "]");

    mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s", json);
}

static void handle_api_gallery_delete(struct mg_connection *c,
                                       struct mg_http_message *hm)
{
    /* URI: /api/gallery/filename.jpg */
    struct mg_str name = mg_str_n(hm->uri.buf + 13, hm->uri.len - 13); /* skip "/api/gallery/" */
    char path[256];

    if (name.len == 0 || name.len > 128 || mg_strchr(name, '/') != NULL) {
        mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                      "{\"ok\":false,\"error\":\"bad name\"}");
        return;
    }

    snprintf(path, sizeof(path), "%s/%.*s",
             s_app->captures_dir, (int)name.len, name.buf);

    if (unlink(path) == 0) {
        mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                      "{\"ok\":true}");
    } else {
        mg_http_reply(c, 404, "Content-Type: application/json\r\n",
                      "{\"ok\":false,\"error\":\"not found\"}");
    }
}

static void handle_api_status(struct mg_connection *c)
{
    char json[512];
    status_to_json(json, sizeof(json));
    mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s", json);
}

void routes_handle(struct mg_connection *c, int ev, void *ev_data)
{
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;

        if (mg_match(hm->uri, mg_str("/ws/live"), NULL)) {
            handle_ws_live(c, hm);
        } else if (mg_match(hm->uri, mg_str("/api/capture"), NULL) &&
                   mg_strcmp(hm->method, mg_str("POST")) == 0) {
            handle_api_capture(c);
        } else if (mg_match(hm->uri, mg_str("/api/gallery"), NULL) &&
                   mg_strcmp(hm->method, mg_str("GET")) == 0) {
            handle_api_gallery(c);
        } else if (mg_match(hm->uri, mg_str("/api/gallery/*"), NULL) &&
                   mg_strcmp(hm->method, mg_str("DELETE")) == 0) {
            handle_api_gallery_delete(c, hm);
        } else if (mg_match(hm->uri, mg_str("/api/status"), NULL)) {
            handle_api_status(c);
        } else if (mg_match(hm->uri, mg_str("/captures/*"), NULL)) {
            struct mg_http_serve_opts opts = {.root_dir = s_app->captures_dir,
                                               .extra_headers = "Cache-Control: no-cache\r\n"};
            /* Strip /captures prefix: serve /captures/foo.jpg from captures_dir/foo.jpg */
            hm->uri.buf += 9;  /* skip "/captures" */
            hm->uri.len -= 9;
            mg_http_serve_dir(c, hm, &opts);
        } else {
            struct mg_http_serve_opts opts = {.root_dir = s_app->static_dir,
                                               .ssi_pattern = NULL};
            mg_http_serve_dir(c, hm, &opts);
        }
    } else if (ev == MG_EV_WS_OPEN) {
        /* WebSocket connected — nothing extra needed */
    } else if (ev == MG_EV_CLOSE) {
        if (c->data[0] == 'W') {
            s_app->ws_client_count--;
            printf("[ws] client disconnected (total: %d)\n", s_app->ws_client_count);
        }
    }
}

void routes_broadcast_frame(struct mg_mgr *mgr, const uint8_t *jpg, size_t len)
{
    struct mg_connection *c;
    for (c = mgr->conns; c != NULL; c = c->next) {
        if (c->data[0] == 'W') {
            mg_ws_send(c, (const char *)jpg, len, WEBSOCKET_OP_BINARY);
        }
    }
}
```

- [ ] **Step 3: Update main.c to wire everything together**

```c
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

static volatile sig_atomic_t s_shutdown = 0;

static void signal_handler(int sig)
{
    (void)sig;
    s_shutdown = 1;
}

static capture_ctx_t s_capture;
static app_ctx_t s_app;

static void frame_timer(void *arg)
{
    struct mg_mgr *mgr = (struct mg_mgr *)arg;
    const uint8_t *jpg;
    size_t jpg_len;

    if (s_app.ws_client_count == 0) return;

    if (capture_grab(&s_capture, &jpg, &jpg_len) == 0) {
        routes_broadcast_frame(mgr, jpg, jpg_len);
    }
}

static void usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [options]\n"
        "  -p PORT    Listen port (default: 8080)\n"
        "  -d DEVICE  V4L2 device (default: /dev/video0)\n"
        "  -c DIR     Captures directory (default: ./captures)\n"
        "  -s DIR     Static files directory (default: ./static)\n"
        "  -h         Show this help\n", prog);
}

int main(int argc, char *argv[])
{
    struct mg_mgr mgr;
    int port = 8080;
    const char *device = "/dev/video0";
    const char *captures_dir = "captures";
    const char *static_dir = "static";
    char listen_url[64];
    int opt;

    while ((opt = getopt(argc, argv, "p:d:c:s:h")) != -1) {
        switch (opt) {
        case 'p': port = atoi(optarg); break;
        case 'd': device = optarg; break;
        case 'c': captures_dir = optarg; break;
        case 's': static_dir = optarg; break;
        case 'h': usage(argv[0]); return 0;
        default:  usage(argv[0]); return 1;
        }
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    mkdir(captures_dir, 0755);

    printf("=== CRT Monitor Dashboard ===\n");
    printf("Device:   %s\n", device);
    printf("Static:   %s\n", static_dir);
    printf("Captures: %s\n", captures_dir);

    if (capture_open(&s_capture, device, CAPTURE_DEFAULT_WIDTH,
                     CAPTURE_DEFAULT_HEIGHT) != 0) {
        fprintf(stderr, "[main] WARNING: camera not available, live feed disabled\n");
    }

    routes_init(&s_app, &s_capture, static_dir, captures_dir);
    status_init(device);

    mg_mgr_init(&mgr);
    snprintf(listen_url, sizeof(listen_url), "http://0.0.0.0:%d", port);
    mg_http_listen(&mgr, listen_url, routes_handle, NULL);
    printf("Listening: %s\n\n", listen_url);

    mg_timer_add(&mgr, 333, MG_TIMER_REPEAT, frame_timer, &mgr); /* ~3fps */

    while (!s_shutdown) {
        mg_mgr_poll(&mgr, 50);
    }

    capture_close(&s_capture);
    mg_mgr_free(&mgr);
    printf("\nShutdown complete.\n");
    return 0;
}
```

- [ ] **Step 4: Update status.c/h with init function**

`status.h`:
```c
#ifndef STATUS_H
#define STATUS_H

#include <stddef.h>

void status_init(const char *device);
size_t status_to_json(char *buf, size_t buf_size);

#endif
```

`status.c`:
```c
#include "status.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static time_t s_start_time = 0;
static const char *s_device = "/dev/video0";

void status_init(const char *device)
{
    s_start_time = time(NULL);
    s_device = device;
}

size_t status_to_json(char *buf, size_t buf_size)
{
    long uptime = (long)(time(NULL) - s_start_time);
    return (size_t)snprintf(buf, buf_size,
        "{\"video_standard\":\"NTSC\",\"color_enabled\":true,"
        "\"active_lines\":240,\"sample_rate_hz\":14318180,"
        "\"capture_device\":\"%s\","
        "\"capture_resolution\":\"1280x720\","
        "\"uptime_s\":%ld}", s_device, uptime);
}
```

- [ ] **Step 5: Build and test endpoints**

```bash
cd tools/crt_monitor && make
```

Expected: compiles. Then run and test:

```bash
./crt_monitor -p 9999 &
curl http://localhost:9999/api/status
# Expected: {"video_standard":"NTSC","color_enabled":true,...}

curl -X POST http://localhost:9999/api/capture
# Expected: {"ok":false,"error":"capture failed"} (no camera in test)

curl http://localhost:9999/api/gallery
# Expected: []

kill %1
```

- [ ] **Step 6: Commit**

```bash
git add tools/crt_monitor/routes.c tools/crt_monitor/routes.h \
        tools/crt_monitor/main.c tools/crt_monitor/status.c tools/crt_monitor/status.h
git commit -m "feat(monitor): HTTP routes and WebSocket live feed"
```

---

### Task 4: Retro Terminal Frontend — HTML + CSS

**Files:**
- Create: `tools/crt_monitor/static/index.html`
- Create: `tools/crt_monitor/static/style.css`

- [ ] **Step 1: Write index.html**

```html
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>CRT Monitor</title>
<link rel="stylesheet" href="/style.css">
</head>
<body>
<div id="app">
  <div id="tab-content">
    <!-- LIVE TAB -->
    <div class="tab-pane active" data-tab="live">
      <div class="feed-container">
        <img id="live-img" alt="live feed">
        <div id="feed-overlay" class="feed-overlay">NO SIGNAL</div>
      </div>
      <button id="btn-capture" class="btn-capture">[ CAPTURE ]</button>
    </div>

    <!-- GALLERY TAB -->
    <div class="tab-pane" data-tab="gallery">
      <div id="gallery-grid" class="gallery-grid"></div>
      <div id="lightbox" class="lightbox hidden">
        <img id="lightbox-img">
        <div class="lightbox-controls">
          <button id="lb-prev" class="lb-btn">&lt;</button>
          <span id="lb-name"></span>
          <button id="lb-next" class="lb-btn">&gt;</button>
        </div>
        <button id="lb-close" class="lb-btn lb-close">X</button>
        <button id="lb-delete" class="lb-btn lb-delete">DEL</button>
      </div>
    </div>

    <!-- STATUS TAB -->
    <div class="tab-pane" data-tab="status">
      <pre id="status-readout" class="status-readout">Initializing...<span class="cursor">_</span></pre>
    </div>
  </div>

  <nav id="tab-bar" class="tab-bar">
    <button class="tab active" data-tab="live">LIVE</button>
    <button class="tab" data-tab="gallery">GALLERY</button>
    <button class="tab" data-tab="status">STATUS</button>
  </nav>

  <div class="status-line">
    <span id="ws-status">DISCONNECTED</span>
    <span id="frame-counter">0 frames</span>
  </div>
</div>
<script src="/app.js"></script>
</body>
</html>
```

- [ ] **Step 2: Write style.css**

```css
:root {
  --bg: #0a0a0a;
  --green: #00ff41;
  --green-dim: #0a6e2a;
  --green-glow: rgba(0, 255, 65, 0.4);
  --cyan: #00ffcc;
  --red: #ff3333;
  --border: rgba(0, 255, 51, 0.2);
}

* { margin: 0; padding: 0; box-sizing: border-box; }

body {
  background: var(--bg);
  color: var(--green);
  font-family: "Fira Code", "JetBrains Mono", "Cascadia Code", monospace;
  font-size: 13px;
  overflow: hidden;
  height: 100dvh;
  /* scanline overlay */
  background-image: repeating-linear-gradient(
    0deg,
    transparent,
    transparent 2px,
    rgba(0, 255, 65, 0.015) 2px,
    rgba(0, 255, 65, 0.015) 4px
  );
}

#app {
  display: flex;
  flex-direction: column;
  height: 100dvh;
}

/* TABS */
.tab-bar {
  display: flex;
  border-top: 1px solid var(--border);
  background: #050505;
  flex-shrink: 0;
}

.tab {
  flex: 1;
  padding: 12px 0;
  background: none;
  border: none;
  color: var(--green-dim);
  font-family: inherit;
  font-size: 13px;
  font-weight: bold;
  cursor: pointer;
  text-shadow: none;
  min-height: 44px;
}

.tab.active {
  color: var(--green);
  text-shadow: 0 0 8px var(--green-glow);
  border-top: 2px solid var(--green);
}

/* TAB CONTENT */
#tab-content {
  flex: 1;
  overflow: hidden;
  position: relative;
}

.tab-pane {
  display: none;
  height: 100%;
  overflow-y: auto;
}

.tab-pane.active {
  display: flex;
  flex-direction: column;
}

/* LIVE FEED */
.feed-container {
  flex: 1;
  display: flex;
  align-items: center;
  justify-content: center;
  background: #000;
  position: relative;
  min-height: 0;
}

.feed-container img {
  max-width: 100%;
  max-height: 100%;
  object-fit: contain;
}

.feed-overlay {
  position: absolute;
  top: 50%;
  left: 50%;
  transform: translate(-50%, -50%);
  font-size: 24px;
  color: var(--green-dim);
  text-shadow: 0 0 16px var(--green-glow);
  animation: blink 1s step-end infinite;
}

.feed-overlay.hidden { display: none; }

.btn-capture {
  display: block;
  width: 100%;
  padding: 14px;
  background: #0a1a0a;
  border: 1px solid var(--green-dim);
  color: var(--green);
  font-family: inherit;
  font-size: 16px;
  font-weight: bold;
  cursor: pointer;
  text-shadow: 0 0 8px var(--green-glow);
  min-height: 48px;
  flex-shrink: 0;
}

.btn-capture:active {
  background: var(--green);
  color: var(--bg);
}

.btn-capture.flash {
  background: var(--green);
  color: var(--bg);
  transition: background 0.1s;
}

/* GALLERY */
.gallery-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(120px, 1fr));
  gap: 4px;
  padding: 4px;
}

.gallery-grid img {
  width: 100%;
  aspect-ratio: 16/9;
  object-fit: cover;
  border: 1px solid var(--border);
  cursor: pointer;
}

.gallery-grid img:hover {
  border-color: var(--green);
}

/* LIGHTBOX */
.lightbox {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.95);
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  z-index: 100;
}

.lightbox.hidden { display: none; }

.lightbox img {
  max-width: 100%;
  max-height: 80vh;
  object-fit: contain;
}

.lightbox-controls {
  display: flex;
  align-items: center;
  gap: 16px;
  padding: 12px;
  color: var(--green);
  font-size: 12px;
}

.lb-btn {
  background: none;
  border: 1px solid var(--green-dim);
  color: var(--green);
  font-family: inherit;
  font-size: 14px;
  padding: 8px 16px;
  cursor: pointer;
  min-width: 44px;
  min-height: 44px;
}

.lb-close {
  position: absolute;
  top: 8px;
  right: 8px;
}

.lb-delete {
  position: absolute;
  top: 8px;
  left: 8px;
  color: var(--red);
  border-color: var(--red);
}

/* STATUS */
.status-readout {
  padding: 16px;
  line-height: 1.8;
  white-space: pre-wrap;
  text-shadow: 0 0 6px var(--green-glow);
  flex: 1;
}

/* STATUS LINE (bottom bar) */
.status-line {
  display: flex;
  justify-content: space-between;
  padding: 4px 8px;
  font-size: 10px;
  color: var(--green-dim);
  border-top: 1px solid var(--border);
  background: #050505;
  flex-shrink: 0;
}

/* ANIMATIONS */
@keyframes blink {
  50% { opacity: 0; }
}

.cursor {
  animation: blink 1s step-end infinite;
}
```

- [ ] **Step 3: Test static serving**

```bash
cd tools/crt_monitor && ./crt_monitor -p 9999 &
curl -s http://localhost:9999/ | head -5
# Expected: <!DOCTYPE html>...
kill %1
```

- [ ] **Step 4: Commit**

```bash
git add tools/crt_monitor/static/index.html tools/crt_monitor/static/style.css
git commit -m "feat(monitor): retro terminal HTML/CSS frontend"
```

---

### Task 5: Frontend JavaScript — WebSocket + Gallery + Capture

**Files:**
- Create: `tools/crt_monitor/static/app.js`

- [ ] **Step 1: Write app.js**

```javascript
(function () {
  "use strict";

  /* --- Tab navigation --- */
  const tabs = document.querySelectorAll(".tab");
  const panes = document.querySelectorAll(".tab-pane");

  function switchTab(name) {
    tabs.forEach(function (t) {
      t.classList.toggle("active", t.dataset.tab === name);
    });
    panes.forEach(function (p) {
      p.classList.toggle("active", p.dataset.tab === name);
    });
    if (name === "gallery") loadGallery();
    if (name === "status") loadStatus();
  }

  tabs.forEach(function (t) {
    t.addEventListener("click", function () {
      switchTab(t.dataset.tab);
    });
  });

  /* --- WebSocket live feed --- */
  var ws = null;
  var frameCount = 0;
  var liveImg = document.getElementById("live-img");
  var overlay = document.getElementById("feed-overlay");
  var wsStatus = document.getElementById("ws-status");
  var frameCounter = document.getElementById("frame-counter");
  var prevBlobUrl = null;

  function connectWs() {
    var proto = location.protocol === "https:" ? "wss:" : "ws:";
    ws = new WebSocket(proto + "//" + location.host + "/ws/live");
    ws.binaryType = "arraybuffer";

    ws.onopen = function () {
      wsStatus.textContent = "CONNECTED";
      wsStatus.style.color = "var(--green)";
      overlay.classList.add("hidden");
    };

    ws.onmessage = function (evt) {
      if (prevBlobUrl) URL.revokeObjectURL(prevBlobUrl);
      var blob = new Blob([evt.data], { type: "image/jpeg" });
      prevBlobUrl = URL.createObjectURL(blob);
      liveImg.src = prevBlobUrl;
      frameCount++;
      frameCounter.textContent = frameCount + " frames";
    };

    ws.onclose = function () {
      wsStatus.textContent = "DISCONNECTED";
      wsStatus.style.color = "var(--red)";
      overlay.classList.remove("hidden");
      overlay.textContent = "RECONNECTING...";
      setTimeout(connectWs, 2000);
    };

    ws.onerror = function () {
      ws.close();
    };
  }

  connectWs();

  /* --- Capture button --- */
  var btnCapture = document.getElementById("btn-capture");

  btnCapture.addEventListener("click", function () {
    btnCapture.disabled = true;
    btnCapture.textContent = "[ CAPTURING... ]";

    fetch("/api/capture", { method: "POST" })
      .then(function (r) { return r.json(); })
      .then(function (data) {
        if (data.ok) {
          btnCapture.textContent = "[ SAVED: " + data.name + " ]";
          btnCapture.classList.add("flash");
        } else {
          btnCapture.textContent = "[ ERROR: " + data.error + " ]";
        }
        setTimeout(function () {
          btnCapture.textContent = "[ CAPTURE ]";
          btnCapture.classList.remove("flash");
          btnCapture.disabled = false;
        }, 1500);
      })
      .catch(function () {
        btnCapture.textContent = "[ NETWORK ERROR ]";
        setTimeout(function () {
          btnCapture.textContent = "[ CAPTURE ]";
          btnCapture.disabled = false;
        }, 1500);
      });
  });

  /* --- Gallery --- */
  var galleryGrid = document.getElementById("gallery-grid");
  var lightbox = document.getElementById("lightbox");
  var lightboxImg = document.getElementById("lightbox-img");
  var lbName = document.getElementById("lb-name");
  var galleryItems = [];
  var lbIndex = 0;

  function loadGallery() {
    fetch("/api/gallery")
      .then(function (r) { return r.json(); })
      .then(function (items) {
        items.sort(function (a, b) { return b.timestamp - a.timestamp; });
        galleryItems = items;
        galleryGrid.innerHTML = "";
        items.forEach(function (item, i) {
          var img = document.createElement("img");
          img.src = "/captures/" + item.name;
          img.loading = "lazy";
          img.addEventListener("click", function () { openLightbox(i); });
          galleryGrid.appendChild(img);
        });
        if (items.length === 0) {
          galleryGrid.innerHTML = '<p style="padding:16px;color:var(--green-dim)">No captures yet.</p>';
        }
      });
  }

  function openLightbox(index) {
    lbIndex = index;
    var item = galleryItems[index];
    lightboxImg.src = "/captures/" + item.name;
    lbName.textContent = item.name;
    lightbox.classList.remove("hidden");
  }

  function closeLightbox() {
    lightbox.classList.add("hidden");
  }

  document.getElementById("lb-close").addEventListener("click", closeLightbox);
  document.getElementById("lb-prev").addEventListener("click", function () {
    if (lbIndex > 0) openLightbox(lbIndex - 1);
  });
  document.getElementById("lb-next").addEventListener("click", function () {
    if (lbIndex < galleryItems.length - 1) openLightbox(lbIndex + 1);
  });
  document.getElementById("lb-delete").addEventListener("click", function () {
    var item = galleryItems[lbIndex];
    if (!confirm("Delete " + item.name + "?")) return;
    fetch("/api/gallery/" + item.name, { method: "DELETE" })
      .then(function () {
        closeLightbox();
        loadGallery();
      });
  });

  /* Swipe in lightbox */
  var touchX0 = null;
  lightbox.addEventListener("touchstart", function (e) {
    touchX0 = e.touches[0].clientX;
  });
  lightbox.addEventListener("touchend", function (e) {
    if (touchX0 === null) return;
    var dx = e.changedTouches[0].clientX - touchX0;
    if (Math.abs(dx) > 50) {
      if (dx < 0 && lbIndex < galleryItems.length - 1) openLightbox(lbIndex + 1);
      if (dx > 0 && lbIndex > 0) openLightbox(lbIndex - 1);
    }
    touchX0 = null;
  });

  /* --- Status --- */
  var statusReadout = document.getElementById("status-readout");
  var statusInterval = null;

  function loadStatus() {
    fetch("/api/status")
      .then(function (r) { return r.json(); })
      .then(function (s) {
        var lines = [
          "╔══════════════════════════════════════╗",
          "║   CRT SIGNAL MONITOR v0.1            ║",
          "╠══════════════════════════════════════╣",
          "║                                      ║",
          "║  VIDEO:    " + pad(s.video_standard, 26) + "║",
          "║  COLOR:    " + pad(s.color_enabled ? "ENABLED" : "DISABLED", 26) + "║",
          "║  LINES:    " + pad(String(s.active_lines), 26) + "║",
          "║  SAMPLE:   " + pad((s.sample_rate_hz / 1e6).toFixed(3) + " MHz", 26) + "║",
          "║                                      ║",
          "╠══════════════════════════════════════╣",
          "║                                      ║",
          "║  DEVICE:   " + pad(s.capture_device, 26) + "║",
          "║  RES:      " + pad(s.capture_resolution, 26) + "║",
          "║  UPTIME:   " + pad(formatUptime(s.uptime_s), 26) + "║",
          "║                                      ║",
          "╚══════════════════════════════════════╝",
          "",
          "> READY_",
        ];
        statusReadout.innerHTML = lines.join("\n");
        var cursor = document.createElement("span");
        cursor.className = "cursor";
        cursor.textContent = "█";
        statusReadout.appendChild(cursor);
      })
      .catch(function () {
        statusReadout.textContent = "ERROR: connection lost\n\n> RETRY_";
      });
  }

  function pad(str, len) {
    while (str.length < len) str += " ";
    return str;
  }

  function formatUptime(s) {
    var h = Math.floor(s / 3600);
    var m = Math.floor((s % 3600) / 60);
    var sec = s % 60;
    return h + "h " + m + "m " + sec + "s";
  }

  /* Auto-refresh status every 5s when visible */
  setInterval(function () {
    var statusPane = document.querySelector('[data-tab="status"]');
    if (statusPane && statusPane.classList.contains("active")) {
      loadStatus();
    }
  }, 5000);

})();
```

- [ ] **Step 2: Build and run full integration test**

```bash
cd tools/crt_monitor && make && ./crt_monitor -p 9999 -d /dev/video0 &
sleep 1

# Test all endpoints
curl -s http://localhost:9999/api/status | python3 -m json.tool
curl -s -X POST http://localhost:9999/api/capture | python3 -m json.tool
curl -s http://localhost:9999/api/gallery | python3 -m json.tool
curl -s http://localhost:9999/ | head -3

kill %1
```

- [ ] **Step 3: Commit**

```bash
git add tools/crt_monitor/static/app.js
git commit -m "feat(monitor): frontend JS — WebSocket live, gallery, capture"
```

---

### Task 6: Integration Test on Real Hardware

**Files:** None (testing only)

- [ ] **Step 1: Build final binary**

```bash
cd tools/crt_monitor && make clean && make
```

- [ ] **Step 2: Kill existing capture processes and HTTP servers**

```bash
pkill -f "ffmpeg.*v4l2" 2>/dev/null
pkill -f "http.server.*9090" 2>/dev/null
fuser -k /dev/video0 2>/dev/null
sleep 1
```

- [ ] **Step 3: Launch CRT monitor on port 9090**

```bash
cd tools/crt_monitor && ./crt_monitor -p 9090 -d /dev/video0
```

Expected output:
```
=== CRT Monitor Dashboard ===
Device:   /dev/video0
Static:   static
Captures: captures
[capture] format: 1280x720 MJPEG
[capture] streaming started on /dev/video0
Listening: http://0.0.0.0:9090
```

- [ ] **Step 4: Test from Android browser**

Open `http://100.66.190.106:9090` on Android. Verify:
- LIVE tab shows CRT feed updating at ~3fps
- CAPTURE button takes snapshot and flashes green
- GALLERY tab shows saved captures with thumbnails
- STATUS tab shows retro terminal readout
- Bottom status line shows "CONNECTED" and frame count
- Retro green theme with scanline overlay visible

- [ ] **Step 5: Commit everything**

```bash
git add -A tools/crt_monitor/
git commit -m "feat(monitor): CRT Monitor Dashboard MVP — live feed, capture, gallery"
```

---

### Task 7: Add .gitignore and Documentation

**Files:**
- Create: `tools/crt_monitor/.gitignore`
- Modify: `tools/crt_monitor/Makefile` (add `help` target)

- [ ] **Step 1: Write .gitignore**

```
crt_monitor
captures/*.jpg
```

- [ ] **Step 2: Add help target to Makefile**

Add after the `clean` target:

```makefile
help:
	@echo "CRT Monitor Dashboard"
	@echo ""
	@echo "  make          Build crt_monitor binary"
	@echo "  make clean    Remove binary"
	@echo ""
	@echo "  ./crt_monitor [options]"
	@echo "    -p PORT     Listen port (default: 8080)"
	@echo "    -d DEVICE   V4L2 device (default: /dev/video0)"
	@echo "    -c DIR      Captures dir (default: ./captures)"
	@echo "    -s DIR      Static dir (default: ./static)"
```

- [ ] **Step 3: Commit**

```bash
git add tools/crt_monitor/.gitignore tools/crt_monitor/Makefile
git commit -m "chore(monitor): gitignore and Makefile help target"
```

---

## Self-Review Checklist

- **Spec coverage**: All MVP features covered — live feed (Task 3-5), capture button (Task 3, 5), gallery (Task 3, 5), status (Task 3-5). Visual style (Task 4). Build/run (Task 1, 6). CLI args (Task 3). ✓
- **Placeholder scan**: No TBD/TODO. All code blocks complete. ✓
- **Type consistency**: `capture_ctx_t`, `app_ctx_t`, `capture_grab()`, `routes_handle()`, `routes_broadcast_frame()`, `status_init()`, `status_to_json()` — consistent across all tasks. ✓
- **API consistency**: `/ws/live`, `/api/capture`, `/api/gallery`, `/api/status`, `/captures/*` — match spec exactly. ✓
