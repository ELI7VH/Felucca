// Canvas twin of mac/UltraKit/Sources/UltraKitC/ultra_cursor.c and ultra_xy.c.
// Positions are measured from the apex: the hotspot never moves with a morph.
const TAU = Math.PI * 2;
const BAYER = [0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5];
const GOLD = [[66,54,20],[143,117,43],[255,209,77],[255,228,151]];
const PINK = [255,84.15,178.5];
const clamp = (v, lo = 0, hi = 1) => Math.max(lo,Math.min(hi,v));
const mix = (a,b,t) => a.map((v,i)=>v+(b[i]-v)*t);
const luminance = c => c[0]*.2126+c[1]*.7152+c[2]*.0722;
const arc = a => Math.atan2(Math.sin(a),Math.cos(a));
const STATES = new Set(['idle','interactive','surface','text','resize-ew','resize-ns','resize-nwse','resize-nesw','move','drop','no']);
export function cursorState(value) { return STATES.has(value) ? value : 'idle'; }

export class UltraCursor {
  yaw = .7; morph = 0; surf = 0; pair = 0; cross = 0; drop = 0;
  ux = 1; uy = 0; tint = GOLD[2]; tintMix = 0; palette = GOLD;
  press = 0; velocity = 0; pressed = false; burst = false;
  hover = 0; orbit = 0; pingBorn = -1e9; busy = false; busySince = -1; busyT = 0; busySpin = 0;
  state = 'idle'; reduced = false;
  step(seconds, state, {now = 0, reduced = false, pressed = false, busy = false, tint = GOLD[2], palette = GOLD, accent = null, clickColor = PINK} = {}) {
    const dt = clamp(seconds,0,.1), ease = reduced ? 1 : 1-Math.exp(-dt*12);
    this.accent = accent; this.clickColor = clickColor;
    this.state = cursorState(state); this.reduced = reduced; this.palette = palette;
    const resize = this.state.startsWith('resize-'), move = this.state === 'move';
    const surface = ['surface','text','drop'].includes(this.state);
    const clickable = ['interactive','no'].includes(this.state) || resize || move;
    const tinted = surface || this.state === 'no';
    const colours = {text:accent || GOLD[2],drop:[79,195,247],no:[242.25,66.3,76.5]};
    if (tinted) this.tint = mix(this.tint,colours[this.state] || tint,ease);
    this.tintMix += ((tinted ? 1 : 0)-this.tintMix)*ease;
    const hover = this.state === 'interactive';
    if (hover && this.hover < .5 && now-this.pingBorn > .45 && !reduced) this.pingBorn = now;
    this.hover += ((hover ? 1 : 0)-this.hover)*ease;
    if (!reduced) this.orbit = (this.orbit+dt*TAU/1.3)%TAU;
    this.morph += ((clickable ? 1 : 0)-this.morph)*ease;
    this.surf += ((surface ? 1 : 0)-this.surf)*ease;
    if (resize || move) {
      [this.ux,this.uy] = ({'resize-ns':[0,1],'resize-nwse':[Math.SQRT1_2,Math.SQRT1_2],'resize-nesw':[-Math.SQRT1_2,Math.SQRT1_2]})[this.state] || [1,0];
    }
    this.pair += (((resize || move) ? 1 : 0)-this.pair)*ease;
    this.cross += ((move ? 1 : 0)-this.cross)*ease;
    this.drop += ((this.state === 'drop' ? 1 : 0)-this.drop)*ease;
    this.burst = pressed && !this.pressed && !reduced; this.pressed = pressed;
    if (reduced) { this.press = pressed ? 1 : 0; this.velocity = 0; }
    else {
      // Substeps keep UltraKit's underdamped spring stable after a long frame.
      const steps = Math.max(1,Math.ceil(dt*120)), h = dt/steps;
      for (let i=0;i<steps;i++) {
        this.velocity += (-900*(this.press-(pressed ? 1 : 0))-27*this.velocity)*h;
        this.press = Math.max(0,this.press+this.velocity*h);
      }
    }
    if (this.pair < .002) this.pair = 0;
    if (this.cross < .002) this.cross = 0;
    if (clickable || surface) this.yaw += arc(Math.PI/4-this.yaw)*ease;
    else if (!reduced) this.yaw = (this.yaw+dt*TAU/10)%TAU;
    if (busy && !this.busy) this.busySince = now;
    this.busy = busy;
    const show = busy && now-this.busySince >= .15;
    this.busyT += ((show ? 1 : 0)-this.busyT)*(reduced ? 1 : 1-Math.exp(-dt*(show ? 10 : 24)));
    if (this.busyT < .002) this.busyT = 0;
    if (!reduced) this.busySpin = (this.busySpin+dt*TAU/1.6)%TAU;
  }
  get tip() { return this.project(0,-14); }
  project(x,y) {
    const tilt = -3*Math.PI/4*this.morph, px = x*(1-.22*this.morph), depth = y+14;
    const normal = [px*Math.cos(tilt)-depth*Math.sin(tilt),px*Math.sin(tilt)+depth*Math.cos(tilt)];
    const flat = [depth*.36-x*.06,x*1.9+depth*.42];
    return mix(normal,flat,this.surf);
  }
  tone(i, flare = this.press) {
    // Text defaults to capture gold; a portable cursor can supply its own accent.
    const base = this.state === 'text' && !this.accent ? GOLD[i] : this.palette[i];
    const hi = GOLD[i][2] > 100 ? .25 : 0;
    const tint = this.tint.map(v=>v*(GOLD[i][0]/255)*(1-hi)+255*hi);
    const rgb = mix(base,tint,this.tintMix), grey = luminance(rgb);
    const saturated = mix([grey,grey,grey],rgb,.22+.78*clamp(this.morph+this.surf));
    const k = clamp(luminance(GOLD[i])/luminance(GOLD[2]),.35,1.25);
    return mix(saturated,(this.clickColor || PINK).map(v=>clamp(v*k,0,255)),clamp(flare));
  }
  copies(now) {
    const ring = Array.from({length:4},(_,i)=>({x:Math.cos(this.yaw+i*Math.PI/2)*12,z:Math.sin(this.yaw+i*Math.PI/2)*12}));
    const points = ring.map(p=>this.project(p.x,8-p.z*.36));
    const order = [0,1,2,3].sort((a,b)=>ring[a].z+ring[(a+1)%4].z-ring[b].z-ring[(b+1)%4].z);
    const scale = (1+.3*this.press)*(this.reduced ? 1 : 1+.06*Math.sin(now*6)*this.drop);
    if (!this.pair && !this.cross) return {points,order,copies:[{rotation:0,scale,x:0,y:0,alpha:1}]};
    const base = Math.atan2(points.reduce((s,p)=>s+p[1],0),points.reduce((s,p)=>s+p[0],0));
    const pull = 10*(1-.6*clamp(this.press)), u = [this.ux,this.uy], v = [-this.uy,this.ux];
    const copy = (axis,t,primary=false) => ({rotation:arc(Math.atan2(-axis[1],-axis[0])-base)*(primary ? t : 1),scale:scale*(primary ? 1 : .3+.7*t),x:axis[0]*pull*t,y:axis[1]*pull*t,alpha:primary ? 1 : t});
    const copies = [copy(u,this.pair,true),copy(u.map(n=>-n),this.pair)];
    if (this.cross) copies.push(copy(v,this.cross),copy(v.map(n=>-n),this.cross));
    return {points,order,copies};
  }
  draw(ctx,now) {
    this.drawHighlight(ctx,now);
    const {points,order,copies} = this.copies(now);
    for (const copy of copies) {
      if (copy.alpha <= .01 || copy.scale <= .02) continue;
      const cs = Math.cos(copy.rotation), sn = Math.sin(copy.rotation);
      const pt = points.map(([x,y])=>[(x*cs-y*sn)*copy.scale,(x*sn+y*cs)*copy.scale]);
      const fills = [0,1,2,3].map(i=>`rgba(${this.tone(i).map(v=>Math.round(clamp(v,0,255))).join(',')},${clamp(copy.alpha)})`);
      for (const face of order) {
        const light = (Math.cos(this.yaw+(face+.5)*Math.PI/2-.8)+1)/2;
        rasterTriangle(ctx,pt[face],pt[(face+1)%4],Math.floor(copy.x),Math.floor(copy.y),light,fills);
      }
    }
    if (this.busyT > .01) drawOrb(ctx,18,20,7*(.6+.4*this.busyT),this.tone(2,0),this.busySpin,this.busyT,this.reduced);
  }
  drawHighlight(ctx,now) {
    if (this.reduced) return;
    const age = now-this.pingBorn;
    if (age >= 0 && age < .35) {
      const t = age/.35, radius = 6+24*(1-(1-t)**3), fade = (1-t)**2;
      for (let k=0;k<24;k++) {
        if (fade*1.15 <= (BAYER[(k*5)&15]+.5)/16) continue;
        const a = k*TAU/24+this.yaw;
        fill(ctx,Math.floor(Math.cos(a)*radius)-1,Math.floor(5+Math.sin(a)*radius*.72)-1,2,2,this.tone(2,0),.9);
      }
    }
    if (this.hover > .02) for (let k=0;k<2;k++) for (let tail=0;tail<4;tail++) {
      const a = this.orbit+k*Math.PI-tail*.22, size = 2-tail*.4;
      fill(ctx,Math.floor(Math.cos(a)*15)-size/2,Math.floor(6+Math.sin(a)*15*.6)-size/2,size,size,this.tone(tail ? 2 : 3,0),this.hover*(1-tail*.22));
    }
  }
}

function fill(ctx,x,y,w,h,rgb,alpha=1) {
  ctx.fillStyle = `rgba(${rgb.map(v=>Math.round(clamp(v,0,255))).join(',')},${clamp(alpha)})`;
  ctx.fillRect(Math.round(x),Math.round(y),Math.max(1,Math.round(x+w)-Math.round(x)),Math.max(1,Math.round(y+h)-Math.round(y)));
}
const edge = (a,b,x,y) => (b[0]-a[0])*(y-a[1])-(b[1]-a[1])*(x-a[0]);
function rasterTriangle(ctx,p,q,ox,oy,light,fills) {
  const tip = [0,0], area = edge(tip,p,q[0],q[1]);
  if (Math.abs(area)<.5) return;
  const minX = Math.floor(Math.min(0,p[0],q[0])), maxX = Math.ceil(Math.max(0,p[0],q[0]));
  const minY = Math.floor(Math.min(0,p[1],q[1])), maxY = Math.ceil(Math.max(0,p[1],q[1])), height = Math.max(1,maxY-minY);
  for (let y=minY;y<=maxY;y++) {
    let run = minX, previous = -1;
    for (let x=minX;x<=maxX+1;x++) {
      let tone = -1;
      if (x<=maxX) {
        const e0 = edge(tip,p,x+.5,y+.5), e1 = edge(p,q,x+.5,y+.5), e2 = edge(q,tip,x+.5,y+.5);
        if (area>0 ? e0>=0 && e1>=0 && e2>=0 : e0<=0 && e1<=0 && e2<=0) {
          const shade = clamp(.2+light*2.45+(y-minY)/height*.25,0,3), low = Math.floor(shade);
          tone = Math.min(3,low+(shade-low>(BAYER[(y&3)*4+(x&3)]+.5)/16 ? 1 : 0));
        }
      }
      if (tone!==previous) {
        if (previous>=0) { ctx.fillStyle=fills[previous]; ctx.fillRect(ox+run,oy+y,x-run,1); }
        run = x; previous = tone;
      }
    }
  }
}

// UltraKit's +0.8 low-poly orb, six turning longitude faces, three latitudes.
function drawOrb(ctx,cx,cy,radius,tint,spin,alpha,reduced) {
  const cs = Math.cos(reduced ? 0 : spin), sn = Math.sin(reduced ? 0 : spin);
  let lx = -.45+.75*Math.cos(spin)*.6, ly = -.55-.75*(.5+Math.sin(spin)*.2), lz = .7;
  const ll = Math.hypot(lx,ly,lz); lx/=ll; ly/=ll; lz/=ll;
  const tones = [tint.map(v=>v*.24),tint.map(v=>v*.58),tint,tint.map(v=>v+(255-v)*.62)];
  const ir = Math.ceil(radius), sector = TAU/6;
  for (let y=-ir;y<=ir;y++) for (let x=-ir;x<=ir;x++) {
    let nx = (x+.5)/radius, ny = (y+.5)/radius;
    const rr = nx*nx+ny*ny;
    if (rr>1) continue;
    let nz = Math.sqrt(1-rr);
    const a = Math.atan2(ny,nx)+(reduced ? 0 : spin)*.5;
    const local = a-sector*Math.floor(a/sector)-sector*.5;
    if (Math.sqrt(rr)>Math.cos(sector*.5)/Math.cos(local)) continue;
    const lon = Math.atan2(nx*cs+nz*sn,-nx*sn+nz*cs), lat = Math.asin(clamp(ny,-1,1));
    const lq = (Math.floor(lon/sector)+.5)*sector, dr = Math.PI/3, tq = (Math.floor(lat/dr)+.5)*dr;
    const fx = Math.sin(lq)*Math.cos(tq), fy = Math.sin(tq), fz = Math.cos(lq)*Math.cos(tq);
    nx = fx*cs-fz*sn; ny = fy; nz = Math.max(.05,fx*sn+fz*cs);
    const length = Math.hypot(nx,ny,nz); nx/=length; ny/=length; nz/=length;
    const diff = Math.max(0,nx*lx+ny*ly+nz*lz), band = Math.abs(Math.sin(Math.atan2(nx*cs+nz*sn,-nx*sn+nz*cs)*2))<.2 ? .55 : 0;
    const shade = clamp(.15+2.55*diff-band+1.3*.35+.35*(1-nz)*(diff>.5 ? 1 : 0),0,3), low = Math.floor(shade);
    const tone = Math.min(3,low+(shade-low>(BAYER[(y&3)*4+(x&3)]+.5)/16 ? 1 : 0));
    fill(ctx,cx+x,cy+y,1,1,tones[tone],alpha);
  }
}
