// RuneToolsX panel: Puzzle-box solver (24-puzzle optimal solver + clue puzzle UI).
// Spliced inline into client.html at load; shares its global scope (no IIFE).

  const PuzzleKit = (function () {
"use strict";
// 24-puzzle (5x5) optimal solver: Walking-Distance heuristic via a perfect
// composition-rank + rank/select-compressed table + incremental IDA*.
// blobBytes: Uint8Array of wd_table.bin (header + presence bitvector + values).
// Board: int[25] row-major, 0..23 tiles, 24 = blank. Goal board[i] === i.
// A move = board index of the tile clicked (slides into the adjacent blank).

function createWDSolver(blob){
  // 1) Perfect contingency-table rank of a 5x5 WD line/group count-matrix: rank(blankLine,
  //    M25) in [0, 65650495), M25[line*5 + group]. By square symmetry the SAME rank and
  //    table serve BOTH the vertical (rows) and horizontal (columns) WD.
  const COLCAPS = [5,5,5,5,4];           // group sizes (col sums); blank in group 4
  function rowSum(bl, ri){ return ri===bl ? 4 : 5; }
  function capsKey(c){ return c[0]+c[1]*6+c[2]*36+c[3]*216+c[4]*1296; }

  // f(bl,ri,caps) = #completions of rows ri..4. Dense Float32 (counts < 2^24, exact).
  const fArr = new Float32Array(5*5*7776).fill(-1);
  function f(bl, ri, c){
    if (ri===5){ return (c[0]|c[1]|c[2]|c[3]|c[4])===0 ? 1 : 0; }
    const key=(bl*5+ri)*7776 + capsKey(c);
    const hit=fArr[key]; if(hit>=0) return hit;
    const s=rowSum(bl,ri); let tot=0; const nc=[c[0],c[1],c[2],c[3],c[4]];
    const h0=Math.min(s,c[0]);
    for(let v0=0;v0<=h0;v0++){ nc[0]=c[0]-v0; const r1=s-v0; const h1=Math.min(r1,c[1]);
      for(let v1=0;v1<=h1;v1++){ nc[1]=c[1]-v1; const r2=r1-v1; const h2=Math.min(r2,c[2]);
        for(let v2=0;v2<=h2;v2++){ nc[2]=c[2]-v2; const r3=r2-v2; const h3=Math.min(r3,c[3]);
          for(let v3=0;v3<=h3;v3++){ nc[3]=c[3]-v3; const r4=r3-v3;
            if(r4<=c[4]){ nc[4]=c[4]-r4; tot+=f(bl,ri+1,nc); } } } } }
    fArr[key]=tot; return tot;
  }

  // H(bl,ri,col,rem,caps) gives O(1)-per-column row ranking; 138981 entries, packed below
  // into an open-addressing table.
  let HK, HV, HMASK;
  const Hbuild = new Map();
  function Hcompute(bl, ri, col, rem, c){
    if(col===5){ return rem===0 ? f(bl,ri+1,c) : 0; }
    const key=((((bl*5+ri)*6+col)*6+rem)*7776 + capsKey(c));
    const hit=Hbuild.get(key); if(hit!==undefined) return hit;
    let tot=0; const nc=[c[0],c[1],c[2],c[3],c[4]]; const hi=Math.min(rem,c[col]);
    for(let v=0;v<=hi;v++){ nc[col]=c[col]-v; tot+=Hcompute(bl,ri,col+1,rem-v,nc); nc[col]=c[col]; }
    Hbuild.set(key,tot); return tot;
  }
  // Warm f + H over every reachable (bl,ri,caps), then pack H into open-addressing.
  function warm(){
    const seen=new Set();
    function rec(bl, ri, caps){
      if(ri===5) return;
      const vk=(bl*5+ri)*7776+capsKey(caps);
      if(seen.has(vk)) return; seen.add(vk);
      Hcompute(bl, ri, 0, rowSum(bl,ri), caps);          // fills all H sub-states
      const s=rowSum(bl,ri); const nc=caps.slice();
      const h0=Math.min(s,caps[0]);
      for(let v0=0;v0<=h0;v0++){ nc[0]=caps[0]-v0; const r1=s-v0; const h1=Math.min(r1,caps[1]);
        for(let v1=0;v1<=h1;v1++){ nc[1]=caps[1]-v1; const r2=r1-v1; const h2=Math.min(r2,caps[2]);
          for(let v2=0;v2<=h2;v2++){ nc[2]=caps[2]-v2; const r3=r2-v2; const h3=Math.min(r3,caps[3]);
            for(let v3=0;v3<=h3;v3++){ nc[3]=caps[3]-v3; const r4=r3-v3;
              if(r4<=caps[4]){ nc[4]=caps[4]-r4; rec(bl, ri+1, nc); } } } } }
    }
    for(let bl=0;bl<5;bl++){ f(bl,0,COLCAPS.slice()); rec(bl,0,COLCAPS.slice()); }
    let p=1; while(p < Hbuild.size*1.4) p<<=1; HMASK=p-1;
    HK=new Uint32Array(p).fill(0xffffffff); HV=new Float32Array(p);
    for(const [key,val] of Hbuild){
      let i=(Math.imul(key^(key>>>15),2246822519)>>>0)&HMASK;
      while(HK[i]!==0xffffffff) i=(i+1)&HMASK;
      HK[i]=key; HV[i]=val;
    }
    Hbuild.clear();
  }
  function H(bl,ri,col,rem,c){
    if(col===5){ return rem===0 ? f(bl,ri+1,c) : 0; }
    const key=((((bl*5+ri)*6+col)*6+rem)*7776 + capsKey(c));
    let i=(Math.imul(key^(key>>>15),2246822519)>>>0)&HMASK;
    while(HK[i]!==0xffffffff){ if(HK[i]===key) return HV[i]; i=(i+1)&HMASK; }
    return 0; // unreachable (never queried for valid matrices)
  }

  const Cfull = f(0,0,COLCAPS.slice());   // 13130099
  const TOTALRANK = 5*Cfull;              // 65650495
  warm();

  const rcaps=[0,0,0,0,0];
  function rank(bl, M){
    rcaps[0]=5;rcaps[1]=5;rcaps[2]=5;rcaps[3]=5;rcaps[4]=4;
    let rk=bl*Cfull;
    for(let ri=0;ri<4;ri++){
      let rem=bl===ri?4:5; const off=ri*5;
      for(let col=0;col<5;col++){
        const xv=M[off+col]; const bc=rcaps[col];
        for(let v=0;v<xv;v++){ rcaps[col]=bc-v; rk+=H(bl,ri,col+1,rem-v,rcaps); }
        rcaps[col]=bc-xv; rem-=xv;
      }
    }
    return rk;
  }

  // 2) Load the rank/select-compressed table + build superblock popcounts.
  if(blob[0]!==87||blob[1]!==68||blob[2]!==84||blob[3]!==49) throw new Error("bad WD blob magic");
  const dv=new DataView(blob.buffer, blob.byteOffset, blob.byteLength);
  const total=dv.getUint32(4,true), R=dv.getUint32(8,true), cap=blob[12];
  if(total!==TOTALRANK) throw new Error("blob/rank size mismatch");
  const bvBytes=(total+7)>>3;
  const bvOff=blob.byteOffset+16;
  // Zero-copy views into the blob; the u32 bitvector view needs 4-byte alignment, else copy.
  let bvU32, valBase;
  if((bvOff&3)===0){ bvU32=new Uint32Array(blob.buffer, bvOff, bvBytes>>2); valBase=blob.buffer; }
  else { const c=blob.slice(16); bvU32=new Uint32Array(c.buffer, c.byteOffset, bvBytes>>2); valBase=c.buffer; }
  const valOff=((bvOff&3)===0?bvOff:0)+bvBytes;
  const values=new Uint8Array(valBase, valOff, R);
  // superblock popcount, SB = 1024 bits = 32 u32 words
  const nWords=bvU32.length;
  const nSB=(nWords>>5)+1;
  const sbCount=new Uint32Array(nSB);
  let acc=0;
  for(let sb=0; sb<nSB; sb++){
    sbCount[sb]=acc;
    const base=sb<<5; const end=Math.min(base+32, nWords);
    for(let w=base; w<end; w++){ let x=bvU32[w]; x=x-((x>>>1)&0x55555555); x=(x&0x33333333)+((x>>>2)&0x33333333); x=(x+(x>>>4))&0x0f0f0f0f; acc+=(Math.imul(x,0x01010101)>>>24); }
  }
  function popc(x){ x=x-((x>>>1)&0x55555555); x=(x&0x33333333)+((x>>>2)&0x33333333); x=(x+(x>>>4))&0x0f0f0f0f; return Math.imul(x,0x01010101)>>>24; }
  function lookup(r){
    const w=r>>>5, b=r&31;
    const word=bvU32[w];
    if(((word>>>b)&1)===0) return cap;             // not stored => true WD > cap
    const sb=w>>>5;
    let cnt=sbCount[sb];
    for(let q=sb<<5; q<w; q++) cnt+=popc(bvU32[q]);
    cnt+=popc(word & ((b===0?0:(0xffffffff>>>(32-b)))));
    return values[cnt];
  }

  // 2b) WD line memo for lookup(rank(bl,M)), the hottest call in the search. The key is
  //     EXACT, not a fingerprint: each line's counts for groups 0..3 as base-6 digits
  //     (group 4 is implied, since bl fixes every line sum: 4 for the blank's line, 5
  //     otherwise), packed into two 32-bit words. Direct-mapped; a clash just overwrites.
  const WDC_N = 1 << 17, WDC_MASK = WDC_N - 1;
  const wdcK1 = new Int32Array(WDC_N), wdcK2 = new Int32Array(WDC_N).fill(-1), wdcV = new Uint8Array(WDC_N);
  function wdSide(bl, C) {                                  // C[line*5 + group]
    const r0 = C[0] + 6*C[1] + 36*C[2] + 216*C[3];
    const r1 = C[5] + 6*C[6] + 36*C[7] + 216*C[8];
    const r2 = C[10] + 6*C[11] + 36*C[12] + 216*C[13];
    const r3 = C[15] + 6*C[16] + 36*C[17] + 216*C[18];
    const r4 = C[20] + 6*C[21] + 36*C[22] + 216*C[23];
    const k1 = (r0 + 1296*r1 + 1679616*r2) | 0;             // k2 >= 0 always, so -1 is a safe empty slot
    const k2 = (r3 + 1296*r4 + 1679616*bl) | 0;
    let i = (Math.imul(k1 ^ 0x9e3779b9, 2246822519) ^ Math.imul(k2 + 0x165667b1, 3266489917)) >>> 0;
    i = (i ^ (i >>> 15)) & WDC_MASK;
    if (wdcK2[i] === k2 && wdcK1[i] === k1) return wdcV[i];
    const v = lookup(rank(bl, C));
    wdcK1[i] = k1; wdcK2[i] = k2; wdcV[i] = v;
    return v;
  }

  // 3) Incremental IDA* on the board, WD looked up via rank+lookup per move.
  const vCount=new Int8Array(25), hCount=new Int8Array(25);
  let vBR=0,hBC=0,vWD=0,hWD=0; const board=new Int8Array(25); let blankPos=24;
  function vLook(){ return lookup(rank(vBR,vCount)); }
  function hLook(){ return lookup(rank(hBC,hCount)); }
  function initFromBoard(src){
    for(let i=0;i<25;i++) board[i]=src[i];
    blankPos=board.indexOf(24); vCount.fill(0); hCount.fill(0);
    for(let i=0;i<25;i++){ const t=board[i]; if(t===24)continue;
      const r=(i/5)|0,c=i%5,gr=(t/5)|0,gc=t%5; vCount[r*5+gr]++; hCount[c*5+gc]++; }
    vBR=(blankPos/5)|0; hBC=blankPos%5; vWD=vLook(); hWD=hLook();
  }
  function doMove(cz){ const bz=blankPos,t=board[cz]; board[bz]=t; board[cz]=24; blankPos=cz;
    if(((bz-cz)===1)||((cz-bz)===1)){ const bc=bz%5,cc=cz%5,g=t%5; hCount[cc*5+g]--; hCount[bc*5+g]++; hBC=cc; hWD=hLook(); }
    else { const br=(bz/5)|0,cr=(cz/5)|0,g=(t/5)|0; vCount[cr*5+g]--; vCount[br*5+g]++; vBR=cr; vWD=vLook(); } }
  function undoMove(cz,bz,t,sVBR,sHBC,sVWD,sHWD){ board[cz]=t; board[bz]=24; blankPos=bz;
    if(((bz-cz)===1)||((cz-bz)===1)){ const bc=bz%5,cc=cz%5,g=t%5; hCount[bc*5+g]--; hCount[cc*5+g]++; hBC=sHBC; hWD=sHWD; vBR=sVBR; vWD=sVWD; }
    else { const br=(bz/5)|0,cr=(cz/5)|0,g=(t/5)|0; vCount[br*5+g]--; vCount[cr*5+g]++; vBR=sVBR; vWD=sVWD; hBC=sHBC; hWD=sHWD; } }
  function neighbors(bz,out){ let n=0; const r=(bz/5)|0,c=bz%5;
    if(c>0)out[n++]=bz-1; if(c<4)out[n++]=bz+1; if(r>0)out[n++]=bz-5; if(r<4)out[n++]=bz+5; return n; }

  let deadline=0,nodes=0,timedOut=false; const path=new Int32Array(256); let solMoves=null;
  function search(g,bound,prevBlank){
    const fv=g+vWD+hWD; if(fv>bound) return fv;
    if(vWD===0&&hWD===0){ solMoves=Array.prototype.slice.call(path.subarray(0,g)); return -1; }
    if((++nodes&8191)===0 && Date.now()>=deadline){ timedOut=true; return -1; }
    let min=Infinity; const nb=[0,0,0,0]; const bz=blankPos; const cnt=neighbors(bz,nb);
    for(let i=0;i<cnt;i++){ const cz=nb[i]; if(cz===prevBlank)continue;
      const sVBR=vBR,sHBC=hBC,sVWD=vWD,sHWD=hWD; const t=board[cz];
      doMove(cz); path[g]=cz; const r=search(g+1,bound,bz);
      undoMove(cz,bz,t,sVBR,sHBC,sVWD,sHWD);
      if(r===-1) return -1; if(r<min) min=r; }
    return min;
  }
  function solveOptimal(src,dl,ub){
    initFromBoard(src); deadline=dl; nodes=0; timedOut=false; solMoves=null;
    if(vWD===0&&hWD===0) return {moves:[],optimal:true,nodes:0};
    let bound=vWD+hWD; const capUB=(ub==null)?1e9:ub;
    while(true){ solMoves=null; const t=search(0,bound,-1);
      if(timedOut) return {moves:null,optimal:false,nodes};
      if(t===-1&&solMoves) return {moves:solMoves,optimal:true,nodes};
      if(t===Infinity) return {moves:null,optimal:true,provedUB:false,nodes};
      bound=t; if(bound>=capUB) return {moves:null,optimal:true,provedUB:true,nodes}; }
  }
  function verify(src,moves){ const b=Array.prototype.slice.call(src);
    for(let i=0;i<moves.length;i++){ const click=moves[i]; const blank=b.indexOf(24);
      const bx=blank%5,by=(blank/5)|0,cx=click%5,cy=(click/5)|0;
      if(Math.abs(bx-cx)+Math.abs(by-cy)!==1) return {ok:false,reason:"non-adjacent"};
      b[blank]=b[click]; b[click]=24; }
    for(let i=0;i<25;i++) if(b[i]!==i) return {ok:false,reason:"not goal"}; return {ok:true}; }
  function rootH(src){ initFromBoard(src); return vWD+hWD; }

  // 3b) Weighted A* deep-board fallback: a closed set expands each state once, so unlike
  //     weighted IDA* it can't thrash. f = g + W*h, W>1, no reopening => length ~W*optimal.
  const _vc=new Int8Array(25), _hc=new Int8Array(25);
  function _wdCounts(arr){                              // returns the blank index
    _vc.fill(0); _hc.fill(0); let bp=24;
    for(let i=0;i<25;i++){ const t=arr[i]; if(t===24){ bp=i; continue; }
      const r=(i/5)|0,c=i%5,gr=(t/5)|0,gc=t%5; _vc[r*5+gr]++; _hc[c*5+gc]++; }
    return bp;
  }
  function wdOf(arr){
    const bp=_wdCounts(arr);
    return wdSide((bp/5)|0,_vc) + wdSide(bp%5,_hc);
  }
  function wdOfRaw(arr){                                // same, bypassing the memo (validation)
    const bp=_wdCounts(arr);
    return lookup(rank((bp/5)|0,_vc)) + lookup(rank(bp%5,_hc));
  }

  // 3c) Standalone incremental WD state, so several searches can each hold their own. peek(cz)
  //     = WD after sliding cz into the blank, WITHOUT committing; only one axis changes.
  function newInc(){
    const vC=new Int32Array(25), hC=new Int32Array(25), bd=new Int32Array(25);
    let bp=24, vw=0, hw=0;
    const out=new Int32Array(2);                        // [vWD, hWD] after init/peek
    function init(src){
      for(let i=0;i<25;i++) bd[i]=src[i];
      bp=24; for(let i=0;i<25;i++) if(bd[i]===24){ bp=i; break; }
      vC.fill(0); hC.fill(0);
      for(let i=0;i<25;i++){ const t=bd[i]; if(t===24) continue;
        const r=(i/5)|0,c=i%5,gr=(t/5)|0,gc=t%5; vC[r*5+gr]++; hC[c*5+gc]++; }
      vw=wdSide((bp/5)|0,vC); hw=wdSide(bp%5,hC); out[0]=vw; out[1]=hw; return vw+hw;
    }
    function peek(cz){
      const bz=bp, t=bd[cz];
      if(cz===bz-1||cz===bz+1){                          // horizontal slide: only hWD moves
        const bc=bz%5, cc=cz%5, g=t%5;
        hC[cc*5+g]--; hC[bc*5+g]++;
        const nh=wdSide(cc,hC);
        hC[cc*5+g]++; hC[bc*5+g]--;
        out[0]=vw; out[1]=nh; return vw+nh;
      }
      const br=(bz/5)|0, cr=(cz/5)|0, g=(t/5)|0;
      vC[cr*5+g]--; vC[br*5+g]++;
      const nv=wdSide(cr,vC);
      vC[cr*5+g]++; vC[br*5+g]--;
      out[0]=nv; out[1]=hw; return nv+hw;
    }
    function commit(cz,nv,nh){
      const bz=bp, t=bd[cz]; bd[bz]=t; bd[cz]=24; bp=cz;
      if(cz===bz-1||cz===bz+1){ const bc=bz%5,cc=cz%5,g=t%5; hC[cc*5+g]--; hC[bc*5+g]++; }
      else { const br=(bz/5)|0,cr=(cz/5)|0,g=(t/5)|0; vC[cr*5+g]--; vC[br*5+g]++; }
      vw=nv; hw=nh;
    }
    function uncommit(cz,bz,ov,oh){
      const t=bd[bz]; bd[cz]=t; bd[bz]=24; bp=bz;
      if(cz===bz-1||cz===bz+1){ const bc=bz%5,cc=cz%5,g=t%5; hC[cc*5+g]++; hC[bc*5+g]--; }
      else { const br=(bz/5)|0,cr=(cz/5)|0,g=(t/5)|0; vC[cr*5+g]++; vC[br*5+g]--; }
      vw=ov; hw=oh;
    }
    return { init, peek, commit, uncommit, out };
  }
  function isGoalArr(a){ for(let i=0;i<25;i++) if(a[i]!==i) return false; return true; }
  function keyOf(a){ return String.fromCharCode(a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7],a[8],a[9],a[10],a[11],a[12],a[13],a[14],a[15],a[16],a[17],a[18],a[19],a[20],a[21],a[22],a[23],a[24]); }
  // binary min-heap of node ids keyed by f (parallel arrays, no per-node objects)
  function solveWAStar(src,dl,W){
    if(isGoalArr(src)) return {moves:[]};
    const startKey=keyOf(src);
    const g=new Map([[startKey,0]]);                   // key -> best g
    const par=new Map();
    const arrOf=new Map([[startKey,src.slice()]]);     // key -> board array (for expansion)
    const hf=[], hk=[]; let hn=0;                      // heap: f values + keys
    function push(f,k){ let i=hn++; hf[i]=f; hk[i]=k; while(i>0){ const p=(i-1)>>1; if(hf[p]<=hf[i])break; const tf=hf[p];hf[p]=hf[i];hf[i]=tf; const tk=hk[p];hk[p]=hk[i];hk[i]=tk; i=p; } }
    function pop(){ const k=hk[0],f=hf[0]; hn--; if(hn>0){ hf[0]=hf[hn]; hk[0]=hk[hn]; let i=0; while(true){ const l=2*i+1,r=l+1; let m=i; if(l<hn&&hf[l]<hf[m])m=l; if(r<hn&&hf[r]<hf[m])m=r; if(m===i)break; const tf=hf[m];hf[m]=hf[i];hf[i]=tf; const tk=hk[m];hk[m]=hk[i];hk[i]=tk; i=m; } } return {k,f}; }
    push(W*wdOf(src), startKey);
    let expanded=0;
    const inc=newInc();
    while(hn>0){
      if((++expanded&2047)===0 && Date.now()>=dl) return {moves:null,expanded};
      const {k}=pop();
      const arr=arrOf.get(k); if(arr===undefined) continue;     // stale heap entry
      if(isGoalArr(arr)){
        const moves=[]; let cur=k; while(cur!==startKey){ const p=par.get(cur); moves.push(p.click); cur=p.pk; } moves.reverse();
        return {moves,expanded};
      }
      const cg=g.get(k); arrOf.delete(k);                       // closed: drop its array to cap memory
      const blank=arr.indexOf(24), bx=blank%5,by=(blank/5)|0;
      const nb=[]; if(bx>0)nb.push(blank-1); if(bx<4)nb.push(blank+1); if(by>0)nb.push(blank-5); if(by<4)nb.push(blank+5);
      inc.init(arr);                                           // one full WD build per expansion...
      for(let j=0;j<nb.length;j++){ const click=nb[j];
        const hv=inc.peek(click);                              // ...then one line lookup per child
        const na=arr.slice(); na[blank]=na[click]; na[click]=24;
        const nk=keyOf(na), ng=cg+1; const old=g.get(nk);
        if(old!==undefined && old<=ng) continue;                // no reopening
        g.set(nk,ng); par.set(nk,{pk:k,click}); arrOf.set(nk,na);
        push(ng + W*hv, nk);
      }
    }
    return {moves:null,expanded};
  }
  // Panel entry point: proven-optimal within budget, else the shortest weighted A* result
  // found inside it. Returns {moves, optimal, len, weight?}.
  function solveGuide(src,budgetMs,opts){ opts=opts||{};
    const start=Date.now(), MARGIN=40, dl=start+(budgetMs||600)-MARGIN;
    const OPT_SLICE=opts.optSlice==null?300:opts.optSlice;
    const h0=rootH(src);
    if(h0===0) return {moves:[],optimal:true,len:0,h0:0,ms:0};
    const ro=solveOptimal(src, Math.min(dl, start+OPT_SLICE), null);
    if(ro.optimal && ro.moves) return {moves:ro.moves,optimal:true,len:ro.moves.length,h0,ms:Date.now()-start};
    // High weight first (near-greedy, near-instant), then lower weights while budget remains.
    const Ws=opts.weights||[8,3,1.8];
    let best=null;
    for(let i=0;i<Ws.length;i++){ if(Date.now()>=dl) break;
      const rw=solveWAStar(src,dl,Ws[i]);
      if(rw.moves && (!best || rw.moves.length<best.len))
        best={moves:rw.moves,optimal:false,weight:Ws[i],len:rw.moves.length,h0,ms:Date.now()-start};
    }
    return best || {moves:null,optimal:false,h0,ms:Date.now()-start};
  }

  const ramBytes = fArr.byteLength + HK.byteLength + HV.byteLength +
                   bvU32.byteLength + sbCount.byteLength + values.byteLength +
                   wdcK1.byteLength + wdcK2.byteLength + wdcV.byteLength;
  return { solveOptimal, solveWAStar, solveGuide, verify, rootH, rank, lookup, cap, R,
           wdOf, wdOfRaw, newInc,
           ramBytes, Hentries: HK ? HK.length : 0 };
}


// Production 24-puzzle optimal solver for the panel; createSession(board).step(nodeBudget)
// is the resumable form the UI drives so the client never blocks.
// Heuristic = max(WalkingDistance, additivePDB(board), additivePDB(reflect)); all three
// admissible => the max is admissible => IDA* is optimal. PDB = 5-5-5-5-4 disjoint additive
// pattern database; diagonal reflection is distance-preserving so PDB(reflect(s)) is also a
// valid lower bound.
const N = 25;

function createPuzzleSolver(wdBlob, pdbBlob) {
  const WD = createWDSolver(wdBlob);

  // ---- additive PDB heuristic ----
  if (pdbBlob[0] !== 80 || pdbBlob[1] !== 68 || pdbBlob[2] !== 66 || pdbBlob[3] !== 49) throw new Error("bad PDB magic");
  const dv = new DataView(pdbBlob.buffer, pdbBlob.byteOffset, pdbBlob.byteLength);
  let o = 4; const gc = pdbBlob[o++]; const G = [];
  for (let g = 0; g < gc; g++) {
    const k = pdbBlob[o++]; const tiles = [];
    for (let i = 0; i < k; i++) tiles.push(pdbBlob[o++]);
    const sz = dv.getUint32(o, true); o += 4;
    const vals = pdbBlob.subarray(o, o + sz); o += sz;
    const slot = new Int32Array(N).fill(-1); tiles.forEach((t, i) => slot[t] = i);
    G.push({ k, tiles, vals, slot, pos: new Int32Array(k) });
  }
  function permRank(items, m) {
    let idx = 0; let used = 0;                          // 25-bit "used" mask
    for (let j = 0; j < m; j++) {
      const c = items[j]; let s = 0;
      for (let x = 0; x < c; x++) if ((used & (1 << x)) === 0) s++;
      idx = idx * (N - j) + s; used |= (1 << c);
    }
    return idx;
  }
  function pdbOf(board) {
    let h = 0;
    for (let gi = 0; gi < G.length; gi++) {
      const g = G[gi];
      for (let p = 0; p < N; p++) { const s = g.slot[board[p]]; if (s >= 0) g.pos[s] = p; }
      h += g.vals[permRank(g.pos, g.k)];
    }
    return h;
  }
  const RP = new Int32Array(N), RT = new Int32Array(N), rb = new Int32Array(N);
  for (let p = 0; p < N; p++) RP[p] = (p % 5) * 5 + ((p / 5) | 0);
  for (let t = 0; t < N; t++) RT[t] = (t === 24) ? 24 : (t % 5) * 5 + ((t / 5) | 0);
  // From-scratch heuristic: initialisation, plus the reference the incremental engine is
  // checked against in debug mode. Uses WD.wdOf's own scratch, so it never disturbs a search.
  function hMax(board) {
    let a = pdbOf(board);
    for (let p = 0; p < N; p++) rb[RP[p]] = RT[board[p]];
    const b = pdbOf(rb);
    if (b > a) a = b;
    const w = WD.wdOf(board);
    return w > a ? w : a;
  }
  function rootH(board) { return hMax(board); }

  // ---- incremental heuristic engine -------------------------------------------------
  // A slide moves one tile, so of the three components of hMax only ONE additive PDB group
  // changes on the board, one on the reflected board (a tile's reflection is a fixed tile,
  // hence a fixed group), and one WD axis. peek(cz) evaluates a child without touching the
  // state; commit() is O(1) and records what uncommit() needs, so a child's heuristic is
  // computed exactly once.
  const GC = G.length;
  const gOff = new Int32Array(GC + 1);
  for (let i = 0; i < GC; i++) gOff[i + 1] = gOff[i] + G[i].k;
  const gVals = G.map(g => g.vals), gK = new Int32Array(G.map(g => g.k));
  const PSZ = gOff[GC];
  const tGrp = new Int32Array(N).fill(-1), tSlot = new Int32Array(N).fill(-1);
  for (let gi = 0; gi < GC; gi++) for (let s = 0; s < G[gi].k; s++) { const t = G[gi].tiles[s]; tGrp[t] = gi; tSlot[t] = s; }
  const rGrp = new Int32Array(N).fill(-1), rSlot = new Int32Array(N).fill(-1);
  for (let t = 0; t < N; t++) { if (t === 24) continue; const rt = RT[t]; rGrp[t] = tGrp[rt]; rSlot[t] = tSlot[rt]; }
  function pc32(x) { x = x - ((x >>> 1) & 0x55555555); x = (x & 0x33333333) + ((x >>> 2) & 0x33333333); x = (x + (x >>> 4)) & 0x0f0f0f0f; return Math.imul(x, 0x01010101) >>> 24; }
  // Same ranking as permRank, with the "unused positions below c" count done by popcount.
  function prank(pos, off, m) {
    let idx = 0, used = 0;
    for (let j = 0; j < m; j++) { const c = pos[off + j]; idx = idx * (N - j) + (c - pc32(used & ((1 << c) - 1))); used |= (1 << c); }
    return idx;
  }

  function createEngine() {
    const inc = WD.newInc(), wout = inc.out;
    const eb = new Int32Array(N);
    const posA = new Int32Array(PSZ), posB = new Int32Array(PSZ);
    const hA = new Int32Array(GC), hB = new Int32Array(GC);
    const pk = new Int32Array(4);                      // last peek: [pdbA, pdbB, vWD, hWD]
    let ebl = 24, sA = 0, sB = 0, evw = 0, ehw = 0;
    function init(src) {
      for (let i = 0; i < N; i++) eb[i] = src[i];
      ebl = 24; for (let i = 0; i < N; i++) if (eb[i] === 24) { ebl = i; break; }
      for (let p = 0; p < N; p++) { const t = eb[p]; if (t === 24) continue;
        posA[gOff[tGrp[t]] + tSlot[t]] = p; posB[gOff[rGrp[t]] + rSlot[t]] = RP[p]; }
      sA = 0; sB = 0;
      for (let g = 0; g < GC; g++) {
        hA[g] = gVals[g][prank(posA, gOff[g], gK[g])]; sA += hA[g];
        hB[g] = gVals[g][prank(posB, gOff[g], gK[g])]; sB += hB[g];
      }
      inc.init(eb); evw = wout[0]; ehw = wout[1];
      return h();
    }
    function h() { let x = sA; if (sB > x) x = sB; const w = evw + ehw; if (w > x) x = w; return x; }
    function peek(cz) {                                // h of the child, state unchanged
      const bz = ebl, t = eb[cz];
      const gA = tGrp[t], iA = gOff[gA] + tSlot[t];
      posA[iA] = bz; const nA = gVals[gA][prank(posA, gOff[gA], gK[gA])]; posA[iA] = cz;
      const gB = rGrp[t], iB = gOff[gB] + rSlot[t];
      posB[iB] = RP[bz]; const nB = gVals[gB][prank(posB, gOff[gB], gK[gB])]; posB[iB] = RP[cz];
      const w = inc.peek(cz);
      let x = sA - hA[gA] + nA; const y = sB - hB[gB] + nB;
      if (y > x) x = y; if (w > x) x = w;
      pk[0] = nA; pk[1] = nB; pk[2] = wout[0]; pk[3] = wout[1];
      return x;
    }
    function commit(cz, nA, nB, nV, nH, und, uo) {     // und[uo..uo+7] = the undo record
      const bz = ebl, t = eb[cz], gA = tGrp[t], gB = rGrp[t];
      und[uo] = cz; und[uo + 1] = bz; und[uo + 2] = hA[gA]; und[uo + 3] = sA;
      und[uo + 4] = hB[gB]; und[uo + 5] = sB; und[uo + 6] = evw; und[uo + 7] = ehw;
      eb[bz] = t; eb[cz] = 24; ebl = cz;
      posA[gOff[gA] + tSlot[t]] = bz; sA += nA - hA[gA]; hA[gA] = nA;
      posB[gOff[gB] + rSlot[t]] = RP[bz]; sB += nB - hB[gB]; hB[gB] = nB;
      inc.commit(cz, nV, nH); evw = nV; ehw = nH;
    }
    function uncommit(und, uo) {
      const cz = und[uo], bz = und[uo + 1], t = eb[bz], gA = tGrp[t], gB = rGrp[t];
      eb[cz] = t; eb[bz] = 24; ebl = bz;
      posA[gOff[gA] + tSlot[t]] = cz; hA[gA] = und[uo + 2]; sA = und[uo + 3];
      posB[gOff[gB] + rSlot[t]] = RP[cz]; hB[gB] = und[uo + 4]; sB = und[uo + 5];
      evw = und[uo + 6]; ehw = und[uo + 7];
      inc.uncommit(cz, bz, evw, ehw);
    }
    return { init, peek, commit, uncommit, h, pk, board: eb, blank: function () { return ebl; } };
  }

  // Resumable (non-blocking) IDA* over the combined heuristic: explicit-stack iterative
  // deepening runnable in node-budget slices, driving the incremental engine. Every child's
  // heuristic is computed ONCE, in the parent, by engine.peek(), and children are expanded
  // in increasing f, so the first child over the bound ends the frame and its f is the
  // frame's min. opts.debug re-derives hMax from scratch per child and stops on a mismatch.
  const MAXD = 200;                       // optimal 24-puzzle solutions are <= 152 moves
  function createSession(src, opts) {
    opts = opts || {};
    const eng = createEngine(), pk = eng.pk;
    const fg = new Int32Array(MAXD + 1);      // g at each depth
    const fh = new Int32Array(MAXD + 1);      // h at each depth (from the parent's peek)
    const fprev = new Int32Array(MAXD + 1);   // blank position descended from
    const fcz = new Int32Array(MAXD + 1);     // move applied to descend from this frame
    const fmin = new Float64Array(MAXD + 1);  // min f over this frame's cut-off subtrees
    const fni = new Int32Array(MAXD + 1), fcnt = new Int32Array(MAXD + 1);
    const fent = new Uint8Array(MAXD + 1);
    const cCz = new Int32Array(MAXD * 4), cH = new Int32Array(MAXD * 4),   // sorted children
          cA = new Int32Array(MAXD * 4), cB = new Int32Array(MAXD * 4),
          cV = new Int32Array(MAXD * 4), cW = new Int32Array(MAXD * 4);
    const und = new Int32Array(MAXD * 8);     // per-depth undo records
    const dbg = !!opts.debug, dund = new Int32Array(8);
    let dbgChecks = 0, dbgFail = null;

    const h0 = eng.init(src);
    let bound = h0, top = 0, finished = false, moves = null, nodes = 1, childMin = Infinity;
    function resetIteration() {
      eng.init(src);
      top = 0; fg[0] = 0; fh[0] = h0; fprev[0] = -1; fent[0] = 0; fmin[0] = Infinity;
      childMin = Infinity;
    }
    if (h0 === 0) { finished = true; moves = []; } else resetIteration();

    // Generate this frame's children, heuristic-evaluated and insertion-sorted by h.
    function expand(d) {
      const base = d << 2, bz = eng.blank(), prev = fprev[d];
      const r = (bz / 5) | 0, c = bz % 5;
      let m = 0;
      for (let i = 0; i < 4; i++) {
        const cz = i === 0 ? (c > 0 ? bz - 1 : -1) : i === 1 ? (c < 4 ? bz + 1 : -1)
                 : i === 2 ? (r > 0 ? bz - 5 : -1) : (r < 4 ? bz + 5 : -1);
        if (cz < 0 || cz === prev) continue;
        const hv = eng.peek(cz); nodes++;
        if (dbg) {
          const nA = pk[0], nB = pk[1], nV = pk[2], nW = pk[3];
          eng.commit(cz, nA, nB, nV, nW, dund, 0);
          const ref = hMax(eng.board); dbgChecks++;
          if (ref !== hv && !dbgFail) dbgFail = { inc: hv, scratch: ref, board: Array.prototype.slice.call(eng.board) };
          eng.uncommit(dund, 0);
          pk[0] = nA; pk[1] = nB; pk[2] = nV; pk[3] = nW;
        }
        let j = m++;
        while (j > 0 && cH[base + j - 1] > hv) {
          const q = base + j, p = q - 1;
          cCz[q] = cCz[p]; cH[q] = cH[p]; cA[q] = cA[p]; cB[q] = cB[p]; cV[q] = cV[p]; cW[q] = cW[p];
          j--;
        }
        const s = base + j;
        cCz[s] = cz; cH[s] = hv; cA[s] = pk[0]; cB[s] = pk[1]; cV[s] = pk[2]; cW[s] = pk[3];
      }
      return m;
    }
    function popFrame() {
      const d = top, fv = fmin[d];
      if (d === 0) { childMin = fv; top = -1; return; }
      eng.uncommit(und, (d - 1) << 3);
      if (fv < fmin[d - 1]) fmin[d - 1] = fv;
      top = d - 1;
    }

    // Run up to `nodeBudget` loop iterations, then yield. Returns a status object.
    function step(nodeBudget) {
      if (finished) return { done: true, moves, optimal: true, nodes, bound };
      let work = 0;
      while (top >= 0) {
        if (++work > nodeBudget) return { done: false, optimal: true, nodes, bound };
        const d = top;
        if (!fent[d]) {
          fent[d] = 1;
          if (fh[d] === 0) {                  // h = 0 implies WD = 0 implies goal
            finished = true; moves = [];
            for (let i = 0; i < d; i++) moves.push(fcz[i]);
            return { done: true, moves, optimal: true, nodes, bound };
          }
          fmin[d] = Infinity; fni[d] = 0; fcnt[d] = (d >= MAXD - 1) ? 0 : expand(d);
          if (dbg && dbgFail) { finished = true; return { done: true, moves: null, optimal: false, nodes, bound, dbgFail }; }
        }
        if (fni[d] < fcnt[d]) {
          const j = (d << 2) + fni[d], fv = fg[d] + 1 + cH[j];
          if (fv > bound) {                   // sorted by h, so every later child is cut too
            if (fv < fmin[d]) fmin[d] = fv;
            fni[d] = fcnt[d];
            continue;
          }
          fni[d]++;
          const cz = cCz[j], uo = d << 3;
          fcz[d] = cz;
          eng.commit(cz, cA[j], cB[j], cV[j], cW[j], und, uo);
          const nd = d + 1;
          fg[nd] = fg[d] + 1; fh[nd] = cH[j]; fprev[nd] = und[uo + 1]; fent[nd] = 0;
          top = nd;
        } else popFrame();
      }
      // stack emptied for this bound -> deepen, or unsolvable
      if (childMin === Infinity) { finished = true; moves = null; return { done: true, moves: null, optimal: true, nodes, bound }; }
      bound = childMin; resetIteration();
      return { done: false, optimal: true, nodes, bound };
    }
    return { step, dbg: function () { return { checks: dbgChecks, fail: dbgFail }; } };
  }

  // ---- synchronous IDA*; same core as the session ----
  function solveIDA(src, deadline) {
    const s = createSession(src);
    let r = s.step(1);
    while (!r.done) {
      if (Date.now() >= deadline) return { moves: null, optimal: false, nodes: r.nodes };
      r = s.step(200000);
    }
    return { moves: r.moves, optimal: true, nodes: r.nodes };
  }

  // WD's own incremental IDA* first, then the stronger combined heuristic if it can't finish.
  function solve(src, budgetMs) {
    const start = Date.now(), dl = start + (budgetMs || 600) - 30;
    const r0 = WD.solveOptimal(src, Math.min(dl, start + 120), null);
    if (r0.optimal && r0.moves) return { moves: r0.moves, optimal: true, nodes: r0.nodes, via: "wd" };
    const r1 = solveIDA(src, dl);
    return r1.optimal && r1.moves ? { moves: r1.moves, optimal: true, nodes: r1.nodes, via: "pdb" } : { moves: null, optimal: false, nodes: r1.nodes };
  }

  // solveGuide is re-exported so the panel can bail out of an optimal proof that can take
  // billions of nodes, while weighted A* answers in ms.
  return { solve, createSession, solveGuide: WD.solveGuide, solveWAStar: WD.solveWAStar,
           rootH, verify: WD.verify, _pdbOf: pdbOf, _hMax: hMax,
           _wdOf: WD.wdOf, _wdOfRaw: WD.wdOfRaw };
}


  return { createWDSolver: createWDSolver, createPuzzleSolver: createPuzzleSolver };
  })();

  // Puzzle-box (24-puzzle) live guide. Board from interface 1931 / comp 18 (bridge
  // puzzleState -> 25 raw cell sprites); tables lazy-loaded from wd_table.bin / pdb_5554.bin
  // via the puzzleWdTable / puzzlePdbTable bridges. Re-solve only when the live board
  // diverges from the planned next state.
  const PUZZLE_OPT_MS = 2000;       // optimal-proof budget; past this, weighted A* wins
  let puzzleFailedSig = null;       // board that beat both searches; retried when it moves
  let puzzleSolver = null;
  let puzzleTableState = 0;         // 0 untried, 1 loading, 2 ready/failed
  let puzzlePlan = null;            // {moves, states:[sig...], idx, optimal}
  let puzzleSess = null;            // {stepper, sig, nodes} active resumable search
  let puzzleImp = null;             // {board, sig, ws, i} queued weighted-A* improvement passes
  let puzzlePumping = false;
  let puzzleBusy = false;
  let puzzleDrawSig = null;

  // Raw cell sprites -> board[25] in 0..23 (tile), 24 (blank). Gap sprite = 65535.
  function puzzleBoardFromSprites(sprites) {
    if (!Array.isArray(sprites) || sprites.length !== 25) return null;
    // Tile sprites are NOT guaranteed consecutive ids (some boxes use two clusters), so
    // tile number = the sprite's RANK among sorted distinct non-gap sprites.
    const uniq = [...new Set(sprites.filter(s => s !== 65535 && s >= 0))].sort((a, b) => a - b);
    if (uniq.length !== 24) return null;
    const rank = new Map(uniq.map((s, i) => [s, i]));
    const board = new Array(25), seen = {}; let blanks = 0;
    for (let i = 0; i < 25; i++) {
      const s = sprites[i];
      if (s === 65535) { board[i] = 24; blanks++; }
      else { const t = rank.get(s); if (t === undefined || seen[t]) return null; seen[t] = 1; board[i] = t; }
    }
    return blanks === 1 ? board : null;
  }
  function puzzleIsGoal(b) { for (let i = 0; i < 25; i++) if (b[i] !== i) return false; return true; }
  // SOLVABILITY (parity). Half of all 24-puzzle arrangements cannot reach the goal, and an
  // optimal search on one runs until it exhausts the space. The grid is 5 wide (odd), so the
  // blank's row is irrelevant and a board is solvable iff the inversion count among the 24
  // tiles (row-major, blank removed) is even. A failing board is unsolvable or a misread.
  function puzzleSolvable(b) {
    const t = b.filter(v => v !== 24);
    let inv = 0;
    for (let i = 0; i < t.length; i++) for (let j = i + 1; j < t.length; j++) if (t[i] > t[j]) inv++;
    return (inv & 1) === 0;
  }
  function puzzleStates(board, moves) {            // board sig after each prefix of moves
    const states = [board.join(',')], b = board.slice();
    for (const click of moves) { const blank = b.indexOf(24); b[blank] = b[click]; b[click] = 24; states.push(b.join(',')); }
    return states;
  }
  // Lazy solver build: decode the tables and construct the solver on a deferred tick, so
  // the "loading" banner paints first.
  function puzzleEnsureSolver() {
    if (puzzleTableState === 2) return !!puzzleSolver;
    if (puzzleTableState === 1) return false;
    puzzleTableState = 1;
    setTimeout(function () {
      try {
        const dec = name => { const b64 = (bridge() && bridge()[name]) ? bridge()[name]() : ''; if (!b64) return null;
          const bin = atob(b64), u8 = new Uint8Array(bin.length); for (let i = 0; i < bin.length; i++) u8[i] = bin.charCodeAt(i); return u8; };
        const wd = dec('puzzleWdTable'), pdb = dec('puzzlePdbTable');
        if (wd && pdb) puzzleSolver = PuzzleKit.createPuzzleSolver(wd, pdb);
      } catch (e) { puzzleSolver = null; }
      puzzleTableState = 2; puzzleDrawSig = null;     // force a repaint with the result
    }, 0);
    return false;
  }
  // Weighted-A* ladder over one board, keeping the SHORTEST result (high W answers almost
  // instantly, low W runs closer to optimal). msTotal caps the ladder, which blocks the panel.
  function puzzleWeighted(board, Ws, msTotal) {
    const end = Date.now() + msTotal;
    let best = null;
    for (let i = 0; i < Ws.length; i++) {
      if (Date.now() >= end) break;
      try {
        const rw = puzzleSolver.solveWAStar(board, end, Ws[i]);
        if (rw && rw.moves && rw.moves.length && (!best || rw.moves.length < best.moves.length))
          best = { moves: rw.moves, weight: Ws[i] };
      } catch (e) {}
    }
    return best;
  }
  // Install a solution as the live plan, but only as an UPGRADE: the board on screen must
  // lie on the candidate path, and the remaining click count must improve (or the candidate
  // is proven optimal against a merely near-optimal plan). True if adopted.
  function puzzleAdopt(board, moves, optimal, weight) {
    if (!moves || !moves.length) return false;
    const states = puzzleStates(board, moves);
    const live = puzzleLastBoard ? puzzleLastBoard.join(',') : states[0];
    const k = states.indexOf(live);
    if (k < 0 || k >= states.length - 1) return false;          // screen is off this path
    if (puzzlePlan) {
      const remainNew = moves.length - k, remainOld = puzzlePlan.moves.length - puzzlePlan.idx;
      if (!(remainNew < remainOld || (optimal && !puzzlePlan.optimal))) return false;
    }
    puzzlePlan = { moves: moves, states: states, idx: k, optimal: optimal, weight: weight };
    return true;
  }
  // Self-scheduling search pump: advances the optimal session in short wall-clock bursts and
  // publishes exactly one plan -- optimal if it lands within PUZZLE_OPT_MS, else weighted.
  function puzzlePump() {
    puzzlePumping = false;
    // Improvement queue: one weight per timer slice. Weighted A* is not resumable, so a low
    // weight would freeze the panel unless each rung runs from its own capped callback.
    if (puzzleImp && puzzleSolver) {
      // Every rung runs BEFORE anything is published, so a displayed plan is never swapped.
      const imp = puzzleImp, better = puzzleWeighted(imp.board, [imp.ws[imp.i++]], 1200);
      if (better && (!imp.best || better.moves.length < imp.best.moves.length)) imp.best = better;
      if (imp.i >= imp.ws.length) {
        puzzleImp = null;
        if (imp.best) puzzleAdopt(imp.board, imp.best.moves, false, imp.best.weight);
        // Nothing found even greedily: remember the board so the poll does not restart the
        // same doomed search until it moves.
        else if (!puzzlePlan) puzzleFailedSig = imp.sig;
        puzzleDrawSig = null; puzzlePaint();
        if (puzzleLastBoard) puzzleHighlight(puzzlePlan);
      } else if (!puzzlePumping) { puzzlePumping = true; setTimeout(puzzlePump, 0); }
      return;
    }
    if (!puzzleSess || !puzzleSolver) return;
    // NO SEED PLAN: seeding a weighted plan and upgrading it in place moves the click target
    // under the cursor, so the proof gets the full PUZZLE_OPT_MS and nothing is drawn until
    // there is a final answer -- shown once and never swapped.
    const t0 = Date.now();
    let r;
    do { r = puzzleSess.stepper.step(20000); puzzleSess.nodes = r.nodes; } while (!r.done && (Date.now() - t0) < 30);
    // Give up on the proof at PUZZLE_OPT_MS: the search is exponential in the IDA* bound and
    // hard boards need 1e8-1e9+ nodes, while weighted A* answers in ms a few moves longer.
    if (!r.done && (Date.now() - puzzleSess.startedAt) >= PUZZLE_OPT_MS) {
      // Solve from the board that is ON SCREEN NOW: the player may have moved a tile during
      // the proof, and a plan built from a stale board is discarded on the next poll.
      const board = (puzzleLastBoard && puzzleSolvable(puzzleLastBoard)) ? puzzleLastBoard : puzzleSess.board;
      const sig = board.join(',');
      puzzleSess = null;
      // Weighted A* DIRECTLY, not solveGuide: solveGuide would spend its first optSlice
      // re-running the optimal search that just failed.
      puzzleImp = { board: board, sig: sig, ws: [8, 4, 2], i: 0, best: null };
      puzzleDrawSig = null; puzzlePaint();
      if (!puzzlePumping) { puzzlePumping = true; setTimeout(puzzlePump, 0); }
      return;
    }
    if (r.done) {
      const board = puzzleSess.board;
      puzzleSess = null;
      if (r.moves && r.moves.length) puzzleAdopt(board, r.moves, true, 0);
      else if (r.moves) puzzlePlan = null;                       // already solved
      puzzleDrawSig = null; puzzlePaint();
      if (puzzleLastBoard) puzzleHighlight(puzzlePlan);
      return;
    }
    if (!puzzlePumping) { puzzlePumping = true; setTimeout(puzzlePump, 0); }   // keep going next frame
    // Repainting rebuilds the grid and re-crosses the C++ bridge, costing more than a 30ms
    // search slice; only the node counter moves, so refresh it a few times a second.
    if (Date.now() - (puzzleSess.paintedAt || 0) >= 250) {
      puzzleSess.paintedAt = Date.now(); puzzleDrawSig = null; puzzlePaint();
    }
  }
  function puzzlePaint() {
    const board = puzzleLastBoard; if (board) puzzleDraw(board, puzzlePlan);
  }
  let puzzleLastBoard = null, puzzleHl = false;
  // In-game highlight of the next tile: puzzleCellRects gives absolute rects of interface
  // 1931 / comp 18, and draws only when cr.abs resolves them to screen pixels.
  function puzzleClearHl() {
    if (!puzzleHl) return;
    try { if (bridge() && bridge().puzzleCells) bridge().puzzleCells(myPid(), ''); } catch (e) {}
    puzzleHl = false;
  }
  // Collapse the move list into CLICKS: a straight run along one row/column (constant step
  // +-1 / +-5, not crossing a row edge) is ONE in-game click on the run's FURTHEST tile.
  function puzzleClicks(moves, startIdx, blank) {
    // moves = the blank's successive positions; `blank` = its position BEFORE moves[startIdx].
    // A run's step is measured from the blank INTO its first move, so an L-turn splits in two.
    const out = [];
    let i = startIdx | 0;
    let b = (typeof blank === 'number') ? blank : (moves.length ? moves[i] : 0);
    while (i < moves.length) {
      const d = moves[i] - b;                                   // direction of this slide (blank -> first move)
      const horiz = (d === 1 || d === -1), vert = (d === 5 || d === -5);
      let end = i;
      if (horiz || vert)
        while (end + 1 < moves.length && (moves[end + 1] - moves[end]) === d
               && (!horiz || (((moves[end] / 5) | 0) === ((moves[end + 1] / 5) | 0)))) end++;
      out.push(moves[end]);
      b = moves[end];                // after the click the blank sits on the clicked tile
      i = end + 1;
    }
    return out;
  }
  // Mirror the next up-to-3 clicks onto the live board as numbered cells (step 0 = click now),
  // re-sent every tick so the boxes track the interface if it moves.
  function puzzleHighlight(plan) {
    if (!plan || plan.idx >= plan.moves.length || !bridge() || !bridge().puzzleCellRects || !bridge().puzzleCells) { puzzleClearHl(); return; }
    try {
      const cr = JSON.parse(bridge().puzzleCellRects(myPid()) || '{}');
      if (!cr.abs || !cr.cells) { puzzleClearHl(); return; }
      const hlBlank = (puzzleLastBoard ? puzzleLastBoard.indexOf(24) : -1);
      const nexts = puzzleClicks(plan.moves, plan.idx, hlBlank).slice(0, 3), seen = new Set(), segs = [];
      for (let s = 0; s < nexts.length; s++) {
        const pos = nexts[s]; if (seen.has(pos)) continue; seen.add(pos);
        const c = cr.cells[pos]; if (!c) continue;
        // Box = the actual cell (49px) on a 56px pitch, so a larger box would overlap neighbours.
        segs.push(c[0] + ',' + c[1] + ',' + c[2] + ',' + c[3] + ',' + s);
      }
      if (segs.length) { bridge().puzzleCells(myPid(), segs.join(';')); puzzleHl = true; }
      else puzzleClearHl();
    } catch (e) { puzzleClearHl(); }
  }
  function puzzleTick() {
    if (lockboxOwnsPanel || towersOwnsPanel) return;   // that solver owns the panel + overlay while its interface is open
    if (puzzleBusy) return; puzzleBusy = true;
    try {
      let sprites = null;
      try { sprites = JSON.parse(bridge().puzzleState(myPid()) || '[]'); } catch (e) {}
      const board = puzzleBoardFromSprites(sprites);
      cluePuzzleOpen = !!board;                        // tracked so the carousel can auto-focus the puzzle slot
      const el = $('cluePuzzle');
      // Show the solver only on the focused puzzle-box slot; if no held puzzle item was
      // identified, don't gate, so the solver never silently vanishes.
      const onSlot = cluePuzzleHeld.length === 0 || cluePuzzleHeld.some(z => z.i === activeClueId);
      if (!board || !onSlot) {
        if (!board) { puzzleLastBoard = null; puzzlePlan = null; puzzleSess = null; puzzleImp = null; puzzleDrawSig = null; puzzleClearHl(); }
        // No slider board open, but a puzzle CLUE is the selected held clue -> placeholder.
        const ac = (activeClueId >= 0 && !clueBrowse) ? cluePuzzleHeld.find(z => z.i === activeClueId) : null;
        if (el && ac) {
          if (el._phId !== ac.i) { el._phId = ac.i;
            el.innerHTML = '<b>' + String(ac.nm || 'Puzzle box').replace(/[<>&]/g, '') + '</b><br><span style="opacity:0.82">Open it in-game. Sliding puzzles, lockboxes, and towers all solve automatically here.</span>'; }
          el.style.display = ''; return;
        }
        if (el) el.style.display = 'none'; return;
      }
      if (el) el.style.display = '';
      puzzleLastBoard = board;
      if (!puzzleEnsureSolver()) { puzzleClearHl(); puzzleDraw(board, null); return; }   // still loading the tables
      if (puzzleIsGoal(board)) { puzzlePlan = null; puzzleSess = null; puzzleImp = null; puzzleClearHl(); puzzleDraw(board, null); return; }
      const sig = board.join(',');
      // live board already on the current plan? advance (no re-solve).
      if (puzzlePlan) {
        if (puzzlePlan.states[puzzlePlan.idx] !== sig) {
          const k = puzzlePlan.states.indexOf(sig);
          if (k >= 0 && k < puzzlePlan.states.length - 1) puzzlePlan.idx = k;
          else puzzlePlan = null;                     // reached goal, or diverged
        }
      }
      // A new solution is needed: (re)start the resumable session, unless this exact board
      // already exhausted its budget (it would relaunch every poll) or is parity-unsolvable.
      if (!puzzlePlan && !puzzleImp && sig !== puzzleFailedSig && puzzleSolvable(board)
          && (!puzzleSess || puzzleSess.sig !== sig)) {
        puzzleSess = { stepper: puzzleSolver.createSession(board), sig: sig, board: board, nodes: 0, startedAt: Date.now() };
        if (!puzzlePumping) { puzzlePumping = true; setTimeout(puzzlePump, 0); }
      }
      puzzleDraw(board, puzzlePlan);
      puzzleHighlight(puzzlePlan);                    // mirror the next move onto the live interface
    } catch (e) {} finally { puzzleBusy = false; }
  }
  // 5x5 grid mirroring the live board: next click highlighted, tiles already home tinted green.
  function puzzleDraw(board, plan) {
    const el = $('cluePuzzle'); if (!el) return;
    el._phId = -1;   // a real board render invalidates the puzzle-clue placeholder cache
    const remaining = plan ? (plan.moves.length - plan.idx) : 0;   // optimal single-tile moves left
    // nexts[] = next click tiles (furthest of each run); seq[] = the piece numbers on them.
    const clicks = plan ? puzzleClicks(plan.moves, plan.idx, board.indexOf(24)) : [];
    const nexts = clicks.slice(0, 3);
    const seq = nexts.map(pos => board[pos] + 1);
    const goal = puzzleIsGoal(board);
    const searching = !!puzzleSess;                  // optimal proof still running in the background
    const solving = !plan && searching;
    const nodesK = searching ? ((puzzleSess.nodes / 1000) | 0) : -1;
    const sig = board.join(',') + '|' + nexts.join('-') + '|' + remaining + '|' + goal + '|' + puzzleTableState
              + '|' + nodesK + '|' + (plan ? (plan.optimal ? 'o' : 'w') : '-');
    if (sig === puzzleDrawSig) return; puzzleDrawSig = sig;
    let head;
    if (goal) head = '<b>Solved.</b>';
    else if (puzzleTableState === 1) head = 'loading solver...';
    else if (!puzzleSolver) head = 'solver tables missing (reinstall to enable)';
    else if (solving) head = 'solving optimally... <span style="opacity:0.55">(' + nodesK + 'k nodes)</span>';
    else if (!plan && !puzzleSolvable(board)) head = 'this board cannot be solved <span style="opacity:0.55">(parity -- likely a misread; reopen the box)</span>';
    else if (!plan && puzzleFailedSig === board.join(',')) head = 'no solution found within budget <span style="opacity:0.55">(move a tile to retry)</span>';
    else if (!plan && puzzleImp) head = 'no optimal proof in time; finding the shortest fallback...';
    else if (!plan) head = 'solving...';
    else head = 'click the highlighted tile  ·  <b>' + clicks.length + '</b> click' + (clicks.length === 1 ? '' : 's') + ' <span style="opacity:0.55">(' + remaining + ' move' + (remaining === 1 ? '' : 's') + (plan.optimal ? ' optimal' : ', near-optimal') + ')</span>'
              + (seq.length ? '  ·  next: ' + seq.map((p, n) => '<b style="color:' + (n === 0 ? '#ffd479' : n === 1 ? 'rgba(255,212,121,0.8)' : 'rgba(255,212,121,0.6)') + '">' + p + '</b>').join(' <span style="opacity:0.4">→</span> ') : '')
              ;
    let grid = '<div style="margin-top:8px">';
    for (let r = 0; r < 5; r++) {
      grid += '<div style="display:flex;gap:3px;margin-bottom:3px">';
      for (let c = 0; c < 5; c++) {
        const i = r * 5 + c, v = board[i], blank = (v === 24), done = (v === i), step = nexts.indexOf(i);
        let bg, fg, bd;
        if (step >= 0) {                                                                            // next moves, gold fading by step
          const fa = [0.95, 0.5, 0.32, 0.2][step] || 0.2;
          bg = 'rgba(255,196,64,' + fa + ')'; fg = step === 0 ? '#15171d' : '#cdd6e4';
          bd = step === 0 ? '#ffd479' : 'rgba(255,212,121,' + (0.75 - step * 0.13) + ')';
        }
        else if (blank) { bg = 'rgba(255,255,255,0.02)'; fg = '#cdd6e4'; bd = 'rgba(255,255,255,0.08)'; }
        else if (done) { bg = 'rgba(120,200,140,0.16)'; fg = '#bfeccb'; bd = 'rgba(255,255,255,0.08)'; }
        else { bg = 'rgba(120,180,255,0.10)'; fg = '#cdd6e4'; bd = 'rgba(255,255,255,0.08)'; }
        const badge = step >= 0 ? '<span style="position:absolute;top:0;left:3px;font-size:9px;font-weight:800;opacity:0.9">' + (step + 1) + '</span>' : '';
        grid += '<div style="position:relative;width:38px;height:38px;display:flex;align-items:center;justify-content:center;'
              + 'border-radius:5px;font-size:13px;font-weight:600;background:' + bg + ';color:' + fg + ';border:1px solid ' + bd + '">'
              + badge + (blank ? '' : (v + 1)) + '</div>';
      }
      grid += '</div>';
    }
    grid += '</div>';
    const html = '<span style="opacity:0.65;text-transform:uppercase;font-size:10px;letter-spacing:0.6px;margin-right:7px">Puzzle</span>' + head + grid;
    if (el._pzHtml !== html) { el._pzHtml = html; el.innerHTML = html; }
  }
