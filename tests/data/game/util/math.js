let n = 0;
export function clamp(v, lo, hi) { n++; return Math.min(hi, Math.max(lo, v)); }
export function calls() { return n; }
