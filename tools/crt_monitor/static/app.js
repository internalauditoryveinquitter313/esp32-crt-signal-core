(function () {
  "use strict";

  /* ── State ─────────────────────────────────────────────────────── */
  var ws = null, prevBlobUrl = null, frameCount = 0, activeTab = "live";
  var statusTimer = null;
  var galleryItems = [], lbIndex = 0;

  /* Luminance memory — ring buffer */
  var LUMA_HISTORY = 300; /* ~10s at 30fps WS throttle */
  var lumaHistory = [];
  var prevLuma = null;
  var fpsFrames = 0, fpsTick = 0, fpsVal = 0;

  /* Hidden image for decoding WS JPEG */
  var hiddenImg = new Image();

  /* ── DOM refs ──────────────────────────────────────────────────── */
  var canvas, ctx, feedOverlay, statsOverlay;
  var statLuma, statDelta, statFps;
  var filterSelect, slBrightness, slContrast, valBrightness, valContrast;
  var lumaChart, lumaCtx;
  var btnCapture, btnRecord, btnAnalyze, recStatus;
  var wsStatus, frameCounter;
  var galleryGrid, lightbox, lbImg, lbName, lbPrev, lbNext, lbClose, lbDelete;
  var statusReadout;

  /* ── Helpers ────────────────────────────────────────────────────── */
  function pad(s, w) { s = String(s); while (s.length < w) s += " "; return s; }
  function formatUptime(s) {
    s = Math.max(0, Math.floor(s));
    return Math.floor(s/3600) + "h " + Math.floor((s%3600)/60) + "m " + (s%60) + "s";
  }
  function repeat(ch, n) { var s = ""; for (var i = 0; i < n; i++) s += ch; return s; }
  function escHtml(s) { return s.replace(/&/g,"&amp;").replace(/</g,"&lt;").replace(/>/g,"&gt;"); }
  function fmtHz(hz) {
    if (hz >= 1e6) return (hz/1e6).toFixed(3) + " MHz";
    if (hz >= 1e3) return (hz/1e3).toFixed(1) + " kHz";
    return hz + " Hz";
  }

  /* ── Pixel filters ─────────────────────────────────────────────── */

  function applyBrightnessContrast(data, bri, con) {
    var f = (259 * (con + 255)) / (255 * (259 - con));
    for (var i = 0; i < data.length; i += 4) {
      data[i]   = Math.max(0, Math.min(255, f * (data[i]   - 128) + 128 + bri));
      data[i+1] = Math.max(0, Math.min(255, f * (data[i+1] - 128) + 128 + bri));
      data[i+2] = Math.max(0, Math.min(255, f * (data[i+2] - 128) + 128 + bri));
    }
  }

  function filterGrayscale(data) {
    for (var i = 0; i < data.length; i += 4) {
      var y = 0.2126 * data[i] + 0.7152 * data[i+1] + 0.0722 * data[i+2];
      data[i] = data[i+1] = data[i+2] = y;
    }
  }

  function filterInvert(data) {
    for (var i = 0; i < data.length; i += 4) {
      data[i] = 255 - data[i]; data[i+1] = 255 - data[i+1]; data[i+2] = 255 - data[i+2];
    }
  }

  function filterFalseColor(data) {
    for (var i = 0; i < data.length; i += 4) {
      var y = (0.2126 * data[i] + 0.7152 * data[i+1] + 0.0722 * data[i+2]) / 255;
      /* Viridis-like: black → blue → green → yellow → white */
      if (y < 0.25) {
        data[i] = 0; data[i+1] = y*4*80; data[i+2] = 60 + y*4*140;
      } else if (y < 0.5) {
        var t = (y - 0.25) * 4;
        data[i] = 0; data[i+1] = 80 + t*175; data[i+2] = 200 - t*200;
      } else if (y < 0.75) {
        var t = (y - 0.5) * 4;
        data[i] = t*255; data[i+1] = 255; data[i+2] = 0;
      } else {
        var t = (y - 0.75) * 4;
        data[i] = 255; data[i+1] = 255; data[i+2] = t*255;
      }
    }
  }

  function filterEdge(imgData) {
    var w = imgData.width, h = imgData.height;
    var src = new Uint8ClampedArray(imgData.data);
    var dst = imgData.data;
    for (var y = 1; y < h - 1; y++) {
      for (var x = 1; x < w - 1; x++) {
        var idx = (y * w + x) * 4;
        /* Sobel on luminance */
        var tl = src[((y-1)*w+(x-1))*4], tc = src[((y-1)*w+x)*4], tr = src[((y-1)*w+(x+1))*4];
        var ml = src[(y*w+(x-1))*4],                                mr = src[(y*w+(x+1))*4];
        var bl = src[((y+1)*w+(x-1))*4], bc = src[((y+1)*w+x)*4], br = src[((y+1)*w+(x+1))*4];
        var gx = -tl + tr - 2*ml + 2*mr - bl + br;
        var gy = -tl - 2*tc - tr + bl + 2*bc + br;
        var mag = Math.min(255, Math.sqrt(gx*gx + gy*gy));
        dst[idx] = dst[idx+1] = mag; dst[idx+2] = Math.min(255, mag * 1.5); dst[idx+3] = 255;
      }
    }
  }

  function filterThreshold(data) {
    for (var i = 0; i < data.length; i += 4) {
      var y = 0.2126 * data[i] + 0.7152 * data[i+1] + 0.0722 * data[i+2];
      var v = y > 128 ? 255 : 0;
      data[i] = data[i+1] = data[i+2] = v;
    }
  }

  /* ── Compute luminance stats from canvas ────────────────────────── */

  function computeLumaStats(imgData) {
    var data = imgData.data, n = data.length / 4;
    var sum = 0;
    for (var i = 0; i < data.length; i += 4) {
      sum += 0.2126 * data[i] + 0.7152 * data[i+1] + 0.0722 * data[i+2];
    }
    var mean = sum / n / 255;
    var delta = prevLuma !== null ? Math.abs(mean - prevLuma) : 0;
    prevLuma = mean;

    lumaHistory.push(mean);
    if (lumaHistory.length > LUMA_HISTORY) lumaHistory.shift();

    return { mean: mean, delta: delta };
  }

  /* ── Draw luminance chart ──────────────────────────────────────── */

  function drawLumaChart() {
    var w = lumaChart.width, h = lumaChart.height;
    lumaCtx.fillStyle = "#0a0a0a";
    lumaCtx.fillRect(0, 0, w, h);

    if (lumaHistory.length < 2) return;

    /* Grid */
    lumaCtx.strokeStyle = "#1a1a1a";
    lumaCtx.lineWidth = 0.5;
    for (var g = 0.25; g < 1; g += 0.25) {
      var gy = h - g * h;
      lumaCtx.beginPath(); lumaCtx.moveTo(0, gy); lumaCtx.lineTo(w, gy); lumaCtx.stroke();
    }

    /* Line */
    var step = w / (LUMA_HISTORY - 1);
    var offset = LUMA_HISTORY - lumaHistory.length;
    lumaCtx.beginPath();
    lumaCtx.strokeStyle = "#00ff88";
    lumaCtx.lineWidth = 1.5;
    lumaCtx.shadowColor = "rgba(0,255,136,0.4)";
    lumaCtx.shadowBlur = 4;
    for (var i = 0; i < lumaHistory.length; i++) {
      var x = (offset + i) * step;
      var y = h - lumaHistory[i] * h;
      if (i === 0) lumaCtx.moveTo(x, y); else lumaCtx.lineTo(x, y);
    }
    lumaCtx.stroke();
    lumaCtx.shadowBlur = 0;

    /* Label */
    lumaCtx.fillStyle = "#00aa66";
    lumaCtx.font = "9px monospace";
    lumaCtx.fillText("LUMINANCE", 4, 10);
    lumaCtx.fillText(lumaHistory[lumaHistory.length-1].toFixed(3), w - 40, 10);
  }

  /* ── Process frame ─────────────────────────────────────────────── */

  function processFrame() {
    if (!canvas || !ctx) return;
    canvas.width = hiddenImg.naturalWidth || hiddenImg.width;
    canvas.height = hiddenImg.naturalHeight || hiddenImg.height;
    ctx.drawImage(hiddenImg, 0, 0);

    var imgData = ctx.getImageData(0, 0, canvas.width, canvas.height);
    var data = imgData.data;

    /* Brightness / Contrast */
    var bri = parseInt(slBrightness.value, 10);
    var con = parseInt(slContrast.value, 10);
    if (bri !== 0 || con !== 0) {
      applyBrightnessContrast(data, bri, con);
    }

    /* Filter */
    var filter = filterSelect.value;
    switch (filter) {
      case "grayscale":  filterGrayscale(data); break;
      case "invert":     filterInvert(data); break;
      case "falsecolor": filterFalseColor(data); break;
      case "edge":       filterEdge(imgData); break;
      case "threshold":  filterThreshold(data); break;
    }

    ctx.putImageData(imgData, 0, 0);

    /* Stats */
    var stats = computeLumaStats(imgData);
    statLuma.textContent = "Y:" + stats.mean.toFixed(3);
    statDelta.textContent = "\u0394:" + stats.delta.toFixed(4);

    /* FPS */
    fpsFrames++;
    var now = performance.now();
    if (now - fpsTick > 1000) {
      fpsVal = fpsFrames;
      fpsFrames = 0;
      fpsTick = now;
    }
    statFps.textContent = "FPS:" + fpsVal;

    drawLumaChart();
  }

  /* ── Tabs ──────────────────────────────────────────────────────── */

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
        if (target === "gallery") loadGallery();
        else if (target === "status") { loadStatus(); startStatusTimer(); }
        else stopStatusTimer();
      });
    });
  }

  /* ── WebSocket ─────────────────────────────────────────────────── */

  function wsConnect() {
    var proto = location.protocol === "https:" ? "wss:" : "ws:";
    ws = new WebSocket(proto + "//" + location.host + "/ws/live");
    ws.binaryType = "arraybuffer";

    ws.onopen = function () {
      wsStatus.textContent = "CONNECTED";
      wsStatus.style.color = "#00ff41";
      feedOverlay.style.display = "none";
    };

    ws.onmessage = function (evt) {
      var blob = new Blob([evt.data], { type: "image/jpeg" });
      var url = URL.createObjectURL(blob);
      if (prevBlobUrl) URL.revokeObjectURL(prevBlobUrl);
      prevBlobUrl = url;
      hiddenImg.src = url;
      frameCount++;
      frameCounter.textContent = frameCount;
    };

    ws.onclose = function () {
      wsStatus.textContent = "DISCONNECTED";
      wsStatus.style.color = "#ff4141";
      feedOverlay.style.display = "flex";
      feedOverlay.textContent = "RECONNECTING...";
      setTimeout(wsConnect, 2000);
    };

    ws.onerror = function () { ws.close(); };
  }

  /* ── Capture ───────────────────────────────────────────────────── */

  function initCapture() {
    btnCapture.addEventListener("click", function () {
      btnCapture.disabled = true;
      btnCapture.textContent = "...";
      fetch("/api/capture", { method: "POST" }).then(function (r) { return r.json(); })
        .then(function (d) {
          btnCapture.textContent = d.ok ? "SAVED" : "ERR";
          setTimeout(function () { btnCapture.textContent = "CAPTURE"; btnCapture.disabled = false; }, 1500);
        }).catch(function () {
          btnCapture.textContent = "ERR"; btnCapture.disabled = false;
        });
    });
  }

  /* ── Record ────────────────────────────────────────────────────── */

  function initRecord() {
    btnRecord.addEventListener("click", function () {
      btnRecord.disabled = true;
      btnRecord.textContent = "REC...";
      recStatus.textContent = "RECORDING";
      recStatus.style.color = "#ff3333";

      fetch("/api/record?duration=5", { method: "POST" }).then(function (r) { return r.json(); })
        .then(function (d) {
          if (!d.ok) { btnRecord.textContent = "ERR"; btnRecord.disabled = false; return; }
          /* Poll status */
          var poll = setInterval(function () {
            fetch("/api/record").then(function (r) { return r.json(); })
              .then(function (s) {
                recStatus.textContent = "REC " + s.frames_saved + "/" + (s.frames_saved + s.frames_left);
                if (!s.recording) {
                  clearInterval(poll);
                  recStatus.textContent = "DONE: " + s.frames_saved + " frames";
                  recStatus.style.color = "#00ff41";
                  btnRecord.textContent = "REC 5s";
                  btnRecord.disabled = false;
                  setTimeout(function () { recStatus.textContent = ""; }, 3000);
                }
              });
          }, 500);
        }).catch(function () { btnRecord.textContent = "ERR"; btnRecord.disabled = false; });
    });
  }

  /* ── R Analyze ─────────────────────────────────────────────────── */

  function initAnalyze() {
    btnAnalyze.addEventListener("click", function () {
      btnAnalyze.disabled = true;
      btnAnalyze.textContent = "RUNNING R...";

      /* Get latest recording dir */
      fetch("/api/record").then(function (r) { return r.json(); })
        .then(function (s) {
          if (!s.dir) {
            btnAnalyze.textContent = "NO REC";
            setTimeout(function () { btnAnalyze.textContent = "R ANALYZE"; btnAnalyze.disabled = false; }, 2000);
            return;
          }
          /* POST to trigger R analysis (we'll add this endpoint) */
          fetch("/api/analyze?dir=" + encodeURIComponent(s.dir), { method: "POST" })
            .then(function (r) { return r.json(); })
            .then(function (d) {
              btnAnalyze.textContent = d.ok ? "DONE" : "ERR";
              setTimeout(function () { btnAnalyze.textContent = "R ANALYZE"; btnAnalyze.disabled = false; }, 2000);
              if (d.ok) loadGallery(); /* refresh to show analysis images */
            }).catch(function () {
              btnAnalyze.textContent = "ERR"; btnAnalyze.disabled = false;
            });
        });
    });
  }

  /* ── Gallery ───────────────────────────────────────────────────── */

  function loadGallery() {
    fetch("/api/gallery").then(function (r) { return r.json(); })
      .then(function (items) {
        items.sort(function (a, b) { return b.mtime - a.mtime; });
        galleryItems = items;
        renderGallery();
      }).catch(function (e) { galleryGrid.textContent = "ERROR: " + e.message; });
  }

  function renderGallery() {
    galleryGrid.innerHTML = "";
    if (galleryItems.length === 0) {
      galleryGrid.innerHTML = '<p class="gallery-empty">NO CAPTURES YET</p>';
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

  /* ── Lightbox ──────────────────────────────────────────────────── */

  function openLightbox(i) { lbIndex = i; updateLightbox(); lightbox.style.display = "flex"; }
  function closeLightbox() { lightbox.style.display = "none"; }
  function updateLightbox() {
    var item = galleryItems[lbIndex];
    lbImg.src = "/captures/" + item.file;
    lbName.textContent = item.file;
    lbPrev.disabled = lbIndex <= 0;
    lbNext.disabled = lbIndex >= galleryItems.length - 1;
  }

  function initLightbox() {
    lbPrev.addEventListener("click", function () { if (lbIndex > 0) { lbIndex--; updateLightbox(); } });
    lbNext.addEventListener("click", function () { if (lbIndex < galleryItems.length-1) { lbIndex++; updateLightbox(); } });
    lbClose.addEventListener("click", closeLightbox);
    lightbox.addEventListener("click", function (e) { if (e.target === lightbox) closeLightbox(); });
    document.addEventListener("keydown", function (e) {
      if (lightbox.style.display !== "flex") return;
      if (e.key === "ArrowLeft" && lbIndex > 0) { lbIndex--; updateLightbox(); }
      if (e.key === "ArrowRight" && lbIndex < galleryItems.length-1) { lbIndex++; updateLightbox(); }
      if (e.key === "Escape") closeLightbox();
    });
    var tx = 0;
    lbImg.addEventListener("touchstart", function (e) { tx = e.changedTouches[0].clientX; }, {passive:true});
    lbImg.addEventListener("touchend", function (e) {
      var dx = e.changedTouches[0].clientX - tx;
      if (Math.abs(dx) < 50) return;
      if (dx < 0 && lbIndex < galleryItems.length-1) { lbIndex++; updateLightbox(); }
      if (dx > 0 && lbIndex > 0) { lbIndex--; updateLightbox(); }
    }, {passive:true});
    lbDelete.addEventListener("click", function () {
      var item = galleryItems[lbIndex];
      fetch("/api/gallery/" + encodeURIComponent(item.file), {method:"DELETE"})
        .then(function (r) { return r.json(); })
        .then(function (d) { if (d.ok) { closeLightbox(); loadGallery(); } });
    });
  }

  /* ── Status ────────────────────────────────────────────────────── */

  function loadStatus() {
    fetch("/api/status").then(function (r) { return r.json(); }).then(renderStatus)
      .catch(function (e) { statusReadout.textContent = "ERROR: " + e.message; });
  }
  function renderStatus(d) {
    var W = 42;
    function row(l, v) { var c = "  " + pad(l,10) + pad(v, W-12-2); return "\u2551" + pad(c,W) + "\u2551\n"; }
    var s = "";
    s += "\u2554" + repeat("\u2550",W) + "\u2557\n";
    s += "\u2551" + pad("   CRT SIGNAL MONITOR v0.2",W) + "\u2551\n";
    s += "\u2560" + repeat("\u2550",W) + "\u2563\n";
    s += row("VIDEO:", d.video_standard||"N/A");
    s += row("COLOR:", d.color_enabled?"ENABLED":"DISABLED");
    s += row("LINES:", String(d.active_lines||0));
    s += row("SAMPLE:", fmtHz(d.sample_rate_hz||0));
    s += row("DEVICE:", d.capture_device||"N/A");
    s += row("RESOLUT:", d.capture_resolution||"N/A");
    s += row("UPTIME:", formatUptime(d.uptime_s||0));
    s += "\u255A" + repeat("\u2550",W) + "\u255D\n";
    statusReadout.innerHTML = escHtml(s) + "\n&gt; READY_<span class=\"cursor\">\u2588</span>";
  }
  function startStatusTimer() { stopStatusTimer(); statusTimer = setInterval(function () { if (activeTab==="status") loadStatus(); }, 5000); }
  function stopStatusTimer() { if (statusTimer) { clearInterval(statusTimer); statusTimer = null; } }

  /* ── Filter sliders ────────────────────────────────────────────── */

  function initFilters() {
    slBrightness.addEventListener("input", function () { valBrightness.textContent = slBrightness.value; });
    slContrast.addEventListener("input", function () { valContrast.textContent = slContrast.value; });
  }

  /* ── Boot ──────────────────────────────────────────────────────── */

  document.addEventListener("DOMContentLoaded", function () {
    canvas        = document.getElementById("live-canvas");
    ctx           = canvas.getContext("2d");
    feedOverlay   = document.getElementById("feed-overlay");
    statsOverlay  = document.getElementById("stats-overlay");
    statLuma      = document.getElementById("stat-luma");
    statDelta     = document.getElementById("stat-delta");
    statFps       = document.getElementById("stat-fps");
    filterSelect  = document.getElementById("filter-select");
    slBrightness  = document.getElementById("sl-brightness");
    slContrast    = document.getElementById("sl-contrast");
    valBrightness = document.getElementById("val-brightness");
    valContrast   = document.getElementById("val-contrast");
    lumaChart     = document.getElementById("luma-chart");
    lumaCtx       = lumaChart.getContext("2d");
    btnCapture    = document.getElementById("btn-capture");
    btnRecord     = document.getElementById("btn-record");
    btnAnalyze    = document.getElementById("btn-analyze");
    recStatus     = document.getElementById("rec-status");
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

    /* Set luma chart width to match container */
    lumaChart.width = lumaChart.parentElement.clientWidth || 320;

    hiddenImg.onload = processFrame;

    initTabs();
    initFilters();
    initCapture();
    initRecord();
    initAnalyze();
    initLightbox();
    wsConnect();

    fpsTick = performance.now();
  });

})();
