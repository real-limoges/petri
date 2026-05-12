import { init, step, setTilt, getRays, FLOATS_PER_RAY } from '../../vendor/petri/js/prism.js';

// Tilt range is asymmetric: negative tilts cause TIR across the full spectrum.
// Map cursor x to [TILT_MIN, TILT_MAX] rather than the symmetric ±0.6 in prism-plan.md.
const DEFAULT_TILT = 0.5;   // matches DEFAULT_TILT in src/prism.c
const TILT_MIN     = 0.0;
const TILT_MAX     = 1.1;

export default {
    async mounted() {
        const el      = this.el;
        const canvasW = parseInt(el.dataset.width  ?? 600, 10);
        const canvasH = parseInt(el.dataset.height ?? 400, 10);
        el.width  = canvasW;
        el.height = canvasH;
        const ctx = el.getContext('2d');

        await init();

        let dirty      = true;
        let curTilt    = DEFAULT_TILT;
        let targetTilt = DEFAULT_TILT;

        const draw = () => {
            ctx.clearRect(0, 0, canvasW, canvasH);

            // Translucent white band hinting at the incoming beam (left half, centered on y).
            ctx.save();
            ctx.globalAlpha = 0.12;
            ctx.fillStyle = '#ffffff';
            ctx.fillRect(0, canvasH / 2 - 4, canvasW / 2, 8);
            ctx.restore();

            step();
            const rays = getRays();   // Float32Array view — NOT a copy
            const n = rays.length / FLOATS_PER_RAY;
            ctx.lineWidth = 1;

            for (let i = 0; i < n; i++) {
                const o = i * FLOATS_PER_RAY;
                const r = Math.round(rays[o + 6] * 255);
                const g = Math.round(rays[o + 7] * 255);
                const b = Math.round(rays[o + 8] * 255);
                ctx.strokeStyle = `rgb(${r},${g},${b})`;

                // entry → exit (path inside the prism)
                ctx.beginPath();
                ctx.moveTo(rays[o],     rays[o + 1]);
                ctx.lineTo(rays[o + 2], rays[o + 3]);
                ctx.stroke();

                // exit → screen wall (or TIR bounce point inside prism)
                ctx.beginPath();
                ctx.moveTo(rays[o + 2], rays[o + 3]);
                ctx.lineTo(rays[o + 4], rays[o + 5]);
                ctx.stroke();
            }
        };

        const loop = () => {
            if (Math.abs(curTilt - targetTilt) > 0.001) {
                curTilt += (targetTilt - curTilt) * 0.08;
                setTilt(curTilt);
                dirty = true;
            }
            if (dirty) { draw(); dirty = false; }
            this._rafId = requestAnimationFrame(loop);
        };
        this._rafId = requestAnimationFrame(loop);

        this._onMove = (e) => {
            const { left, width } = el.getBoundingClientRect();
            const fx = Math.max(0, Math.min(1, (e.clientX - left) / width));
            targetTilt = TILT_MIN + fx * (TILT_MAX - TILT_MIN);
            dirty = true;
        };

        this._onLeave = () => { targetTilt = DEFAULT_TILT; };

        el.addEventListener('pointermove',  this._onMove);
        el.addEventListener('pointerleave', this._onLeave);
    },

    destroyed() {
        cancelAnimationFrame(this._rafId);
        this.el.removeEventListener('pointermove',  this._onMove);
        this.el.removeEventListener('pointerleave', this._onLeave);
    },
};
