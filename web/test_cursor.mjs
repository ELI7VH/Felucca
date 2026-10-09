// Adapted from WaveLoop web/scripts/ultra-cursor.test.mjs.
// Run: node --test web/test_cursor.mjs
import test from 'node:test';
import assert from 'node:assert/strict';
import { UltraCursor, cursorState } from './brand/ultra-cursor.js';

const states = ['idle','interactive','surface','text','resize-ew','resize-ns','resize-nwse','resize-nesw','move','drop','no'];
const settle = (cursor,state,options={}) => { for(let i=0;i<240;i++) cursor.step(1/60,state,{now:i/60,...options}); };

test('every UltraKit pose retains the hotspot through transitions and pressed forms',()=>{
  const cursor = new UltraCursor();
  for(const state of states) for(const pressed of [true,false]) {
    for(let i=0;i<60;i++) {
      cursor.step(1/60,state,{now:i/60,pressed});
      assert.deepEqual(cursor.tip,[0,0]);
      for(const copy of cursor.copies(i/60).copies) {
        assert.ok([copy.x,copy.y,copy.scale,copy.rotation,copy.alpha].every(Number.isFinite));
        assert.ok(Math.hypot(copy.x,copy.y)<=10.01);
      }
    }
  }
  assert.equal(cursorState('unknown'),'idle');
});

test('the pair follows every axis and MOVE grows a second perpendicular pair',()=>{
  for(const [state,axis] of [['resize-ew',[1,0]],['resize-ns',[0,1]],['resize-nwse',[Math.SQRT1_2,Math.SQRT1_2]],['resize-nesw',[-Math.SQRT1_2,Math.SQRT1_2]]]) {
    const cursor = new UltraCursor();settle(cursor,state);
    const copies = cursor.copies(4).copies;
    assert.equal(copies.length,2);
    assert.ok(Math.abs(copies[0].x-10*axis[0])<.01);
    assert.ok(Math.abs(copies[0].y-10*axis[1])<.01);
    assert.ok(Math.abs(copies[0].x+copies[1].x)<.01);
    assert.ok(Math.abs(copies[0].y+copies[1].y)<.01);
  }
  const cursor = new UltraCursor();settle(cursor,'move');
  assert.equal(cursor.copies(4).copies.length,4);
});

test('press animates the existing pose, bursts once, remains pink while held and releases stably',()=>{
  const cursor = new UltraCursor();settle(cursor,'resize-ew');
  cursor.step(1/60,'resize-ew',{pressed:true});assert.equal(cursor.burst,true);
  let overshoot=false;
  for(let i=0;i<120;i++) {
    cursor.step(1/60,'resize-ew',{pressed:true});
    overshoot ||= cursor.press>1;
    assert.equal(cursor.burst,false);
    assert.equal(cursor.state,'resize-ew');
  }
  assert.ok(overshoot);
  assert.ok(Math.abs(cursor.press-1)<.001);
  assert.ok(cursor.tone(2)[0]>240 && cursor.tone(2)[2]>150);
  assert.ok(Math.abs(cursor.copies(2).copies[0].x-4)<.01);
  for(let i=0;i<120;i++) cursor.step(1/60,'resize-ew',{pressed:false});
  assert.ok(cursor.press<.001);
  for(let i=0;i<20;i++) cursor.step(10,'move',{pressed:i%2===0});
  assert.ok(Number.isFinite(cursor.press) && cursor.press<2);
});

test('reduced motion snaps poses and pink, with no ping, turn, pulse or burst',()=>{
  const cursor=new UltraCursor(),yaw=cursor.yaw;
  settle(cursor,'idle',{reduced:true});assert.equal(cursor.yaw,yaw);
  cursor.step(.1,'text',{reduced:true,pressed:true,now:5});
  assert.equal(cursor.surf,1);assert.equal(cursor.press,1);assert.equal(cursor.burst,false);
  assert.equal(cursor.pingBorn,-1e9);
  cursor.step(.1,'move',{reduced:true,pressed:false,now:6});
  assert.equal(cursor.cross,1);assert.equal(cursor.press,0);
  cursor.step(.1,'drop',{reduced:true,now:7});
  assert.equal(cursor.copies(7).copies[0].scale,cursor.copies(7.5).copies[0].scale);
});

test('busy orb appears only after 150ms, clears and starts a fresh delay on the next job',()=>{
  const cursor=new UltraCursor();
  cursor.step(.01,'interactive',{now:1,busy:true,reduced:true});assert.equal(cursor.busyT,0);
  cursor.step(.1,'interactive',{now:1.14,busy:true,reduced:true});assert.equal(cursor.busyT,0);
  cursor.step(.01,'interactive',{now:1.16,busy:true,reduced:true});assert.equal(cursor.busyT,1);
  cursor.step(.01,'interactive',{now:1.2,busy:false,reduced:true});assert.equal(cursor.busyT,0);
  cursor.step(.01,'interactive',{now:2,busy:true,reduced:true});assert.equal(cursor.busyT,0);
});

test('all poses, hover effects and busy orb rasterize inside the bounded damage region',()=>{
  const cursor=new UltraCursor();let count=0;
  const ctx={fillStyle:'',fillRect(x,y,w,h){
    assert.ok([x,y,w,h].every(Number.isFinite));assert.ok(w>0 && h>0);
    assert.ok(x>=-64 && y>=-64 && x+w<=64 && y+h<=64);count++;
  }};
  for(const state of states) {
    settle(cursor,state,{pressed:true,busy:true});cursor.draw(ctx,4);
  }
  assert.ok(count>100);
});
