#include "routes.h"
#include "capture.h"
#include "status.h"
#include "mongoose.h"
#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>

static app_ctx_t *s_app = NULL;

void routes_init(app_ctx_t *app, capture_ctx_t *capture,
                 const char *static_dir, const char *captures_dir)
{
    app->capture      = capture;
    app->static_dir   = static_dir;
    app->captures_dir = captures_dir;
    app->ws_client_count = 0;
    s_app = app;
}

/* ------------------------------------------------------------------ */
/* WebSocket upgrade                                                    */
/* ------------------------------------------------------------------ */
static void handle_ws_live(struct mg_connection *c, struct mg_http_message *hm)
{
    mg_ws_upgrade(c, hm, NULL);
    c->data[0] = 'W';
    if (s_app) s_app->ws_client_count++;
}

/* ------------------------------------------------------------------ */
/* POST /api/capture                                                    */
/* ------------------------------------------------------------------ */
static void handle_api_capture(struct mg_connection *c,
                                struct mg_http_message *hm)
{
    (void)hm;
    if (!s_app || !s_app->capture) {
        mg_http_reply(c, 503, "Content-Type: application/json\r\n",
                      "{\"error\":\"capture not available\"}\n");
        return;
    }

    const uint8_t *buf = NULL;
    size_t len = 0;
    if (capture_grab(s_app->capture, &buf, &len) != 0 || buf == NULL) {
        mg_http_reply(c, 500, "Content-Type: application/json\r\n",
                      "{\"error\":\"capture_grab failed\"}\n");
        return;
    }

    /* timestamp filename */
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    char filename[64];
    snprintf(filename, sizeof(filename), "%ld%03ld.jpg",
             (long)ts.tv_sec, (long)(ts.tv_nsec / 1000000));

    char path[512];
    snprintf(path, sizeof(path), "%s/%s", s_app->captures_dir, filename);

    FILE *f = fopen(path, "wb");
    if (!f) {
        mg_http_reply(c, 500, "Content-Type: application/json\r\n",
                      "{\"error\":\"fopen failed\"}\n");
        return;
    }
    fwrite(buf, 1, len, f);
    fclose(f);

    mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                  "{\"ok\":true,\"file\":\"%s\",\"bytes\":%lu}\n",
                  filename, (unsigned long)len);
}

/* ------------------------------------------------------------------ */
/* GET /api/gallery                                                     */
/* ------------------------------------------------------------------ */
static void handle_api_gallery(struct mg_connection *c,
                                struct mg_http_message *hm)
{
    (void)hm;
    if (!s_app) {
        mg_http_reply(c, 503, "Content-Type: application/json\r\n",
                      "{\"error\":\"not initialised\"}\n");
        return;
    }

    DIR *dir = opendir(s_app->captures_dir);
    if (!dir) {
        mg_http_reply(c, 200, "Content-Type: application/json\r\n", "[]\n");
        return;
    }

    /* Build JSON array.  We use a fixed 64 KB scratch buffer; enough for
       ~500 entries.  A real impl would heap-alloc, but keep it simple. */
    static char buf[65536];
    size_t pos = 0;
    buf[pos++] = '[';
    bool first = true;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        size_t nlen = strlen(ent->d_name);
        if (nlen < 4 || strcmp(ent->d_name + nlen - 4, ".jpg") != 0)
            continue;

        char full[512];
        snprintf(full, sizeof(full), "%s/%s", s_app->captures_dir, ent->d_name);
        struct stat st;
        if (stat(full, &st) != 0) continue;

        if (!first) buf[pos++] = ',';
        first = false;

        pos += (size_t)snprintf(buf + pos, sizeof(buf) - pos,
            "{\"file\":\"%s\",\"size\":%lld,\"mtime\":%lld}",
            ent->d_name, (long long)st.st_size, (long long)st.st_mtime);

        if (pos >= sizeof(buf) - 128) break; /* safety */
    }
    closedir(dir);
    buf[pos++] = ']';
    buf[pos++] = '\n';
    buf[pos]   = '\0';

    mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s", buf);
}

/* ------------------------------------------------------------------ */
/* DELETE /api/gallery/<name>                                           */
/* ------------------------------------------------------------------ */
static void handle_api_gallery_delete(struct mg_connection *c,
                                      struct mg_http_message *hm)
{
    /* URI: /api/gallery/filename.jpg  — extract basename */
    struct mg_str uri = hm->uri;
    const char *prefix = "/api/gallery/";
    size_t plen = strlen(prefix);
    if (uri.len <= plen) {
        mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                      "{\"error\":\"missing filename\"}\n");
        return;
    }

    char filename[256];
    size_t fnlen = uri.len - plen;
    if (fnlen >= sizeof(filename)) fnlen = sizeof(filename) - 1;
    memcpy(filename, uri.buf + plen, fnlen);
    filename[fnlen] = '\0';

    /* Reject path traversal */
    if (strchr(filename, '/') || strchr(filename, '\\')) {
        mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                      "{\"error\":\"invalid filename\"}\n");
        return;
    }

    char path[512];
    snprintf(path, sizeof(path), "%s/%s", s_app->captures_dir, filename);

    if (unlink(path) != 0) {
        mg_http_reply(c, 404, "Content-Type: application/json\r\n",
                      "{\"error\":\"not found\"}\n");
        return;
    }

    mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                  "{\"ok\":true,\"file\":\"%s\"}\n", filename);
}

/* ------------------------------------------------------------------ */
/* POST /api/record?duration=N                                          */
/* ------------------------------------------------------------------ */
static void handle_api_record_start(struct mg_connection *c,
                                     struct mg_http_message *hm)
{
    if (!s_app) {
        mg_http_reply(c, 503, "Content-Type: application/json\r\n",
                      "{\"error\":\"not initialised\"}\n");
        return;
    }
    if (s_app->recording) {
        mg_http_reply(c, 409, "Content-Type: application/json\r\n",
                      "{\"error\":\"already recording\",\"frames_left\":%d}\n",
                      s_app->rec_frames_left);
        return;
    }

    /* Parse duration (seconds) from query string, default 5, max 30 */
    int duration = 5;
    struct mg_str q = hm->query;
    char tmp[16] = {0};
    if (mg_http_get_var(&q, "duration", tmp, sizeof(tmp)) > 0) {
        duration = atoi(tmp);
    }
    if (duration < 1) duration = 1;
    if (duration > 30) duration = 30;

    /* Create recording directory */
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    snprintf(s_app->rec_dir, sizeof(s_app->rec_dir), "%s/rec_%ld",
             s_app->captures_dir, (long)ts.tv_sec);
    mkdir(s_app->rec_dir, 0755);

    s_app->rec_frames_left  = duration * 30;
    s_app->rec_frames_saved = 0;
    s_app->recording        = true;

    mg_http_reply(c, 200, "Content-Type: application/json\r\n",
        "{\"ok\":true,\"dir\":\"%s\",\"duration\":%d,\"target_frames\":%d}\n",
        s_app->rec_dir, duration, s_app->rec_frames_left);
}

/* ------------------------------------------------------------------ */
/* GET /api/record                                                      */
/* ------------------------------------------------------------------ */
static void handle_api_record_status(struct mg_connection *c,
                                      struct mg_http_message *hm)
{
    (void)hm;
    if (!s_app) {
        mg_http_reply(c, 503, "Content-Type: application/json\r\n",
                      "{\"error\":\"not initialised\"}\n");
        return;
    }
    mg_http_reply(c, 200, "Content-Type: application/json\r\n",
        "{\"recording\":%s,\"frames_saved\":%d,\"frames_left\":%d,\"dir\":\"%s\"}\n",
        s_app->recording ? "true" : "false",
        s_app->rec_frames_saved,
        s_app->rec_frames_left,
        s_app->rec_dir);
}

/* ------------------------------------------------------------------ */
/* POST /api/analyze?dir=<path>                                         */
/* ------------------------------------------------------------------ */
static void handle_api_analyze(struct mg_connection *c,
                                struct mg_http_message *hm)
{
    char dir[256] = {0};
    struct mg_str q = hm->query;
    if (mg_http_get_var(&q, "dir", dir, sizeof(dir)) <= 0) {
        mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                      "{\"error\":\"missing dir param\"}\n");
        return;
    }

    /* Sanitize — reject path traversal */
    if (strstr(dir, "..") != NULL) {
        mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                      "{\"error\":\"invalid dir\"}\n");
        return;
    }

    /* Run R analysis in background */
    /* Resolve R script relative to the server binary's parent dir */
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
        "Rscript ../analysis/crt_flicker_analysis.R '%s' '%s/analysis' "
        ">/tmp/r_analysis.log 2>&1 &",
        dir, dir);
    system(cmd);

    mg_http_reply(c, 200, "Content-Type: application/json\r\n",
        "{\"ok\":true,\"dir\":\"%s\"}\n", dir);
}

/* ------------------------------------------------------------------ */
/* GET /api/status                                                      */
/* ------------------------------------------------------------------ */
static void handle_api_status(struct mg_connection *c,
                               struct mg_http_message *hm)
{
    (void)hm;
    static char buf[512];
    size_t n = status_to_json(buf, sizeof(buf));
    buf[n] = '\0';
    mg_http_reply(c, 200, "Content-Type: application/json\r\n", "%s\n", buf);
}

/* ------------------------------------------------------------------ */
/* Main event handler                                                   */
/* ------------------------------------------------------------------ */
void routes_handle(struct mg_connection *c, int ev, void *ev_data)
{
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;
        struct mg_str uri    = hm->uri;
        struct mg_str method = hm->method;

        /* WebSocket upgrade */
        if (mg_match(uri, mg_str("/ws/live"), NULL)) {
            handle_ws_live(c, hm);
            return;
        }

        /* POST /api/capture */
        if (mg_strcmp(method, mg_str("POST")) == 0 &&
            mg_match(uri, mg_str("/api/capture"), NULL)) {
            handle_api_capture(c, hm);
            return;
        }

        /* POST /api/record */
        if (mg_strcmp(method, mg_str("POST")) == 0 &&
            mg_match(uri, mg_str("/api/record"), NULL)) {
            handle_api_record_start(c, hm);
            return;
        }

        /* GET /api/record */
        if (mg_strcmp(method, mg_str("GET")) == 0 &&
            mg_match(uri, mg_str("/api/record"), NULL)) {
            handle_api_record_status(c, hm);
            return;
        }

        /* POST /api/analyze */
        if (mg_strcmp(method, mg_str("POST")) == 0 &&
            mg_match(uri, mg_str("/api/analyze"), NULL)) {
            handle_api_analyze(c, hm);
            return;
        }

        /* GET /api/status */
        if (mg_strcmp(method, mg_str("GET")) == 0 &&
            mg_match(uri, mg_str("/api/status"), NULL)) {
            handle_api_status(c, hm);
            return;
        }

        /* DELETE /api/gallery/:name */
        if (mg_strcmp(method, mg_str("DELETE")) == 0 &&
            mg_match(uri, mg_str("/api/gallery/*"), NULL)) {
            handle_api_gallery_delete(c, hm);
            return;
        }

        /* GET /api/gallery */
        if (mg_strcmp(method, mg_str("GET")) == 0 &&
            mg_match(uri, mg_str("/api/gallery"), NULL)) {
            handle_api_gallery(c, hm);
            return;
        }

        /* Serve /captures/... from captures_dir */
        if (mg_match(uri, mg_str("/captures/*"), NULL)) {
            if (!s_app) return;
            struct mg_http_serve_opts opts = {
                .root_dir      = s_app->captures_dir,
                .extra_headers = "Cache-Control: no-cache\r\n"
            };
            /* strip /captures prefix so mongoose resolves relative to root */
            hm->uri.buf += sizeof("/captures") - 1;
            hm->uri.len -= sizeof("/captures") - 1;
            mg_http_serve_dir(c, hm, &opts);
            return;
        }

        /* Everything else → static dir */
        if (s_app && s_app->static_dir) {
            struct mg_http_serve_opts opts = {
                .root_dir = s_app->static_dir
            };
            mg_http_serve_dir(c, hm, &opts);
        } else {
            mg_http_reply(c, 404, "", "Not found\n");
        }

    } else if (ev == MG_EV_CLOSE) {
        if (c->data[0] == 'W' && s_app) {
            s_app->ws_client_count--;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Save frame to recording directory (called from fast timer)           */
/* ------------------------------------------------------------------ */
void routes_record_frame(const uint8_t *jpg, size_t len)
{
    if (!s_app || !s_app->recording || s_app->rec_frames_left <= 0) return;

    char path[512];
    snprintf(path, sizeof(path), "%s/frame_%04d.jpg",
             s_app->rec_dir, s_app->rec_frames_saved);

    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite(jpg, 1, len, f);
        fclose(f);
    }

    s_app->rec_frames_saved++;
    s_app->rec_frames_left--;
    if (s_app->rec_frames_left <= 0) {
        s_app->recording = false;
        fprintf(stderr, "[record] done: %d frames in %s\n",
                s_app->rec_frames_saved, s_app->rec_dir);
    }
}

/* ------------------------------------------------------------------ */
/* Broadcast a JPEG frame to all live WebSocket clients                 */
/* ------------------------------------------------------------------ */
void routes_broadcast_frame(struct mg_mgr *mgr, const uint8_t *jpg, size_t len)
{
    for (struct mg_connection *c = mgr->conns; c != NULL; c = c->next) {
        if (c->data[0] == 'W') {
            mg_ws_send(c, (const char *)jpg, len, WEBSOCKET_OP_BINARY);
        }
    }
}
