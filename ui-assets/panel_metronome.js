// RuneToolsX panel: Metronome (HUD window).
// Spliced inline into client.html at load; IIFE (window exports + registerTab; see the RTX registry in client.html).
//
// Replaces the companion-drawn tick dial. That widget lived in a separate topmost window
// whose hit rect was tested with a bare PtInRect, so it stole clicks from any panel drawn
// over it -- there is no shared z-order between a real HWND and pixels composited into the
// game frame. As a normal HUD window it now sits in the same DOM and the same consume-rect
// list as everything else, so overlap resolves correctly.
//
// TIMING. The beat is 600 ms and the panel sweep runs at 4 Hz, so sampling the tick per
// repaint would jitter by more than a third of a beat. Instead we sample rtx.gameTickState
// (count + ms since that tick) a few times a second and interpolate the phase from the
// page's own clock between samples, resyncing whenever the count moves. Audio stays native:
// the click fires off the confirmed counter edge on the reader's 5 ms poll thread, which is
// tighter than anything reachable from here.
(function () {

  const METRO_TICK_MS = 600;          // one server tick
  let metroTs = null;                 // { count, age, at }  last sample, `at` = performance.now()
  let metroPollAt = 0, metroPolling = false, metroRaf = 0, metroCanvas = null, metroSig = '';

  async function metroPoll() {
    if (!bridge() || !bridge().gameTickState || metroPolling) return;
    const now = performance.now();
    if (now - metroPollAt < 120) return;
    metroPollAt = now;
    metroPolling = true;
    try {
      const r = JSON.parse(await bridge().gameTickState(myPid()) || 'null');
      if (r && r.count >= 0) metroTs = { count: r.count | 0, age: r.age >= 0 ? r.age : 0, at: performance.now() };
      else metroTs = null;
    } catch (e) { metroTs = null; } finally { metroPolling = false; }
  }
  // Kick a sample immediately on open so the dial does not sit blank for a poll interval.
  function metroTickKick() { metroPollAt = 0; metroPoll(); }

  // Phase within the beat, 0..1, where 0 is the beat landing on 12 o'clock. null = no data.
  function metroPhase() {
    if (!metroTs) return null;
    const iv = Math.max(1, Math.min(6, metroInterval | 0));
    const age = metroTs.age + (performance.now() - metroTs.at);
    // Clamp the interpolation to one tick: if the count has not moved by then the game is
    // paused, the client is loading, or we lost the counter -- freezing at the top of the
    // beat is honest, whereas free-running would invent ticks that never happened.
    const frac = Math.min(age / METRO_TICK_MS, 1);
    return (((metroTs.count % iv) + frac) / iv) % 1;
  }

  function metroDraw() {
    metroRaf = 0;
    const cv = metroCanvas;
    if (!cv || !paneVisible('metronome')) { metroCanvas = null; return; }
    metroPoll();                                   // self-throttled
    const host = cv.parentElement;
    const cw = host ? host.clientWidth : 0, ch = host ? host.clientHeight : 0;
    if (cw > 0 && ch > 0) {
      const dpr = window.devicePixelRatio || 1;
      const pw = Math.round(cw * dpr), ph = Math.round(ch * dpr);
      const resized = (cv.width !== pw || cv.height !== ph);
      // Decide whether to draw BEFORE touching the canvas. Clearing first and then bailing
      // out erases the dial for that frame, which reads as a flicker -- and at a beat of 4+
      // the phase moves so little per frame that the skip fires most frames, so it flickered
      // constantly. Resizing also blanks the canvas, so it always forces a redraw.
      const phSig = metroPhase();
      const sig = cw + 'x' + ch + '|' + (phSig === null ? 'n' : phSig.toFixed(4));
      if (!resized && sig === metroSig) { metroRaf = requestAnimationFrame(metroDraw); return; }
      metroSig = sig;
      if (resized) { cv.width = pw; cv.height = ph; }
      cv.style.width = cw + 'px'; cv.style.height = ch + 'px';
      const g = cv.getContext('2d');
      g.setTransform(dpr, 0, 0, dpr, 0, 0);
      g.clearRect(0, 0, cw, ch);
      const cx = cw / 2, cy = ch / 2, r = Math.max(8, Math.min(cw, ch) / 2 - 2);
      const TAU = Math.PI * 2, TOP = -Math.PI / 2;

      g.beginPath(); g.arc(cx, cy, r, 0, TAU);
      g.fillStyle = 'rgba(10,12,20,0.62)'; g.fill();

      // Ring ticks: 28 marks, same cadence the companion widget used.
      g.strokeStyle = 'rgba(70,80,110,0.72)'; g.lineWidth = 1;
      for (let i = 0; i < 28; i++) {
        const a = TOP + (i / 28) * TAU, i0 = r - 1, i1 = r - 4;
        g.beginPath();
        g.moveTo(cx + Math.cos(a) * i1, cy + Math.sin(a) * i1);
        g.lineTo(cx + Math.cos(a) * i0, cy + Math.sin(a) * i0);
        g.stroke();
      }

      const ph2 = phSig;
      if (ph2 === null) {
        g.beginPath(); g.arc(cx, cy, r / 3, 0, TAU);
        g.fillStyle = 'rgba(130,130,200,0.35)'; g.fill();
      } else {
        g.beginPath(); g.arc(cx, cy, r - 6, TOP, TOP + TAU * ph2);
        g.strokeStyle = (typeof accentRgba === 'function') ? accentRgba(0.88) : 'rgba(var(--accent-rgb),0.88)'; g.lineWidth = Math.max(2, r * 0.09);
        g.lineCap = 'round'; g.stroke();
        // Core flash: brightest as the beat lands, decaying quadratically across it.
        const f = (1 - ph2) * (1 - ph2);
        g.beginPath(); g.arc(cx, cy, r * 0.42, 0, TAU);
        g.fillStyle = 'rgba(170,140,255,' + (0.82 * f).toFixed(3) + ')'; g.fill();
        const a2 = TOP + TAU * ph2;
        g.beginPath(); g.moveTo(cx, cy);
        g.lineTo(cx + Math.cos(a2) * (r - 8), cy + Math.sin(a2) * (r - 8));
        g.strokeStyle = 'rgba(255,255,255,0.92)'; g.lineWidth = Math.max(1.5, r * 0.055);
        g.lineCap = 'round'; g.stroke();
      }
    }
    metroRaf = requestAnimationFrame(metroDraw);
  }

  function renderMetronome() {
    const c = $('content');
    let cv = c.querySelector('#metroDial');
    if (!cv) {
      c.innerHTML = '';
      const wrap = document.createElement('div');
      wrap.style.cssText = 'width:100%;height:100%;display:block;position:relative';
      cv = document.createElement('canvas');
      cv.id = 'metroDial';
      cv.style.cssText = 'display:block;width:100%;height:100%';
      wrap.appendChild(cv);
      c.appendChild(wrap);
    }
    metroCanvas = cv;
    metroTickKick();
    // One loop at a time: reopening the window must not stack a second rAF chain.
    if (!metroRaf) metroRaf = requestAnimationFrame(metroDraw);
  }

// ---- IIFE exports (generated by panel_iife.py: only names other files use) ----
Object.assign(window, { renderMetronome });
registerTab({ id: 'metronome', render: renderMetronome, open: function () { applyMetroOverlay(); metroTickKick(); } });
})();
