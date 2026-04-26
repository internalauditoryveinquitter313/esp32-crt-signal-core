CC = gcc
CFLAGS = -Wall -Wextra -Wno-unused-parameter -O2 -std=gnu11 -D_GNU_SOURCE -I. -DMG_ENABLE_LINES=0 \
         -DMG_MAX_RECV_SIZE='(15UL * 1024UL * 1024UL)'
LDFLAGS = -lpthread

# Capture backend: v4l2 (default) or rtsp
CAPTURE ?= v4l2

ifeq ($(CAPTURE),rtsp)
  CAPTURE_SRC = capture_rtsp.c
else
  CAPTURE_SRC = capture.c
endif

SRCS = main.c $(CAPTURE_SRC) routes.c status.c mongoose.c
TARGET = crt_monitor

.PHONY: all clean help

all: $(TARGET)

$(TARGET): $(SRCS) capture.h routes.h status.h
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)

clean:
	rm -f $(TARGET)

help:
	@echo "CRT Monitor Dashboard"
	@echo ""
	@echo "  make                 Build with V4L2 webcam (default)"
	@echo "  make CAPTURE=rtsp    Build with RTSP IP camera"
	@echo "  make clean           Remove binary"
	@echo ""
	@echo "  ./crt_monitor [options]"
	@echo "    -p PORT     Listen port (default: 8080)"
	@echo "    -d DEVICE   V4L2 device or RTSP URL (default: /dev/video0)"
	@echo "    -c DIR      Captures dir (default: ./captures)"
	@echo "    -s DIR      Static dir (default: ./static)"
	@echo ""
	@echo "  RTSP example:"
	@echo "    make CAPTURE=rtsp"
	@echo "    ./crt_monitor -d 'rtsp://admin:pass@192.168.0.100:554/onvif1'"
