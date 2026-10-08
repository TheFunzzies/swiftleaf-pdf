// Draw the Swiftleaf app icon (a white leaf on a teal rounded square) and write
// it as a multi-size .ico with PNG-compressed images, plus a 256px .png preview.
//
//   bun tools/swiftleaf/make-icon.ts src/shared/gfx/Swiftleaf.ico
import { writeFileSync } from "node:fs";
import { deflateSync } from "node:zlib";

const out = process.argv[2] ?? "src/shared/gfx/Swiftleaf.ico";
const sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256];
const kSamples = 4; // per axis, for anti-aliasing

type RGBA = [number, number, number, number];
const teal1: RGBA = [0x14, 0x9a, 0x80, 255]; // top
const teal2: RGBA = [0x0a, 0x63, 0x52, 255]; // bottom
const white: RGBA = [255, 255, 255, 255];
const vein: RGBA = [0x0e, 0x7c, 0x66, 255];

// signed tests in unit coordinates (0..1, y down)
function inRoundedSquare(x: number, y: number, margin: number, radius: number): boolean {
  const lo = margin;
  const hi = 1 - margin;
  if (x < lo || x > hi || y < lo || y > hi) return false;
  const cx = Math.min(Math.max(x, lo + radius), hi - radius);
  const cy = Math.min(Math.max(y, lo + radius), hi - radius);
  return (x - cx) ** 2 + (y - cy) ** 2 <= radius * radius;
}

// leaf: a lens (two intersecting circles) along an axis tilted 45 degrees,
// base at the bottom left, tip at the top right
const leafCenter = { x: 0.53, y: 0.47 };
const leafHalfLen = 0.31;
const leafHalfWidth = 0.16;
const angle = -Math.PI / 4;
const R = (leafHalfLen ** 2 + leafHalfWidth ** 2) / (2 * leafHalfWidth);
const off = R - leafHalfWidth;

function toLeaf(x: number, y: number): [number, number] {
  const dx = x - leafCenter.x;
  const dy = y - leafCenter.y;
  // rotate by -angle so the leaf axis is the u axis
  const u = dx * Math.cos(-angle) - dy * Math.sin(-angle);
  const v = dx * Math.sin(-angle) + dy * Math.cos(-angle);
  return [u, v];
}

function inLeaf(x: number, y: number): boolean {
  const [u, v] = toLeaf(x, y);
  return u * u + (v - off) ** 2 <= R * R && u * u + (v + off) ** 2 <= R * R;
}

function nearSegment(u: number, v: number, u0: number, u1: number, halfThick: number): boolean {
  return u >= u0 && u <= u1 && Math.abs(v) <= halfThick;
}

function inVein(x: number, y: number, size: number): boolean {
  const [u, v] = toLeaf(x, y);
  const t = size <= 24 ? 0.03 : 0.018;
  if (nearSegment(u, v, -leafHalfLen * 0.8, leafHalfLen * 0.72, t)) return true;
  if (size < 40) return false;
  // side veins
  for (const k of [-0.35, 0.0, 0.35]) {
    const u0 = k * leafHalfLen;
    for (const s of [1, -1]) {
      // a short line from the midrib toward the edge, angled to the tip
      const du = u - u0;
      const dv = v * s;
      const along = (du * 0.6 + dv * 0.8);
      const across = Math.abs(du * 0.8 - dv * 0.6);
      if (along > 0 && along < leafHalfWidth * 0.95 && across < t * 0.8) return true;
    }
  }
  return false;
}

function inStem(x: number, y: number, size: number): boolean {
  const [u, v] = toLeaf(x, y);
  const t = size <= 24 ? 0.035 : 0.025;
  return nearSegment(u, v, -leafHalfLen - 0.11, -leafHalfLen + 0.02, t);
}

function shade(x: number, y: number, size: number): RGBA | null {
  const margin = size <= 20 ? 0.0 : 0.04;
  const radius = size <= 20 ? 0.16 : 0.2;
  if (!inRoundedSquare(x, y, margin, radius)) return null;
  if (inLeaf(x, y)) {
    return inVein(x, y, size) ? vein : white;
  }
  if (inStem(x, y, size)) return white;
  const t = y;
  return [
    teal1[0] + (teal2[0] - teal1[0]) * t,
    teal1[1] + (teal2[1] - teal1[1]) * t,
    teal1[2] + (teal2[2] - teal1[2]) * t,
    255,
  ];
}

function render(size: number): Uint8Array {
  const px = new Uint8Array(size * size * 4);
  for (let py = 0; py < size; py++) {
    for (let pxi = 0; pxi < size; pxi++) {
      let r = 0, g = 0, b = 0, a = 0;
      for (let sy = 0; sy < kSamples; sy++) {
        for (let sx = 0; sx < kSamples; sx++) {
          const x = (pxi + (sx + 0.5) / kSamples) / size;
          const y = (py + (sy + 0.5) / kSamples) / size;
          const c = shade(x, y, size);
          if (!c) continue;
          r += c[0];
          g += c[1];
          b += c[2];
          a += 255;
        }
      }
      const n = kSamples * kSamples;
      const i = (py * size + pxi) * 4;
      const cov = a / 255; // samples covered
      // straight (non-premultiplied) alpha
      px[i] = cov ? Math.round(r / cov) : 0;
      px[i + 1] = cov ? Math.round(g / cov) : 0;
      px[i + 2] = cov ? Math.round(b / cov) : 0;
      px[i + 3] = Math.round(a / n);
    }
  }
  return px;
}

const crcTable = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    t[n] = c >>> 0;
  }
  return t;
})();

function crc32(buf: Uint8Array): number {
  let c = 0xffffffff;
  for (const b of buf) c = crcTable[(c ^ b) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

function chunk(type: string, data: Uint8Array): Uint8Array {
  const out = new Uint8Array(12 + data.length);
  const dv = new DataView(out.buffer);
  dv.setUint32(0, data.length);
  for (let i = 0; i < 4; i++) out[4 + i] = type.charCodeAt(i);
  out.set(data, 8);
  dv.setUint32(8 + data.length, crc32(out.subarray(4, 8 + data.length)));
  return out;
}

function png(size: number, rgba: Uint8Array): Uint8Array {
  const raw = new Uint8Array(size * (size * 4 + 1));
  for (let y = 0; y < size; y++) {
    raw[y * (size * 4 + 1)] = 0; // filter: none
    raw.set(rgba.subarray(y * size * 4, (y + 1) * size * 4), y * (size * 4 + 1) + 1);
  }
  const ihdr = new Uint8Array(13);
  const dv = new DataView(ihdr.buffer);
  dv.setUint32(0, size);
  dv.setUint32(4, size);
  ihdr[8] = 8; // bit depth
  ihdr[9] = 6; // RGBA
  const parts = [
    new Uint8Array([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk("IHDR", ihdr),
    chunk("IDAT", new Uint8Array(deflateSync(raw, { level: 9 }))),
    chunk("IEND", new Uint8Array()),
  ];
  return concat(parts);
}

function concat(parts: Uint8Array[]): Uint8Array {
  const total = parts.reduce((n, p) => n + p.length, 0);
  const res = new Uint8Array(total);
  let off = 0;
  for (const p of parts) {
    res.set(p, off);
    off += p.length;
  }
  return res;
}

const images = sizes.map((s) => ({ size: s, data: png(s, render(s)) }));
const header = new Uint8Array(6 + 16 * images.length);
const hv = new DataView(header.buffer);
hv.setUint16(0, 0, true);
hv.setUint16(2, 1, true); // icon
hv.setUint16(4, images.length, true);
let offset = header.length;
images.forEach((img, i) => {
  const e = 6 + i * 16;
  header[e] = img.size >= 256 ? 0 : img.size;
  header[e + 1] = img.size >= 256 ? 0 : img.size;
  hv.setUint16(e + 4, 1, true); // planes
  hv.setUint16(e + 6, 32, true); // bpp
  hv.setUint32(e + 8, img.data.length, true);
  hv.setUint32(e + 12, offset, true);
  offset += img.data.length;
});
writeFileSync(out, concat([header, ...images.map((i) => i.data)]));
writeFileSync(out.replace(/\.ico$/, "-256.png"), images[images.length - 1].data);
console.log(`wrote ${out} (${sizes.join(", ")})`);
