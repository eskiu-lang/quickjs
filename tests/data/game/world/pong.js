import { ping } from "./ping.js";
export function pong(n) { return n <= 0 ? "pong" : ping(n - 1); }
