// Entry module: relative imports, a bare import from the root, an import without
// an extension, top-level await and the engine host functions.
import { Player } from "./world/player.js";
import { clamp, calls } from "util/math.js";
import * as config from "./config";
import { ready } from "./world/async.js";
import { ping } from "./world/ping.js";

const player = new Player(engine.startX());
let frames = 0;

export function describe() {
  return `${config.NAME} ${player.x} ${frames} ${calls()} ${ping(3)}`;
}

globalThis.onBoot = () => {
  engine.log(`boot ${config.NAME} ready=${ready}`);
};

globalThis.onUpdate = (dt) => {
  frames++;
  player.walk(dt * config.SPEED);
  player.x = clamp(player.x, 0, 100);
  engine.setPos(player.x, frames);
};

export const later = import("./world/late.js").then((m) => m.value);
export default player;
