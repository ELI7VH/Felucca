import { UltraCursor, cursorState } from './ultra-cursor.js';

// Remote peers keep the original Mac pyramid. The local pointer uses the
// full UltraKit twin in ultra-cursor.js, with the same pinned apex and trails.
const TAU = 2 * Math.PI;
const BAYER = [0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5];
export const GOLD_PALETTE = [[66,54,20],[143,117,43],[255,209,77],[255,228,151]];
const colors = palette => palette.map(rgb => `rgb(${rgb.join(' ')})`);

export class CursorPyramid {
  yaw = .7;
  morph = 0;
  advance(seconds, clickable, reduced) {
    const dt = Math.min(.1, Math.max(0, seconds));
    const ease = reduced ? 1 : 1 - Math.exp(-dt * 12);
    this.morph += ((clickable ? 1 : 0) - this.morph) * ease;
    if (clickable) {
      const delta = Math.atan2(Math.sin(Math.PI / 4 - this.yaw), Math.cos(Math.PI / 4 - this.yaw));
      this.yaw += delta * ease;
    } else if (!reduced) this.yaw = (this.yaw + dt * TAU / 10) % TAU;
  }
  project(x, y) {
    const tilt = -3 * Math.PI / 4 * this.morph;
    const px = x * (1 - .22 * this.morph), py = y + 14;
    return [px * Math.cos(tilt) - py * Math.sin(tilt), px * Math.sin(tilt) + py * Math.cos(tilt)];
  }
  get tip() { return this.project(0, -14); }
  draw(ctx, palette = GOLD_PALETTE) {
    const ring = Array.from({length:4}, (_,i) => {
      const a = this.yaw + i * Math.PI / 2;
      return {x:Math.cos(a)*12,z:Math.sin(a)*12};
    });
    const order = [0,1,2,3].sort((i,j) => ring[i].z + ring[(i+1)%4].z - ring[j].z - ring[(j+1)%4].z);
    const fills = colors(palette);
    for (const i of order) {
      const a = ring[i], b = ring[(i+1)%4];
      const points = [this.tip, this.project(a.x,8-a.z*.36), this.project(b.x,8-b.z*.36)];
      const light = (Math.cos(this.yaw + (i+.5)*Math.PI/2 - .8)+1)/2;
      ctx.beginPath(); ctx.moveTo(...points[0]); ctx.lineTo(...points[1]); ctx.lineTo(...points[2]); ctx.closePath();
      ctx.fillStyle = fills[Math.min(3,Math.floor(light*3.99))]; ctx.fill();
      ctx.save(); ctx.clip(); ctx.shadowBlur = 0;
      const xs = points.map(p=>p[0]), ys = points.map(p=>p[1]);
      const minY = Math.min(...ys), maxY = Math.max(...ys);
      const pixels = fills.map(() => new Path2D());
      for (let y=Math.floor(minY);y<=Math.ceil(maxY);y++) {
        const gradient = (y-minY)/Math.max(1,maxY-minY);
        const shade = Math.min(3,Math.max(0,.2+light*2.45+gradient*.25)), low = Math.floor(shade);
        for (let x=Math.floor(Math.min(...xs));x<=Math.ceil(Math.max(...xs));x++) {
          const threshold = (BAYER[(y&3)*4+(x&3)]+.5)/16;
          pixels[Math.min(3,low+(shade-low>threshold ? 1:0))].rect(x,y,1,1);
        }
      }
      for (let tone=0;tone<4;tone++) { ctx.fillStyle = fills[tone]; ctx.fill(pixels[tone]); }
      ctx.restore();
    }
  }
}

export function cursorContext(target) {
  if (!(target instanceof Element)) return 'idle';
  const control = target.closest('a[href],button,summary,label,input,textarea,select,[contenteditable]:not([contenteditable="false"]),[role="button"],[role="link"],[role="checkbox"],[role="radio"],[role="slider"],[role="textbox"],wl-fader');
  if (target.closest('[inert]')) return 'idle';
  const marked = target.closest('[data-wl-cursor]');
  const explicit = cursorState(marked?.dataset.wlCursor);
  if (control?.matches(':disabled,[aria-disabled="true"]') || control?.closest('[aria-disabled="true"]')) return 'no';
  if (marked && !['idle','surface'].includes(explicit)) return explicit;
  if (target.closest('[data-choppa-drag="true"],[draggable="true"]')) return 'move';
  if (control?.matches('[role="slider"],input[type="range"],wl-fader')) return control.getAttribute('aria-orientation') === 'vertical' ? 'resize-ns' : 'resize-ew';
  if (control?.matches('textarea,[contenteditable]:not([contenteditable="false"]),[role="textbox"],input:not([type="button"]):not([type="submit"]):not([type="reset"]):not([type="checkbox"]):not([type="radio"]):not([type="range"]):not([type="color"]):not([type="file"]):not([type="hidden"])')) return 'text';
  if (control?.matches('label') && control.control?.matches(':disabled')) return 'no';
  if (control) return 'interactive';
  return marked ? explicit : target.closest('canvas,svg[aria-label*="waveform"],.wl-tape-screen,.wl-choppa-screen') ? 'surface' : 'idle';
}

// Match the shared cursor's ll-* ownership, including shadow DOM controls.
export function widgetCursorHost(target) {
  for (let node = target; node instanceof Element; node = node.parentElement || node.getRootNode()?.host) {
    if (node.localName.startsWith('ll-')) return node;
  }
  return null;
}

export function startArtifactCursor(allowed = () => true, getColour = () => null) {
  if (document.querySelector('.wl-artifact-cursor')) return;
  const fine = matchMedia('(hover: hover) and (pointer: fine)');
  const motion = matchMedia('(prefers-reduced-motion: reduce)');
  const contrast = matchMedia('(forced-colors: active)');
  const canvas = document.createElement('canvas');
  canvas.className = 'wl-artifact-cursor'; canvas.hidden = true;
  canvas.setAttribute('aria-hidden','true'); document.body.append(canvas);
  const ctx = canvas.getContext('2d');
  if (!ctx) { canvas.remove(); return; }
  const cursor = new UltraCursor();
  let tint, palette = GOLD_PALETTE, rgb = [255,209,77];
  let point, previous, sparks = [], distance = 0, spawned = 0, frame = 0, last = 0, dirty, dpr, widget;
  let pressed = false, pressUntil = 0, dragState = null, fileDrag = null, typedUntil = 0, typingTimer;
  const enabled = () => fine.matches && !contrast.matches && allowed();
  function setWidget(host) {
    if (host === widget) return;
    widget?.removeAttribute('data-wl-widget-hover'); widget = host;
    widget?.setAttribute('data-wl-widget-hover', '');
  }
  function resize() {
    dpr = Math.min(2,window.devicePixelRatio || 1);
    canvas.width = Math.round(innerWidth*dpr); canvas.height = Math.round(innerHeight*dpr);
    ctx.setTransform(dpr,0,0,dpr,0,0); dirty = null;
  }
  function stop() {
    cancelAnimationFrame(frame); frame = 0; last = 0;
    canvas.hidden = true; document.documentElement.classList.remove('wl-artifact-active');
    if (dirty) ctx.clearRect(...dirty);
    dirty = null; point = previous = null; sparks = []; distance = 0;
    pressed = false; pressUntil = 0; dragState = fileDrag = null;
    cursor.pressed = false; cursor.busy = false; cursor.busyT = 0; cursor.hover = 0;
  }
  function addSpark(x,y,now) {
    sparks.push({x,y,born:now,seed:spawned++*2.399963});
    if (sparks.length>80) sparks.splice(0,sparks.length-80);
  }
  function paint(now) {
    frame = 0;
    if (!point || !enabled() || document.hidden || !document.hasFocus()) { stop(); return; }
    const target = document.elementFromPoint(point.x,point.y);
    const host = widgetCursorHost(target);
    if (host) { setWidget(host); stop(); return; }
    // Reconcile ownership after a panel disappears underneath a still pointer.
    setWidget(null);
    const nextTint = getColour();
    if (nextTint !== tint) {
      tint = nextTint;
      rgb = /^#[0-9a-f]{6}$/i.test(tint || '') ? tint.slice(1).match(/../g).map(v=>parseInt(v,16)) : [255,209,77];
      palette = tint ? [rgb.map(v=>Math.round(v*.26)),rgb.map(v=>Math.round(v*.56)),rgb,rgb.map(v=>Math.round(v*.59+255*.41))] : GOLD_PALETTE;
    }
    const text = typedUntil>now && cursorContext(document.activeElement)==='text';
    const context = fileDrag || dragState || (text ? 'text' : cursorContext(target));
    const reduced = motion.matches;
    const surface = target?.closest('[data-wl-cursor-colour],[data-app],.wl-tape,.lp');
    const colour = surface && (surface.dataset.wlCursorColour || getComputedStyle(surface).getPropertyValue('--app').trim() || getComputedStyle(surface).getPropertyValue('--wl-accent').trim());
    const surfaceRGB = /^#[0-9a-f]{6}$/i.test(colour || '') ? colour.slice(1).match(/../g).map(v=>parseInt(v,16)) : rgb;
    const busy = [...document.querySelectorAll('[aria-busy="true"],[data-wl-cursor-busy="true"]')].some(node=>!widgetCursorHost(node) && !node.closest('[hidden],[inert]'));
    // A quick tap can finish between frames; keep its pink spring edge visible.
    cursor.step(last ? (now-last)/1000 : 1/60,context,{now:now/1000,reduced,pressed:pressed || now<pressUntil,busy,tint:surfaceRGB,palette}); last = now;
    canvas.dataset.context = context;
    canvas.dataset.pressed = String(pressed);
    canvas.dataset.busy = String(cursor.busyT>.01);
    if (dirty) ctx.clearRect(...dirty);
    if (reduced) sparks = [];
    sparks = sparks.filter(s=>now-s.born<=550);
    if (cursor.burst) for (let i=0;i<10;i++) addSpark(point.x+Math.cos(i*TAU/10)*6,point.y+Math.sin(i*TAU/10)*6,now);
    const xs = [point.x-64,point.x+64,...sparks.map(s=>s.x)], ys = [point.y-64,point.y+64,...sparks.map(s=>s.y)];
    const left = Math.min(...xs)-24, top = Math.min(...ys)-24;
    dirty = [left,top,Math.max(...xs)-left+24,Math.max(...ys)-top+24];
    for (const s of sparks) paintSpark(ctx,s,now,550,cursor.tone(2,0));
    ctx.shadowBlur = 0;
    ctx.save(); ctx.translate(Math.floor(point.x),Math.floor(point.y)); cursor.draw(ctx,now/1000); ctx.restore();
    canvas.hidden = false; document.documentElement.classList.add('wl-artifact-active');
    if (!reduced || now<pressUntil || (busy && cursor.busyT===0)) frame = requestAnimationFrame(tick);
  }
  function tick(now) { try { paint(now); } catch { stop(); } }
  function schedule() { if (!frame && point) frame = requestAnimationFrame(tick); }
  function pointer(e) {
    setWidget(e.pointerType === 'mouse' ? widgetCursorHost(e.composedPath()[0]) : null);
    if (widget || e.pointerType !== 'mouse' || !enabled()) { stop(); return false; }
    point = {x:e.clientX,y:e.clientY}; return true;
  }
  document.addEventListener('pointermove',e=>{
    if (!pointer(e)) return;
    if (previous && !motion.matches) {
      distance += Math.hypot(point.x-previous.x,point.y-previous.y);
      const count = Math.min(8,Math.floor(distance/3));
      if (count) distance %= 3;
      for (let i=1;i<=count;i++) { const f=i/count; addSpark(previous.x+(point.x-previous.x)*f,previous.y+(point.y-previous.y)*f,performance.now()); }
    }
    previous = point; schedule();
  },{passive:true});
  document.addEventListener('pointerdown',e=>{
    if (e.button!==0 || !pointer(e)) return;
    pressed = true;
    pressUntil = performance.now()+65;
    const state = cursorContext(e.composedPath()[0]);
    dragState = ['move','text','surface'].includes(state) || state.startsWith('resize-') ? state : null;
    schedule();
  },{passive:true,capture:true});
  const release = ()=>{ pressed=false; dragState=null; fileDrag=null; schedule(); };
  window.addEventListener('pointerup',release,{passive:true,capture:true});
  window.addEventListener('pointercancel',release,{passive:true,capture:true});
  document.addEventListener('keydown',e=>{
    if (cursorContext(document.activeElement)!=='text' || e.metaKey || e.ctrlKey || e.altKey) return;
    typedUntil = performance.now()+1200;
    clearTimeout(typingTimer); typingTimer=setTimeout(schedule,1210); schedule();
  },{passive:true});
  document.addEventListener('dragover',e=>{
    if (!enabled() || widgetCursorHost(e.target)) { stop(); return; }
    point={x:e.clientX,y:e.clientY};
    fileDrag=e.defaultPrevented || e.target.closest('[data-wl-cursor-drop]') ? 'drop' : 'no'; schedule();
  },{passive:true});
  document.addEventListener('dragleave',e=>{ if (!e.relatedTarget) stop(); },{passive:true});
  document.addEventListener('drop',release,{passive:true});
  document.addEventListener('dragend',release,{passive:true});
  document.documentElement.addEventListener('pointerleave',stop);
  window.addEventListener('blur',()=>{setWidget(null);stop();});
  document.addEventListener('visibilitychange',()=>{if(document.hidden){setWidget(null);stop();}});
  window.addEventListener('pagehide',stop);
  window.addEventListener('resize',()=>{resize();schedule();});
  window.addEventListener('scroll',schedule,{passive:true});
  document.addEventListener('focusin',schedule);
  motion.addEventListener('change',schedule);
  for (const query of [fine,contrast]) query.addEventListener('change',()=>{if(!enabled())stop();else schedule();});
  new MutationObserver(schedule).observe(document.body,{subtree:true,childList:true,attributes:true,attributeFilter:['aria-busy','data-wl-cursor-busy','aria-disabled','disabled']});
  resize();
}

function paintSpark(ctx,spark,now,lifetime,rgb) {
  const age = (now-spark.born)/1000, fade = Math.max(0,1-(now-spark.born)/lifetime)**2;
  const x = spark.x+Math.sin(spark.seed)*age*14, y = spark.y+age*12, size = 1+fade*1.5;
  ctx.shadowBlur = 5; ctx.shadowColor = `rgba(${rgb.join(',')},${fade*.8})`;
  ctx.fillStyle = `rgba(${rgb.join(',')},${fade})`; ctx.fillRect(x-size/2,y-size/2,size,size);
}

// One bounded, short-lived particle pool for the whole room, not a loop per peer.
export class SparkTrail {
  particles = [];
  history = new Map();
  constructor({limit=320,lifetime=750}={}) { this.limit=limit; this.lifetime=lifetime; }
  move(id,point,now,rgb) {
    if (!Number.isFinite(point.x) || !Number.isFinite(point.y)) return;
    const old = this.history.get(id);
    const distance = old ? old.distance+Math.hypot(point.x-old.x,point.y-old.y) : 0;
    const count = Math.min(16,Math.floor(distance/3));
    this.history.set(id,{...point,distance:count ? distance%3 : distance});
    if (old) for (let i=1;i<=count;i++) {
      const f = i/count;
      this.particles.push({id,x:old.x+(point.x-old.x)*f,y:old.y+(point.y-old.y)*f,born:now,seed:this.particles.length*2.399963,rgb});
    }
    if (this.particles.length>this.limit) this.particles.splice(0,this.particles.length-this.limit);
  }
  expire(now) { this.particles=this.particles.filter(p => now-p.born<=this.lifetime); }
  remove(id) { this.history.delete(id); this.particles=this.particles.filter(p=>p.id!==id); }
  clear() { this.history.clear(); this.particles=[]; }
}

export function createVisitorTrails(container) {
  const trail = new SparkTrail(), motion = matchMedia('(prefers-reduced-motion: reduce)');
  const canvas = document.createElement('canvas');
  canvas.className='wl-visitor-trails'; canvas.hidden=true; container.prepend(canvas);
  const ctx = canvas.getContext('2d');
  let frame=0, dirty;
  function clearDrawing() { if (ctx && dirty) ctx.clearRect(...dirty); dirty=null; }
  function clear() {
    cancelAnimationFrame(frame); frame=0; trail.clear(); clearDrawing();
    canvas.hidden=true; canvas.dataset.sparks='0';
  }
  function resize() { clear(); canvas.width=innerWidth; canvas.height=innerHeight; }
  function draw(now) {
    frame=0; clearDrawing();
    if (!ctx || document.hidden || motion.matches) { clear(); return; }
    trail.expire(now);
    canvas.dataset.sparks=String(trail.particles.length);
    canvas.hidden=!trail.particles.length;
    if (!trail.particles.length) return;
    const xs=trail.particles.map(p=>p.x), ys=trail.particles.map(p=>p.y);
    const left=Math.min(...xs)-24, top=Math.min(...ys)-24;
    dirty=[left,top,Math.max(...xs)-left+48,Math.max(...ys)-top+48];
    for (const p of trail.particles) paintSpark(ctx,p,now,trail.lifetime,p.rgb);
    ctx.shadowBlur=0;
    frame=requestAnimationFrame(draw);
  }
  function schedule() { if (!frame) frame=requestAnimationFrame(draw); }
  motion.addEventListener('change',clear);
  window.addEventListener('resize',resize);
  resize();
  return {
    move(id,x,y,rgb) {
      if (!ctx || document.hidden || motion.matches) return;
      trail.move(id,{x,y},performance.now(),rgb);
      if (trail.particles.length) schedule();
    },
    remove(id) { trail.remove(id); if (!canvas.hidden) schedule(); },
    clear,
  };
}
