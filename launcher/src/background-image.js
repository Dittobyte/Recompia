// Canvas pipeline for the player's own launcher background: one picture file in,
// two small JPEG data URLs out (sharp for Home, pre-blurred soft for other pages).
// The original is decoded, drawn and dropped; nothing is kept or uploaded.
import { SHARP_W, SHARP_H, SOFT_W, SOFT_H, MAX_STORED_CHARS, checkInput, coverRect } from './background-model.js';

const BLUR_DIV = 8; // soft layer: drawn down to 1/8 and back up is the blur
const Q_SHARP = 0.82, Q_SOFT = 0.7, Q_RETRY = 0.6;
// Only errors made here reach the caller; anything the browser throws becomes MSG_OPEN.
function plain(message) {
  const e = new Error(message);
  e.plain = true;
  return e;
}
const MSG_OPEN = 'That picture could not be opened.';
const MSG_BIG = 'That picture is too detailed to keep. Try a smaller one.';

// Pure plan: every size and rectangle the pipeline uses, for a decoded size.
export function planSteps(srcW, srcH) {
  return {
    crop: coverRect(srcW, srcH, SHARP_W, SHARP_H),
    sharp: { w: SHARP_W, h: SHARP_H },
    small: { w: SOFT_W / BLUR_DIV, h: SOFT_H / BLUR_DIV },
    soft: { w: SOFT_W, h: SOFT_H },
  };
}

function newCanvas(w, h) {
  const c = typeof document !== 'undefined' && document.createElement
    ? document.createElement('canvas')
    : new OffscreenCanvas(w, h);
  c.width = w;
  c.height = h;
  return c;
}

function context(canvas) {
  const g = canvas.getContext('2d');
  if (!g) throw plain(MSG_OPEN);
  g.imageSmoothingEnabled = true;
  g.imageSmoothingQuality = 'high';
  return g;
}

// JPEG data URL; one retry at a lower quality if it is over the storage limit.
export function encode(canvas, quality) {
  let url = canvas.toDataURL('image/jpeg', quality);
  if (url.length > MAX_STORED_CHARS) url = canvas.toDataURL('image/jpeg', Q_RETRY);
  if (url.length > MAX_STORED_CHARS || !url.startsWith('data:image/jpeg')) throw plain(MSG_BIG);
  return url;
}

function readAsDataUrl(file) {
  return new Promise((resolve, reject) => {
    const r = new FileReader();
    r.onload = () => resolve(r.result);
    r.onerror = () => reject(plain(MSG_OPEN));
    r.readAsDataURL(file);
  });
}

// Decode to something drawImage accepts; "close" is a no-op for an <img>.
async function decode(file) {
  if (typeof createImageBitmap === 'function') {
    const bmp = await createImageBitmap(file, { imageOrientation: 'from-image' });
    return { src: bmp, w: bmp.width, h: bmp.height, close: () => bmp.close && bmp.close() };
  }
  const img = new Image();
  img.src = await readAsDataUrl(file);
  await img.decode();
  return { src: img, w: img.naturalWidth, h: img.naturalHeight, close() {} };
}

export async function makeBackground(file) {
  const first = checkInput({ size: file && file.size, type: file && file.type });
  if (!first.ok) throw plain(first.reason);
  let pic = null;
  try {
    try {
      pic = await decode(file);
    } catch (e) {
      throw plain(MSG_OPEN);
    }
    const sized = checkInput({ size: file.size, type: file.type, width: pic.w, height: pic.h });
    if (!sized.ok) throw plain(sized.reason);
    if (!(pic.w > 0 && pic.h > 0)) throw plain(MSG_OPEN);
    const p = planSteps(pic.w, pic.h);
    const sharp = newCanvas(p.sharp.w, p.sharp.h);
    const sg = context(sharp);
    sg.drawImage(pic.src, p.crop.sx, p.crop.sy, p.crop.sw, p.crop.sh, 0, 0, p.sharp.w, p.sharp.h);
    const small = newCanvas(p.small.w, p.small.h);
    context(small).drawImage(sharp, 0, 0, p.small.w, p.small.h);
    const soft = newCanvas(p.soft.w, p.soft.h);
    context(soft).drawImage(small, 0, 0, p.soft.w, p.soft.h);
    return { sharp: encode(sharp, Q_SHARP), soft: encode(soft, Q_SOFT) };
  } catch (e) {
    throw e && e.plain ? e : plain(MSG_OPEN);
  } finally {
    if (pic) pic.close();
  }
}
