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
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <limits.h>
#include <termios.h>
#include <sys/ioctl.h>

#define SEC_HEADERS \
    "X-Content-Type-Options: nosniff\r\n" \
    "X-Frame-Options: DENY\r\n" \
    "Referrer-Policy: no-referrer\r\n"

static app_ctx_t *s_app = NULL;

/* Return true if auth passes (or no auth configured).
 * Sends 401 and returns false if auth fails. */
static bool check_auth(struct mg_connection *c, struct mg_http_message *hm) {
    if (!s_app || !s_app->auth_user || !s_app->auth_pass) return true;

    char user[128] = {0}, pass[128] = {0};
    mg_http_creds(hm, user, sizeof(user), pass, sizeof(pass));

    if (strlen(user) > 0 &&
        strcmp(user, s_app->auth_user) == 0 &&
        strcmp(pass, s_app->auth_pass) == 0) {
        return true;
    }

    mg_http_reply(c, 401,
                  "WWW-Authenticate: Basic realm=\"CRT Monitor\"\r\n"
                  "Content-Type: text/plain\r\n",
                  "Unauthorized\n");
    return false;
}

/* JSON-escape a string into dst. Returns bytes written (excluding NUL). */
static size_t json_escape(char *dst, size_t dst_size, const char *src) {
    size_t pos = 0;
    for (; *src && pos + 6 < dst_size; src++) {
        switch (*src) {
            case '"': dst[pos++] = '\\';
                dst[pos++] = '"';
                break;
            case '\\': dst[pos++] = '\\';
                dst[pos++] = '\\';
                break;
            case '\n': dst[pos++] = '\\';
                dst[pos++] = 'n';
                break;
            case '\r': dst[pos++] = '\\';
                dst[pos++] = 'r';
                break;
            case '\t': dst[pos++] = '\\';
                dst[pos++] = 't';
                break;
            default:
                if ((unsigned char) *src < 0x20) {
                    pos += (size_t) snprintf(dst + pos, dst_size - pos, "\\u%04x", (unsigned char) *src);
                } else {
                    dst[pos++] = *src;
                }
        }
    }
    dst[pos] = '\0';
    return pos;
}

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
/* GET /api/mtime — latest mtime of static files (livereload trigger)   */
/* ------------------------------------------------------------------ */
static void handle_api_mtime(struct mg_connection *c,
                              struct mg_http_message *hm)
{
    (void)hm;
    long latest = 0;
    const char *files[] = {"static/index.html", "static/app.js", "static/style.css", NULL};
    for (int i = 0; files[i]; i++) {
        struct stat st;
        if (stat(files[i], &st) == 0 && (long)st.st_mtime > latest) {
            latest = (long)st.st_mtime;
        }
    }
    mg_http_reply(c, 200, "Content-Type: application/json\r\nCache-Control: no-cache\r\n",
                  "{\"mtime\":%ld}\n", latest);
}

/* ------------------------------------------------------------------ */
/* GET /stream — Motion-JPEG stream (works everywhere, no WebSocket)    */
/* ------------------------------------------------------------------ */
static void handle_stream(struct mg_connection *c,
                           struct mg_http_message *hm)
{
    (void)hm;
    if (!s_app || !s_app->capture) {
        mg_http_reply(c, 503, "", "no capture\n");
        return;
    }

    mg_printf(c, "HTTP/1.1 200 OK\r\n"
                 "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
                 "Cache-Control: no-cache\r\n"
                 "Connection: keep-alive\r\n\r\n");
    c->data[0] = 'M'; /* mark as MJPEG client */
}

/* ------------------------------------------------------------------ */
/* GET /api/snapshot — returns current JPEG frame without saving        */
/* ------------------------------------------------------------------ */
static void handle_api_snapshot(struct mg_connection *c,
                                 struct mg_http_message *hm)
{
    (void)hm;
    if (!s_app || !s_app->capture) {
        mg_http_reply(c, 503, "Content-Type: text/plain\r\n", "no capture\n");
        return;
    }

    const uint8_t *buf = NULL;
    size_t len = 0;
    /* Retry a few times — timer may have consumed the current frame */
    for (int attempt = 0; attempt < 10; attempt++) {
        if (capture_grab(s_app->capture, &buf, &len) == 0 && buf != NULL) break;
        usleep(10000); /* 10ms */
    }
    if (buf == NULL) {
        mg_http_reply(c, 500, "Content-Type: text/plain\r\n", "grab failed\n");
        return;
    }

    mg_printf(c, "HTTP/1.1 200 OK\r\n"
                 "Content-Type: image/jpeg\r\n"
                 "Content-Length: %lu\r\n"
                 "Cache-Control: no-cache, no-store, must-revalidate\r\n"
                 "Pragma: no-cache\r\n"
                 "Expires: 0\r\n\r\n",
              (unsigned long)len);
    mg_send(c, buf, len);
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
        mg_http_reply(c, 200,
                      "Content-Type: application/json\r\n"
                      "Cache-Control: no-cache, no-store, must-revalidate\r\n"
                      "Pragma: no-cache\r\n"
                      "Expires: 0\r\n",
                      "[]\n");
        return;
    }

    /* Build JSON array.  Fixed 64 KB scratch buffer; enough for ~500 entries.
       Reserve 4 bytes at end for "]\n\0" plus safety margin. */
    static char buf[65536];
    const size_t buf_limit = sizeof(buf) - 4;
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

        if (!first && pos < buf_limit) buf[pos++] = ',';
        first = false;

        char escaped_name[512];
        json_escape(escaped_name, sizeof(escaped_name), ent->d_name);
        int written = snprintf(buf + pos, sizeof(buf) - pos,
                               "{\"file\":\"%s\",\"size\":%lld,\"mtime\":%lld}",
                               escaped_name, (long long) st.st_size, (long long) st.st_mtime);
        if (written > 0) pos += (size_t) written;

        if (pos >= buf_limit) break;
    }
    closedir(dir);
    buf[pos++] = ']';
    buf[pos++] = '\n';
    buf[pos]   = '\0';

    mg_http_reply(c, 200,
                  "Content-Type: application/json\r\n"
                  "Cache-Control: no-cache, no-store, must-revalidate\r\n"
                  "Pragma: no-cache\r\n"
                  "Expires: 0\r\n",
                  "%s", buf);
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

    /* Reject path traversal, null bytes, and non-printable chars */
    if (strchr(filename, '/') || strchr(filename, '\\') ||
        memchr(filename, '\0', fnlen) != (filename + fnlen) ||
        strstr(filename, "..") != NULL) {
        mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                      "{\"error\":\"invalid filename\"}\n");
        return;
    }
    /* Only allow alphanumeric, dots, dashes, underscores */
    for (size_t i = 0; i < fnlen; i++) {
        char ch = filename[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == '_')) {
            mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                          "{\"error\":\"invalid filename\"}\n");
            return;
        }
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

    /* Sanitize — reject path traversal and shell metacharacters */
    if (strstr(dir, "..") != NULL) {
        mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                      "{\"error\":\"invalid dir\"}\n");
        return;
    }
    for (const char *p = dir; *p; p++) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '/' || *p == '_' ||
              *p == '-' || *p == '.')) {
            mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                          "{\"error\":\"invalid characters in dir\"}\n");
            return;
        }
    }

    /* Verify dir is under captures_dir using canonicalized paths */
    if (s_app) {
        char real_dir[PATH_MAX], real_captures[PATH_MAX];
        if (!realpath(dir, real_dir) || !realpath(s_app->captures_dir, real_captures) ||
            strncmp(real_dir, real_captures, strlen(real_captures)) != 0) {
            mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                          "{\"error\":\"dir must be under captures directory\"}\n");
            return;
        }
    }

    /* Run R analysis in background via fork+exec (no shell injection) */
    pid_t pid = fork();
    if (pid == 0) {
        char out_dir[512];
        snprintf(out_dir, sizeof(out_dir), "%s/analysis", dir);
        int fd = open("/tmp/r_analysis.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);
        }
        execlp("Rscript", "Rscript", "../analysis/crt_flicker_analysis.R",
               dir, out_dir, (char *) NULL);
        _exit(127);
    }

    mg_http_reply(c, 200, "Content-Type: application/json\r\n",
        "{\"ok\":true,\"dir\":\"%s\"}\n", dir);
}

/* ------------------------------------------------------------------ */
/* POST /api/upload — receive image, process, send to ESP32 via serial  */
/* ------------------------------------------------------------------ */

/* Serial upload protocol constants — must match ESP32 app_main.c */
#define UPLOAD_MAGIC_0  0xFB
#define UPLOAD_MAGIC_1  0xDA
#define UPLOAD_MAGIC_2  0x00
#define UPLOAD_MAGIC_3  0x01
#define UPLOAD_ACK      0x06
#define UPLOAD_FB_SIZE  (256 * 240)  /* 61440 bytes — must match NTSC active_lines */

static int serial_send_framebuffer(int fd, const uint8_t *pixels, size_t len) {
    if (fd < 0) {
        fprintf(stderr, "[upload] serial fd not open\n");
        return -1;
    }

    /* Flush any stale data */
    tcflush(fd, TCIOFLUSH);

    /* Send magic bytes */
    uint8_t magic[] = {UPLOAD_MAGIC_0, UPLOAD_MAGIC_1, UPLOAD_MAGIC_2, UPLOAD_MAGIC_3};
    if (write(fd, magic, sizeof(magic)) != sizeof(magic)) {
        fprintf(stderr, "[upload] write magic failed\n");
        close(fd);
        return -1;
    }

    /* Send pixel data in chunks */
    size_t sent = 0;
    while (sent < len) {
        size_t chunk = len - sent;
        if (chunk > 512) chunk = 512;
        ssize_t n = write(fd, pixels + sent, chunk);
        if (n <= 0) {
            fprintf(stderr, "[upload] write failed at byte %zu: %s\n", sent, strerror(errno));
            close(fd);
            return -1;
        }
        sent += (size_t) n;
        tcdrain(fd); /* wait for data to be transmitted */
    }

    /* Wait for ACK — ESP32 may also send log output, scan for ACK byte */
    usleep(500000); /* 500ms for ESP32 to process and respond */
    uint8_t buf_ack[256];
    ssize_t n = read(fd, buf_ack, sizeof(buf_ack));

    bool got_ack = false;
    for (ssize_t i = 0; i < n; i++) {
        if (buf_ack[i] == UPLOAD_ACK) {
            got_ack = true;
            break;
        }
    }

    if (!got_ack) {
        fprintf(stderr, "[upload] no ACK in %d bytes received\n", (int) n);
        if (n > 0) {
            fprintf(stderr, "[upload] first bytes: ");
            for (ssize_t i = 0; i < n && i < 16; i++) fprintf(stderr, "%02x ", buf_ack[i]);
            fprintf(stderr, "\n");
        }
        return -1;
    }

    fprintf(stderr, "[upload] sent %zu bytes, ACK received\n", sent);
    return 0;
}

static void handle_api_upload(struct mg_connection *c,
                              struct mg_http_message *hm) {
    if (!s_app || s_app->serial_fd < 0) {
        mg_http_reply(c, 503, "Content-Type: application/json\r\n",
                      "{\"error\":\"serial not connected\"}\n");
        return;
    }
    if (!s_app->img2fb_path) {
        mg_http_reply(c, 503, "Content-Type: application/json\r\n",
                      "{\"error\":\"img2fb.py path not configured\"}\n");
        return;
    }

    /* Extract image from multipart body */
    struct mg_http_part part;
    size_t ofs = 0;
    bool found = false;
    while ((ofs = mg_http_next_multipart(hm->body, ofs, &part)) > 0) {
        if (part.body.len > 0) {
            found = true;
            break;
        }
    }

    if (!found || part.body.len == 0) {
        mg_http_reply(c, 400, "Content-Type: application/json\r\n",
                      "{\"error\":\"no image data in request\"}\n");
        return;
    }

    /* Save uploaded image to temp file */
    char tmp_img[] = "/tmp/crt_upload_XXXXXX";
    int tmp_fd = mkstemp(tmp_img);
    if (tmp_fd < 0) {
        mg_http_reply(c, 500, "Content-Type: application/json\r\n",
                      "{\"error\":\"mkstemp failed\"}\n");
        return;
    }
    write(tmp_fd, part.body.buf, part.body.len);
    close(tmp_fd);

    /* Convert image to raw binary using img2fb.py */
    char tmp_bin[128];
    snprintf(tmp_bin, sizeof(tmp_bin), "%s.bin", tmp_img);

    pid_t pid = fork();
    if (pid == 0) {
        /* Child: run img2fb.py --raw */
        int null_fd = open("/dev/null", O_WRONLY);
        if (null_fd >= 0) {
            dup2(null_fd, STDOUT_FILENO);
            close(null_fd);
        }
        execlp("python3", "python3", s_app->img2fb_path, "--raw",
               tmp_img, tmp_bin, (char *) NULL);
        _exit(127);
    }

    if (pid < 0) {
        unlink(tmp_img);
        mg_http_reply(c, 500, "Content-Type: application/json\r\n",
                      "{\"error\":\"fork failed\"}\n");
        return;
    }

    /* Wait for img2fb.py to finish (blocking — typically ~200ms) */
    int status;
    waitpid(pid, &status, 0);

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        unlink(tmp_img);
        unlink(tmp_bin);
        mg_http_reply(c, 500, "Content-Type: application/json\r\n",
                      "{\"error\":\"image processing failed\"}\n");
        return;
    }

    /* Read the raw binary */
    FILE *f = fopen(tmp_bin, "rb");
    if (!f) {
        unlink(tmp_img);
        unlink(tmp_bin);
        mg_http_reply(c, 500, "Content-Type: application/json\r\n",
                      "{\"error\":\"raw binary not found\"}\n");
        return;
    }

    uint8_t pixels[UPLOAD_FB_SIZE];
    size_t nread = fread(pixels, 1, UPLOAD_FB_SIZE, f);
    fclose(f);
    unlink(tmp_img);
    unlink(tmp_bin);

    if (nread != UPLOAD_FB_SIZE) {
        mg_http_reply(c, 500, "Content-Type: application/json\r\n",
                      "{\"error\":\"unexpected raw size: %lu\"}\n", (unsigned long) nread);
        return;
    }

    /* Send to ESP32 via serial */
    if (serial_send_framebuffer(s_app->serial_fd, pixels, UPLOAD_FB_SIZE) != 0) {
        mg_http_reply(c, 500, "Content-Type: application/json\r\n",
                      "{\"error\":\"serial transfer failed\"}\n");
        return;
    }

    /* Also save a copy to captures for the gallery */
    char save_path[512];
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    snprintf(save_path, sizeof(save_path), "%s/upload_%ld.jpg",
             s_app->captures_dir, (long) ts.tv_sec);
    FILE *sf = fopen(save_path, "wb");
    if (sf) {
        fwrite(part.body.buf, 1, part.body.len, sf);
        fclose(sf);
    }

    mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                  "{\"ok\":true,\"bytes\":%d,\"message\":\"image sent to CRT\"}\n",
                  UPLOAD_FB_SIZE);
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
    if (n >= sizeof(buf)) n = sizeof(buf) - 1; /* snprintf may return > buf_size */
    buf[n] = '\0';
    mg_http_reply(c, 200,
                  "Content-Type: application/json\r\n"
                  "Cache-Control: no-cache, no-store, must-revalidate\r\n"
                  "Pragma: no-cache\r\n"
                  "Expires: 0\r\n",
                  "%s\n", buf);
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

        /* Auth check — protect API, stream, and WebSocket endpoints.
         * Static files (HTML/CSS/JS) are served without auth so the
         * login page can load. The page handles auth via JS. */
        bool needs_auth = (uri.len >= 4 && memcmp(uri.buf, "/api", 4) == 0) ||
                          (uri.len >= 7 && memcmp(uri.buf, "/stream", 7) == 0) ||
                          (uri.len >= 3 && memcmp(uri.buf, "/ws", 3) == 0) ||
                          (uri.len >= 9 && memcmp(uri.buf, "/captures", 9) == 0);
        if (needs_auth && !check_auth(c, hm)) return;

        /* MJPEG stream — universal, works on iPhone/Firefox/anything */
        if (mg_match(uri, mg_str("/stream"), NULL)) {
            handle_stream(c, hm);
            return;
        }

        /* WebSocket upgrade */
        if (mg_match(uri, mg_str("/ws/live"), NULL)) {
            handle_ws_live(c, hm);
            return;
        }

        /* GET /api/mtime (livereload) */
        if (mg_strcmp(method, mg_str("GET")) == 0 &&
            mg_match(uri, mg_str("/api/mtime"), NULL)) {
            handle_api_mtime(c, hm);
            return;
        }

        /* GET /api/snapshot */
        if (mg_strcmp(method, mg_str("GET")) == 0 &&
            mg_match(uri, mg_str("/api/snapshot"), NULL)) {
            handle_api_snapshot(c, hm);
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

        /* POST /api/upload — upload image to CRT */
        if (mg_strcmp(method, mg_str("POST")) == 0 &&
            mg_match(uri, mg_str("/api/upload"), NULL)) {
            handle_api_upload(c, hm);
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
                .extra_headers = "Cache-Control: no-cache\r\n" SEC_HEADERS
            };
            /* strip /captures prefix so mongoose resolves relative to root */
            hm->uri.buf += sizeof("/captures") - 1;
            hm->uri.len -= sizeof("/captures") - 1;
            mg_http_serve_dir(c, hm, &opts);
            return;
        }

        /* /live → MJPEG PWA page (universal, iPhone/Safari) */
        if (mg_match(uri, mg_str("/live"), NULL)) {
            struct mg_http_serve_opts opts = {
                .root_dir = s_app ? s_app->static_dir : "static",
                .extra_headers = "Cache-Control: no-cache\r\n" SEC_HEADERS
            };
            hm->uri = mg_str("/live.html");
            mg_http_serve_dir(c, hm, &opts);
            return;
        }

        /* /raw → zero-chrome fullscreen view (just the image, black bg) */
        if (mg_match(uri, mg_str("/raw"), NULL)) {
            struct mg_http_serve_opts opts = {
                .root_dir = s_app ? s_app->static_dir : "static",
                .extra_headers = "Cache-Control: no-cache\r\n" SEC_HEADERS
            };
            hm->uri = mg_str("/raw.html");
            mg_http_serve_dir(c, hm, &opts);
            return;
        }

        /* Everything else → static dir */
        if (s_app && s_app->static_dir) {
            struct mg_http_serve_opts opts = {
                .root_dir = s_app->static_dir,
                .extra_headers = SEC_HEADERS
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
            /* WebSocket client */
            mg_ws_send(c, (const char *)jpg, len, WEBSOCKET_OP_BINARY);
        } else if (c->data[0] == 'M') {
            /* MJPEG stream client */
            mg_printf(c, "--frame\r\n"
                         "Content-Type: image/jpeg\r\n"
                         "Content-Length: %lu\r\n\r\n",
                      (unsigned long)len);
            mg_send(c, jpg, len);
            mg_send(c, "\r\n", 2);
        }
    }
}
