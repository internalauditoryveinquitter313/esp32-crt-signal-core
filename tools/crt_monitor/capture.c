#include "capture.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>

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
    memset(ctx, 0, sizeof(*ctx));
    ctx->fd = -1;

    /* Open device */
    ctx->fd = open(device, O_RDWR | O_NONBLOCK);
    if (ctx->fd == -1) {
        fprintf(stderr, "[capture] open(%s) failed: %s\n", device, strerror(errno));
        return -1;
    }

    /* Query capabilities */
    struct v4l2_capability cap;
    if (xioctl(ctx->fd, VIDIOC_QUERYCAP, &cap) == -1) {
        fprintf(stderr, "[capture] VIDIOC_QUERYCAP failed: %s\n", strerror(errno));
        goto fail;
    }
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf(stderr, "[capture] %s is not a video capture device\n", device);
        goto fail;
    }
    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        fprintf(stderr, "[capture] %s does not support streaming\n", device);
        goto fail;
    }
    fprintf(stderr, "[capture] opened %s: %s\n", device, cap.card);

    /* Set MJPEG format */
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = (unsigned int)width;
    fmt.fmt.pix.height      = (unsigned int)height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.field       = V4L2_FIELD_NONE;
    if (xioctl(ctx->fd, VIDIOC_S_FMT, &fmt) == -1) {
        fprintf(stderr, "[capture] VIDIOC_S_FMT failed: %s\n", strerror(errno));
        goto fail;
    }
    if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG) {
        fprintf(stderr, "[capture] device rejected MJPEG format\n");
        goto fail;
    }
    fprintf(stderr, "[capture] format set: %dx%d MJPEG\n",
            fmt.fmt.pix.width, fmt.fmt.pix.height);

    /* Request MMAP buffers */
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count  = CAPTURE_NUM_BUFFERS;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(ctx->fd, VIDIOC_REQBUFS, &req) == -1) {
        fprintf(stderr, "[capture] VIDIOC_REQBUFS failed: %s\n", strerror(errno));
        goto fail;
    }
    if (req.count < 2) {
        fprintf(stderr, "[capture] insufficient buffer memory\n");
        goto fail;
    }
    ctx->buffer_count = (int)req.count;

    /* Map and queue each buffer */
    for (int i = 0; i < ctx->buffer_count; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = (unsigned int)i;
        if (xioctl(ctx->fd, VIDIOC_QUERYBUF, &buf) == -1) {
            fprintf(stderr, "[capture] VIDIOC_QUERYBUF[%d] failed: %s\n", i, strerror(errno));
            goto fail_unmap;
        }

        ctx->buffers[i].length = buf.length;
        ctx->buffers[i].start  = mmap(NULL, buf.length,
                                      PROT_READ | PROT_WRITE, MAP_SHARED,
                                      ctx->fd, buf.m.offset);
        if (ctx->buffers[i].start == MAP_FAILED) {
            fprintf(stderr, "[capture] mmap[%d] failed: %s\n", i, strerror(errno));
            ctx->buffers[i].start = NULL;
            goto fail_unmap;
        }

        if (xioctl(ctx->fd, VIDIOC_QBUF, &buf) == -1) {
            fprintf(stderr, "[capture] VIDIOC_QBUF[%d] failed: %s\n", i, strerror(errno));
            goto fail_unmap;
        }
    }
    fprintf(stderr, "[capture] %d buffers allocated and queued\n", ctx->buffer_count);

    /* Start streaming */
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(ctx->fd, VIDIOC_STREAMON, &type) == -1) {
        fprintf(stderr, "[capture] VIDIOC_STREAMON failed: %s\n", strerror(errno));
        goto fail_unmap;
    }
    ctx->streaming = true;
    fprintf(stderr, "[capture] streaming started\n");
    return 0;

fail_unmap:
    for (int i = 0; i < ctx->buffer_count; i++) {
        if (ctx->buffers[i].start && ctx->buffers[i].start != MAP_FAILED)
            munmap(ctx->buffers[i].start, ctx->buffers[i].length);
        ctx->buffers[i].start = NULL;
    }
fail:
    close(ctx->fd);
    ctx->fd = -1;
    return -1;
}

int capture_grab(capture_ctx_t *ctx, const uint8_t **jpg_buf, size_t *jpg_len)
{
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (xioctl(ctx->fd, VIDIOC_DQBUF, &buf) == -1) {
        if (errno == EAGAIN)
            return -1;  /* no frame ready yet */
        fprintf(stderr, "[capture] VIDIOC_DQBUF failed: %s\n", strerror(errno));
        return -1;
    }

    *jpg_buf = (const uint8_t *)ctx->buffers[buf.index].start;
    *jpg_len = buf.bytesused;

    /* Re-queue buffer for next capture */
    if (xioctl(ctx->fd, VIDIOC_QBUF, &buf) == -1) {
        fprintf(stderr, "[capture] VIDIOC_QBUF after dequeue failed: %s\n", strerror(errno));
    }

    return 0;
}

void capture_close(capture_ctx_t *ctx)
{
    if (ctx->fd == -1)
        return;

    if (ctx->streaming) {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (xioctl(ctx->fd, VIDIOC_STREAMOFF, &type) == -1)
            fprintf(stderr, "[capture] VIDIOC_STREAMOFF failed: %s\n", strerror(errno));
        ctx->streaming = false;
        fprintf(stderr, "[capture] streaming stopped\n");
    }

    for (int i = 0; i < ctx->buffer_count; i++) {
        if (ctx->buffers[i].start && ctx->buffers[i].start != MAP_FAILED) {
            munmap(ctx->buffers[i].start, ctx->buffers[i].length);
            ctx->buffers[i].start = NULL;
        }
    }

    close(ctx->fd);
    ctx->fd = -1;
    fprintf(stderr, "[capture] closed\n");
}
