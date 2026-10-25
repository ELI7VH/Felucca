// Vendored unchanged from WaveLoop's Shape Arp toy (SHAYPE): waveloop web/src/arp/core/gesture.mjs at 678cdf2b.
// sha256 of the source (every line after these three): fde4742c6040916389fdaa0afed54ea700056768d16759b4673a4ec94ad32e0c
// editor.html carries it inline (ARP-CORE, exports dropped); node web/test_web.mjs checks both copies.
// Editing the grid by pointer, in cells. On an empty cell a press starts a note and dragging across sets its
// length. On a note a drag carries it (any row, any step), a drag from the right half of its last cell
// stretches it, and a press that never moves removes it. Erase (the right button) takes every note it crosses.
import { paintNote } from './sequence.mjs';

export const noteAt = (pattern, row, step) => pattern.find(n => n.row === row && step >= n.step && step < n.step + n.length);
export const eraseAt = (pattern, row, step) => pattern.filter(n => !(n.row === row && step >= n.step && step < n.step + n.length));

// -> { drag, pattern }: keep drag for the moves and the release.
export function gridPress(pattern, { row, step, rightHalf = false, erase = false }) {
  if (erase) return { drag: { mode: 'erase' }, pattern: eraseAt(pattern, row, step) };
  const hit = noteAt(pattern, row, step), base = pattern.filter(n => n !== hit).map(n => ({ ...n }));
  if (!hit) return { drag: { mode: 'draw', from: step, base }, pattern: paintNote(base, row, step, 1) };
  if (step === hit.step + hit.length - 1 && rightHalf) return { drag: { mode: 'draw', from: hit.step, base, hit, at: { step, row } }, pattern };
  return { drag: { mode: 'move', grab: step - hit.step, length: hit.length, base, hit, at: { step, row } }, pattern };
}

export function gridDrag(drag, pattern, { row, step }, steps) {
  if (drag.mode === 'erase') return eraseAt(pattern, row, step);
  if (drag.at && drag.at.step === step && drag.at.row === row) return pattern;
  drag.at = null;
  // Across sets the length from the origin step; up and down carries the whole note to that row.
  if (drag.mode === 'draw') return paintNote(drag.base, row, Math.min(drag.from, step), Math.abs(step - drag.from) + 1);
  return paintNote(drag.base, row, Math.max(0, Math.min(steps - drag.length, step - drag.grab)), drag.length);
}

// A note pressed and let go where it was is gone.
export const gridRelease = (drag, pattern) => (drag.hit && drag.at ? drag.base : pattern);
