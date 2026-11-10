import { clamp } from "../util/math.js";
import { tileSize } from "./tiles.js";

export class Player {
  constructor(x) { this.x = x; }
  walk(d) { this.x = clamp(this.x + d * tileSize, -1000, 1000); }
}
