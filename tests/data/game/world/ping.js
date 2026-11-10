// A cycle: ping imports pong, which imports ping.
import { pong } from "./pong.js";
export function ping(n) { return n <= 0 ? "ping" : pong(n - 1); }
