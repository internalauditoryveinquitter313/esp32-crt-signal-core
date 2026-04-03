(function () {
  "use strict";

  /* ------------------------------------------------------------------ */
  /* State                                                                */
  /* ------------------------------------------------------------------ */
  var ws = null;
  var prevBlobUrl = null;
  var frameCount = 0;
  var activeTab = "live";
  var statusTimer = null;

  /* Gallery state */
  var galleryItems = [];   /* [{file, size, mtime}, ...] */
  var lbIndex = 0;

  /* ------------------------------------------------------------------ */
  /* DOM refs (populated after DOMContentLoaded)                         */
  /* ------------------------------------------------------------------ */
  var liveImg, feedOverlay, btnCapture, wsStatus, frameCounter;
  var galleryGrid, lightbox, lbImg, lbName, lbPrev, lbNext, lbClose, lbDelete;
  var statusReadout;

  /* ------------------------------------------------------------------ */
  /* Helpers                                                              */
  /* ------------------------------------------------------------------ */
  function pad(str, width) {
    str = String(str);
    while (str.length < width) str += " ";
    return str;
  }

  function formatUptime(s) {
    s = Math.max(0, Math.floor(s));
    var h = Math.floor(s / 3600);
    var m = Math.floor((s % 3600) / 60);
    var sec = s % 60;
    return h + "h " + m + "m " + sec + "s";
  }

  /* ------------------------------------------------------------------ */
  /* Tab navigation                                                       */
  /* ------------------------------------------------------------------ */
  function initTabs() {
    var tabs = document.querySelectorAll(".tab");
    var panes = document.querySelectorAll(".tab-pane");

    tabs.forEach(function (tab) {
      tab.addEventListener("click", function () {
        var target = tab.getAttribute("data-tab");

        tabs.forEach(function (t) { t.classList.remove("active"); });
        panes.forEach(function (p) { p.classList.remove("active"); });

        tab.classList.add("active");
        var pane = document.querySelector('.tab-pane[data-tab="' + target + '"]');
        if (pane) pane.classList.add("active");

        activeTab = target;

        if (target === "gallery") {
          loadGallery();
        } else if (target === "status") {
          loadStatus();
          startStatusTimer();
        } else {
          stopStatusTimer();
        }
      });
    });
  }

  /* ------------------------------------------------------------------ */
  /* WebSocket live feed                                                  */
  /* ------------------------------------------------------------------ */
  function wsConnect() {
    var proto = location.protocol === "https:" ? "wss:" : "ws:";
    var url = proto + "//" + location.host + "/ws/live";

    ws = new WebSocket(url);
    ws.binaryType = "arraybuffer";

    ws.onopen = function () {
      wsStatus.textContent = "CONNECTED";
      wsStatus.style.color = "#00ff41";
      if (feedOverlay) feedOverlay.style.display = "none";
    };

    ws.onmessage = function (evt) {
      var blob = new Blob([evt.data], { type: "image/jpeg" });
      var url = URL.createObjectURL(blob);

      if (prevBlobUrl) URL.revokeObjectURL(prevBlobUrl);
      prevBlobUrl = url;

      liveImg.src = url;

      frameCount++;
      frameCounter.textContent = frameCount;
    };

    ws.onclose = function () {
      wsStatus.textContent = "DISCONNECTED";
      wsStatus.style.color = "#ff4141";
      if (feedOverlay) {
        feedOverlay.style.display = "flex";
        feedOverlay.textContent = "RECONNECTING...";
      }
      setTimeout(wsConnect, 2000);
    };

    ws.onerror = function () {
      ws.close();
    };
  }

  /* ------------------------------------------------------------------ */
  /* Capture button                                                       */
  /* ------------------------------------------------------------------ */
  function initCapture() {
    btnCapture.addEventListener("click", function () {
      btnCapture.disabled = true;
      btnCapture.textContent = "[ CAPTURING... ]";

      fetch("/api/capture", { method: "POST" })
        .then(function (res) {
          return res.json().then(function (data) {
            return { ok: res.ok, data: data };
          });
        })
        .then(function (result) {
          if (result.ok && result.data.ok) {
            btnCapture.textContent = "[ SAVED: " + result.data.file + " ]";
            btnCapture.classList.add("flash");
            setTimeout(function () {
              btnCapture.classList.remove("flash");
            }, 600);
          } else {
            var msg = (result.data && result.data.error) ? result.data.error : "unknown error";
            btnCapture.textContent = "[ ERROR: " + msg + " ]";
          }
        })
        .catch(function (err) {
          btnCapture.textContent = "[ ERROR: " + err.message + " ]";
        })
        .finally(function () {
          setTimeout(function () {
            btnCapture.textContent = "[ CAPTURE ]";
            btnCapture.disabled = false;
          }, 1500);
        });
    });
  }

  /* ------------------------------------------------------------------ */
  /* Gallery                                                              */
  /* ------------------------------------------------------------------ */
  function loadGallery() {
    fetch("/api/gallery")
      .then(function (res) { return res.json(); })
      .then(function (items) {
        /* sort by mtime descending */
        items.sort(function (a, b) { return b.mtime - a.mtime; });
        galleryItems = items;
        renderGallery();
      })
      .catch(function (err) {
        galleryGrid.textContent = "ERROR: " + err.message;
      });
  }

  function renderGallery() {
    galleryGrid.innerHTML = "";

    if (galleryItems.length === 0) {
      var empty = document.createElement("p");
      empty.textContent = "NO CAPTURES YET";
      empty.className = "gallery-empty";
      galleryGrid.appendChild(empty);
      return;
    }

    galleryItems.forEach(function (item, idx) {
      var img = document.createElement("img");
      img.src = "/captures/" + item.file;
      img.alt = item.file;
      img.loading = "lazy";
      img.className = "gallery-thumb";
      img.addEventListener("click", function () { openLightbox(idx); });
      galleryGrid.appendChild(img);
    });
  }

  /* ------------------------------------------------------------------ */
  /* Lightbox                                                             */
  /* ------------------------------------------------------------------ */
  function openLightbox(index) {
    lbIndex = index;
    updateLightbox();
    lightbox.style.display = "flex";
  }

  function closeLightbox() {
    lightbox.style.display = "none";
  }

  function updateLightbox() {
    var item = galleryItems[lbIndex];
    lbImg.src = "/captures/" + item.file;
    lbName.textContent = item.file;
    lbPrev.disabled = lbIndex <= 0;
    lbNext.disabled = lbIndex >= galleryItems.length - 1;
  }

  function initLightbox() {
    lbPrev.addEventListener("click", function () {
      if (lbIndex > 0) { lbIndex--; updateLightbox(); }
    });

    lbNext.addEventListener("click", function () {
      if (lbIndex < galleryItems.length - 1) { lbIndex++; updateLightbox(); }
    });

    lbClose.addEventListener("click", closeLightbox);

    /* Close on backdrop click */
    lightbox.addEventListener("click", function (e) {
      if (e.target === lightbox) closeLightbox();
    });

    /* Keyboard nav */
    document.addEventListener("keydown", function (e) {
      if (lightbox.style.display !== "flex") return;
      if (e.key === "ArrowLeft" && lbIndex > 0) { lbIndex--; updateLightbox(); }
      if (e.key === "ArrowRight" && lbIndex < galleryItems.length - 1) { lbIndex++; updateLightbox(); }
      if (e.key === "Escape") closeLightbox();
    });

    /* Swipe */
    var touchStartX = 0;
    lbImg.addEventListener("touchstart", function (e) {
      touchStartX = e.changedTouches[0].clientX;
    }, { passive: true });
    lbImg.addEventListener("touchend", function (e) {
      var dx = e.changedTouches[0].clientX - touchStartX;
      if (Math.abs(dx) < 50) return;
      if (dx < 0 && lbIndex < galleryItems.length - 1) { lbIndex++; updateLightbox(); }
      if (dx > 0 && lbIndex > 0) { lbIndex--; updateLightbox(); }
    }, { passive: true });

    /* Delete */
    lbDelete.addEventListener("click", function () {
      var item = galleryItems[lbIndex];
      if (!confirm("Delete " + item.file + "?")) return;

      fetch("/api/gallery/" + encodeURIComponent(item.file), { method: "DELETE" })
        .then(function (res) { return res.json(); })
        .then(function (data) {
          if (data.ok) {
            closeLightbox();
            loadGallery();
          } else {
            alert("Delete failed: " + (data.error || "unknown"));
          }
        })
        .catch(function (err) {
          alert("Delete error: " + err.message);
        });
    });
  }

  /* ------------------------------------------------------------------ */
  /* Status readout                                                       */
  /* ------------------------------------------------------------------ */
  function loadStatus() {
    fetch("/api/status")
      .then(function (res) { return res.json(); })
      .then(renderStatus)
      .catch(function (err) {
        statusReadout.textContent = "ERROR: " + err.message;
      });
  }

  function renderStatus(d) {
    var W = 42; /* inner width between ║ borders */

    function row(label, value) {
      var content = "  " + pad(label, 10) + pad(value, W - 12 - 2);
      return "║" + pad(content, W) + "║\n";
    }

    var lines = "";
    lines += "╔" + repeat("═", W) + "╗\n";
    lines += "║" + pad("   CRT SIGNAL MONITOR v0.1", W) + "║\n";
    lines += "╠" + repeat("═", W) + "╣\n";
    lines += row("VIDEO:",    d.video_standard  || "N/A");
    lines += row("COLOR:",    d.color_enabled ? "ENABLED" : "DISABLED");
    lines += row("LINES:",    String(d.active_lines   || 0));
    lines += row("SAMPLE:",   fmtHz(d.sample_rate_hz  || 0));
    lines += row("DEVICE:",   d.capture_device  || "N/A");
    lines += row("RESOLUT:",  d.capture_resolution || "N/A");
    lines += row("UPTIME:",   formatUptime(d.uptime_s || 0));
    lines += "╚" + repeat("═", W) + "╝\n";

    statusReadout.innerHTML = escHtml(lines) +
      "\n&gt; READY_<span class=\"cursor\">\u2588</span>";
  }

  function fmtHz(hz) {
    if (hz >= 1e6) return (hz / 1e6).toFixed(3) + " MHz";
    if (hz >= 1e3) return (hz / 1e3).toFixed(1) + " kHz";
    return hz + " Hz";
  }

  function repeat(ch, n) {
    var s = "";
    for (var i = 0; i < n; i++) s += ch;
    return s;
  }

  function escHtml(str) {
    return str
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;");
  }

  function startStatusTimer() {
    stopStatusTimer();
    statusTimer = setInterval(function () {
      if (activeTab === "status") loadStatus();
    }, 5000);
  }

  function stopStatusTimer() {
    if (statusTimer) {
      clearInterval(statusTimer);
      statusTimer = null;
    }
  }

  /* ------------------------------------------------------------------ */
  /* Boot                                                                 */
  /* ------------------------------------------------------------------ */
  document.addEventListener("DOMContentLoaded", function () {
    liveImg       = document.getElementById("live-img");
    feedOverlay   = document.getElementById("feed-overlay");
    btnCapture    = document.getElementById("btn-capture");
    wsStatus      = document.getElementById("ws-status");
    frameCounter  = document.getElementById("frame-counter");

    galleryGrid   = document.getElementById("gallery-grid");
    lightbox      = document.getElementById("lightbox");
    lbImg         = document.getElementById("lightbox-img");
    lbName        = document.getElementById("lb-name");
    lbPrev        = document.getElementById("lb-prev");
    lbNext        = document.getElementById("lb-next");
    lbClose       = document.getElementById("lb-close");
    lbDelete      = document.getElementById("lb-delete");

    statusReadout = document.getElementById("status-readout");

    initTabs();
    initCapture();
    initLightbox();
    wsConnect();
  });

})();
