/* GBA DS web player: plays the app's sound. Chunks of 16-bit stereo come in
 * from the worker (over the port the page hands over); what's played is asked
 * for again (`need`), so the app's own sound queue is what sets the pace.
 * Resampled (linearly) when the app's rate isn't the page's.
 *
 * GbadsRing does the work. In an AudioWorklet (registerProcessor) it runs
 * there; where a page can't load a worklet (Chrome, for a page opened from
 * disk) the page runs the same class in a ScriptProcessorNode instead. */
class GbadsRing {
  constructor(outRate) {
    this.outRate = outRate;
    this.cap = 16384;                       // sample frames held at most
    this.l = new Float32Array(this.cap);
    this.r = new Float32Array(this.cap);
    this.rd = 0; this.wr = 0; this.n = 0;   // ring: read, write, frames held
    this.pos = 0;                           // fraction between input frames
    this.rate = 48000;
    this.target = 2048;                     // frames to keep queued here (~43 ms)
    this.asked = 0; this.played = 0; this.dry = 0;
    this.started = false;
    this.src = null;
  }
  link(port) {
    if (this.src) this.src.close();
    this.src = port;
    port.onmessage = (m) => this.take(m.data);
    this.asked = 0; this.dry = 0;
    this.ask(this.target);
  }
  ask(n) { if (this.src && n > 0) { this.src.postMessage({ need: n }); this.asked += n; } }
  take({ pcm, rate }) {
    this.rate = rate;
    const frames = pcm.length >> 1;
    for (let i = 0; i < frames && this.n < this.cap; i++) {
      this.l[this.wr] = pcm[i * 2] / 32768;
      this.r[this.wr] = pcm[i * 2 + 1] / 32768;
      this.wr = (this.wr + 1) % this.cap; this.n++;
    }
    this.asked = Math.max(0, this.asked - frames);
  }
  fill(L, R) {
    const step = this.rate / this.outRate;
    let used = 0;
    // wait for a little to be queued before starting (and after running dry)
    if (!this.started && this.n >= this.target / 2) this.started = true;
    for (let i = 0; i < L.length; i++) {
      if (!this.started || this.n < 2) { L[i] = R[i] = 0; this.started = false; continue; }
      const a = this.rd, b = (this.rd + 1) % this.cap, f = this.pos;
      L[i] = this.l[a] + (this.l[b] - this.l[a]) * f;
      R[i] = this.r[a] + (this.r[b] - this.r[a]) * f;
      this.pos += step;
      while (this.pos >= 1) { this.pos -= 1; this.rd = (this.rd + 1) % this.cap; this.n--; used++; }
    }
    this.played += used;
    // ask for what was played, and for any shortfall below the target
    if (this.played >= 256) {
      const short = Math.max(0, this.target - this.n - this.asked);
      this.ask(Math.max(this.played, short));
      this.played = 0;
    } else if (this.n + this.asked < this.target / 2) {
      this.ask(this.target - this.n - this.asked);
    }
    // dry for a second (the game paused, say): what was asked for may be lost, ask again
    if (this.n === 0) { if ((this.dry += L.length) >= this.outRate) { this.dry = 0; this.asked = 0; this.ask(this.target); } }
    else this.dry = 0;
  }
}

if (typeof registerProcessor === "function") {
  registerProcessor("gbads-out", class extends AudioWorkletProcessor {
    constructor() {
      super();
      this.ring = new GbadsRing(sampleRate);
      this.port.onmessage = (e) => { if (e.data.port) this.ring.link(e.data.port); };
    }
    process(inputs, outputs) {
      const out = outputs[0];
      this.ring.fill(out[0], out[1] || out[0]);
      return true;
    }
  });
}
