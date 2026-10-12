'use strict';
// This interpreter executes the restricted ST subset used by MAIN, not a JS
// reimplementation of the state machine. It is NOT a TwinCAT compiler/runtime.
const fs = require('fs');
const path = require('path');
const assert = require('node:assert/strict');
const gvl = fs.readFileSync(path.join(__dirname, 'GVL_CSP_SMOOTH.st'), 'utf8');
const decl = fs.readFileSync(path.join(__dirname, 'MAIN_Declaration.st'), 'utf8');
const impl = fs.readFileSync(path.join(__dirname, 'MAIN_Implementation.st'), 'utf8');
const clean = s => s.replace(/\(\*[\s\S]*?\*\)/g, '').replace(/\/\/[^\n]*/g, '');
const literal = s => {
    if (/^TRUE$/i.test(s)) return true;
    if (/^FALSE$/i.test(s)) return false;
    if (/^T#/i.test(s)) {
        const m = s.match(/^T#([\d.]+)(ms|s)$/i);
        assert(m, 'Unsupported TIME literal');
        return Number(m[1]) * (m[2].toLowerCase() === 'ms' ? 0.001 : 1);
    }
    s = s.replace(/^(?:UINT|UDINT|DINT|SINT|USINT)#/i, '');
    return s.startsWith('16#') ? parseInt(s.slice(3), 16) : Number(s);
};
function tokens(source) {
    let s = clean(source), out = [];
    const re = /^(?:T#[\d.]+(?:ms|s)|(?:UINT|UDINT|DINT|SINT|USINT)#(?:16#[\da-f]+|\d+)|16#[\da-f]+|\d+(?:\.\d+)?|[a-z_]\w*(?:\.[a-z_]\w*)*|:=|<>|<=|>=|[():;,=+*\/<>-])/i;
    while (s.trim()) {
        s = s.trimStart();
        const m = s.match(re);
        assert(m, 'Unexpected ST text: ' + s.slice(0, 80));
        out.push(m[0]); s = s.slice(m[0].length);
    }
    return out;
}
class Parser {
    constructor(s) { this.t = tokens(s); this.i = 0; }
    peek() { return this.t[this.i]; }
    take(t) {
        const x = this.t[this.i++];
        if (t) assert.equal(x.toUpperCase(), t.toUpperCase(), 'Expected ' + t);
        return x;
    }
    expr(min = 0) {
        let a, t = this.take();
        if (t === '(') { a = this.expr(); this.take(')'); }
        else if (t.toUpperCase() === 'NOT' || t === '-') {
            a = ['un', t.toUpperCase(), this.expr(6)];
        } else if (/^(?:\d|T#|(?:UINT|UDINT|DINT|SINT|USINT)#|TRUE$|FALSE$)/i.test(t)) {
            a = ['lit', literal(t)];
        } else if (this.peek() === '(') {
            this.take('('); a = ['fn', t, this.expr()]; this.take(')');
        } else a = ['var', t];
        const precedence = { OR: 1, AND: 2, '=': 3, '<>': 3, '<': 3,
            '>': 3, '<=': 3, '>=': 3, '+': 4, '-': 4, '*': 5, '/': 5 };
        for (;;) {
            const op = (this.peek() || '').toUpperCase(), p = precedence[op];
            if (p === undefined || p < min) break;
            this.take(); a = ['bin', op, a, this.expr(p + 1)];
        }
        return a;
    }
    block(stop = []) {
        const a = [];
        while (this.peek() && !stop.includes(this.peek().toUpperCase()) &&
               !(stop.includes('LABEL') && this.t[this.i + 1] === ':')) {
            if (this.peek() === ';') { this.take(); continue; }
            a.push(this.statement());
        }
        return a;
    }
    statement() {
        const t = this.take(), u = t.toUpperCase();
        if (u === 'IF') {
            const branches = [];
            let e = this.expr(); this.take('THEN');
            branches.push([e, this.block(['ELSIF', 'ELSE', 'END_IF'])]);
            while ((this.peek() || '').toUpperCase() === 'ELSIF') {
                this.take(); e = this.expr(); this.take('THEN');
                branches.push([e, this.block(['ELSIF', 'ELSE', 'END_IF'])]);
            }
            let otherwise = [];
            if ((this.peek() || '').toUpperCase() === 'ELSE') {
                this.take(); otherwise = this.block(['END_IF']);
            }
            this.take('END_IF'); this.take(';');
            return ['if', branches, otherwise];
        }
        if (u === 'CASE') {
            const e = this.expr(); this.take('OF');
            const branches = [];
            while ((this.peek() || '').toUpperCase() !== 'END_CASE') {
                const label = literal(this.take()); this.take(':');
                branches.push([label, this.block(['LABEL', 'END_CASE'])]);
            }
            this.take('END_CASE');
            return ['case', e, branches];
        }
        if (this.peek() === ':=') {
            this.take(); const e = this.expr(); this.take(';');
            return ['set', t, e];
        }
        this.take('(');
        const args = {};
        do {
            const name = this.take(); this.take(':='); args[name] = this.expr();
            if (this.peek() !== ',') break;
            this.take(',');
        } while (true);
        this.take(')'); this.take(';');
        return ['timer', t, args];
    }
}
const tree = new Parser(impl).block();
const schema = {};
function declarations(source, prefix) {
    for (const m of clean(source).matchAll(/^\s*(\w+)(?:\s+AT\s+%[IQ]\*)?\s*:\s*(\w+)(?:\s*:=\s*([^;]+))?;/gm)) {
        schema[prefix + m[1]] = { type: m[2], value: m[3] ? literal(m[3].trim()) :
            m[2] === 'BOOL' ? false : 0 };
    }
}
declarations(gvl, 'GVL_CSP_SMOOTH.');
declarations(decl, '');
function make() {
    const v = {}, timers = {};
    for (const [k, d] of Object.entries(schema)) {
        v[k] = d.value;
        if (d.type === 'TON') {
            timers[k] = { active: false, elapsed: 0 };
            v[k + '.Q'] = false; v[k + '.ET'] = 0;
        }
    }
    return { v, timers, dt: 0.002 };
}
function rounded(x) {
    const lo = Math.floor(x), r = x - lo;
    return r === 0.5 ? (lo % 2 === 0 ? lo : lo + 1) : Math.round(x);
}
function value(e, c) {
    if (e[0] === 'lit') return e[1];
    if (e[0] === 'var') {
        assert(Object.hasOwn(c.v, e[1]), 'Undeclared symbol ' + e[1]);
        return c.v[e[1]];
    }
    if (e[0] === 'un') return e[1] === 'NOT' ? !value(e[2], c) : -value(e[2], c);
    if (e[0] === 'fn') {
        const x = value(e[2], c);
        if (e[1] === 'ABS') return Math.abs(x);
        if (e[1] === 'DINT_TO_LREAL' || e[1] === 'UDINT_TO_LREAL') return Number(x);
        if (e[1] === 'LREAL_TO_UDINT') { const r=rounded(x); assert(Number.isFinite(r) && r>=0 && r<=4294967295); return r; }
        if (e[1] === 'LREAL_TO_DINT') {
            const r = rounded(x);
            assert(Number.isFinite(r) && r >= -2147483648 && r <= 2147483647, 'DINT overflow');
            return r;
        }
        throw Error('Unknown ST function ' + e[1]);
    }
    const a = value(e[2], c), b = value(e[3], c);
    switch (e[1]) {
        case 'AND': return typeof a === 'boolean' && typeof b === 'boolean' ? a && b : (a & b) >>> 0;
        case 'OR': return typeof a === 'boolean' && typeof b === 'boolean' ? a || b : (a | b) >>> 0;
        case '=': return a === b;
        case '<>': return a !== b;
        case '<': return a < b;
        case '>': return a > b;
        case '<=': return a <= b;
        case '>=': return a >= b;
        case '+': return a + b;
        case '-': return a - b;
        case '*': return a * b;
        case '/': assert.notEqual(b, 0); return a / b;
        default: throw Error('Unknown operator ' + e[1]);
    }
}
function execute(nodes, c) {
    for (const n of nodes) {
        if (n[0] === 'set') {
            assert(schema[n[1]], 'Undeclared destination ' + n[1]);
            const x = value(n[2], c);
            c.v[n[1]] = schema[n[1]].type === 'BOOL' ? Boolean(x) : x;
        } else if (n[0] === 'if') {
            const branch = n[1].find(([e]) => value(e, c));
            execute(branch ? branch[1] : n[2], c);
        } else if (n[0] === 'case') {
            const x = value(n[1], c), branch = n[2].find(([k]) => k === x);
            if (branch) execute(branch[1], c);
        } else {
            const t = c.timers[n[1]];
            assert(t, 'Unknown timer');
            assert(!c.calls.has(n[1]), 'TON called twice in one cycle');
            c.calls.add(n[1]);
            const active = Boolean(value(n[2].IN, c)), pt = value(n[2].PT, c);
            t.elapsed = active ? (t.active ? Math.min(t.elapsed + c.dt, pt) : 0) : 0;
            t.active = active;
            c.v[n[1] + '.Q'] = active && t.elapsed >= pt - 1e-10;
            c.v[n[1] + '.ET'] = t.elapsed;
        }
    }
}

const key = s => 'GVL_CSP_SMOOTH.' + s;
function set(c, o) { for(const [k,v] of Object.entries(o)) c.v[key(k)] = v; }
function get(c, k) { return c.v[key(k)]; }
function cycle(c, follow=false) {
    if(follow) set(c,{iActualPosition:get(c,'qTargetPosition'),iActualVelocity:0});
    c.calls = new Set(); execute(tree,c); assert.equal(c.calls.size,4);
}
function healthy(position=0) {
    const c=make();
    set(c,{iWcState:false,iEthercatState:8,iStatusWord:0x1627,
           iModeDisplay:8,iActualPosition:position});
    cycle(c); return c;
}
function ready(position=0) {
    const c=healthy(position); set(c,{bEnable:true}); cycle(c);
    assert(get(c,'bReady')); return c;
}
function running(position=0) {
    const c=ready(position);set(c,{bRun:true});cycle(c);
    assert.equal(get(c,'nPhase'),1);return c;
}
let passed=0;
function test(name,f) { f();passed++; console.log('PASS '+name); }
test('separate panes; one GVL; 4 outputs/6 inputs; 2 ms',()=>{
    assert(!/\b(?:PROGRAM|VAR_GLOBAL|END_VAR)\b/.test(clean(impl)));
    assert.equal((gvl.match(/AT %Q\*/g)||[]).length,4);
    assert.equal((gvl.match(/AT %I\*/g)||[]).length,6);
    assert.equal(make().v.cTaskSec,.002);
});
test('disconnected power-up cannot start or create a stop latch',()=>{
    const c=make();for(let i=0;i<100;i++) cycle(c);
    assert(!get(c,'bStopLatched')); assert.equal(get(c,'qControlWord'),0);
});
test('6/7/15; require CSP mode and drive-follows-command',()=>{
    const c=healthy();set(c,{bEnable:true,iStatusWord:0x40});cycle(c);
    assert.equal(get(c,'qControlWord'),6);
    set(c,{iStatusWord:0x21});cycle(c);assert.equal(get(c,'qControlWord'),7);
    set(c,{iStatusWord:0x23});cycle(c);assert.equal(get(c,'qControlWord'),15);
    set(c,{iStatusWord:0x627});cycle(c);assert(!get(c,'bReady'));
    set(c,{iStatusWord:0x1627,iModeDisplay:9});cycle(c);assert(!get(c,'bReady'));
    set(c,{iModeDisplay:8});cycle(c);assert(get(c,'bReady'));
});
test('Run during handshake waits; no false ready-loss fault',()=>{
    const c=healthy();set(c,{bEnable:true,bRun:true,iStatusWord:0x40});cycle(c);
    assert(!get(c,'bStopLatched'));assert.equal(get(c,'nPhase'),0);
    set(c,{iStatusWord:0x1627});cycle(c);assert.equal(get(c,'nPhase'),1);
});
test('3 s enable timeout is recorded',()=>{
    const c=healthy();set(c,{bEnable:true,iStatusWord:0x40});
    for(let i=0;i<1502;i++)cycle(c);
    assert(get(c,'bEnableTimeout'));assert.equal(get(c,'nStopReason'),512);
    assert.equal(get(c,'qControlWord'),0);
});
test('all supported durations: monotone quintic, exact circle, bounded speed',()=>{
    for(const duration of [1.5,2,2.003,4,60]) for(const start of [-1234567,17,1140748]) {
        const c=ready(start);set(c,{bRun:true,fMoveTimeSec:duration});cycle(c);
        const total=c.v.nTotalTicks;let last=start,maxRpm=0;
        assert.equal(total,rounded(duration/.002));
        for(let i=0;i<total;i++) {
            cycle(c,true);const target=get(c,'qTargetPosition');
            assert(target>=last && target<=start+1000);last=target;
            maxRpm=Math.max(maxRpm,get(c,'fCommandRpm'));
        }
        assert.equal(last,start+1000);assert.equal(get(c,'nPhase'),2);
        assert(maxRpm<=100);assert.equal(get(c,'fCommandRpm'),0);
    }
});
test('within-one-count/velocity-zero is not completion without status bit 10',()=>{
    const c=running();for(let i=0;i<1000;i++)cycle(c,true);
    set(c,{iActualPosition:1000,iActualVelocity:0,iStatusWord:0x1227});
    for(let i=0;i<200;i++)cycle(c);
    assert(!get(c,'bDriveAtTarget'));assert.equal(get(c,'nCompletedTurns'),0);
    assert.equal(get(c,'nPhase'),2);
    set(c,{iStatusWord:0x1627});for(let i=0;i<52;i++)cycle(c);
    assert.equal(get(c,'nCompletedTurns'),1);assert.equal(get(c,'nPhase'),3);
});
test('hold exactly at endpoint; 500 ms pause after settling',()=>{
    const c=running(123);for(let i=0;i<1052;i++)cycle(c,true);
    assert.equal(get(c,'nPhase'),3);assert.equal(get(c,'nCompletedTurns'),1);
    for(let i=0;i<249;i++){cycle(c,true);assert.equal(get(c,'qTargetPosition'),1123);}
    assert.equal(get(c,'nPhase'),3);
    for(let i=0;i<5;i++)cycle(c,true);assert.equal(get(c,'nPhase'),1);
    assert.equal(get(c,'nCompletedTurns'),1);
});
test('mid-turn parameter edits take effect on next turn, never mid-trajectory',()=>{
    const c=running();cycle(c,true);set(c,{nCountsPerRev:2000,fMoveTimeSec:4});
    for(let i=1;i<1000;i++)cycle(c,true);
    assert.equal(get(c,'qTargetPosition'),1000);
    for(let i=0;i<310;i++)cycle(c,true);
    assert.equal(c.v.fDelta,2000);assert.equal(c.v.fDuration,4);
});
test('normal Run=false completes this circle, no next circle',()=>{
    const c=running();for(let i=0;i<300;i++)cycle(c,true);
    set(c,{bRun:false});for(let i=0;i<1000;i++)cycle(c,true);
    assert.equal(get(c,'qTargetPosition'),1000);
    assert.equal(get(c,'nCompletedTurns'),1);assert.equal(get(c,'nPhase'),0);
    assert.equal(get(c,'qControlWord'),15);assert(!get(c,'bStopLatched'));
});
for(const [input,reason] of [
    [{iWcState:true},1],[{iEthercatState:4},2],[{iStatusWord:0x8},4],
    [{iModeDisplay:9},8],[{iStatusWord:0x627},8]]) {
    test('first fatal stop captured '+reason,()=>{
        const c=running();cycle(c,true);const target=get(c,'qTargetPosition');
        set(c,input);cycle(c);
        assert.equal(get(c,'nStopReason'),reason);assert(get(c,'bStopLatched'));
        assert.equal(get(c,'diagTargetPosition'),target);
        assert.equal(get(c,'diagControlWord'),15);
        assert.equal(get(c,'qControlWord'),0);assert(!get(c,'bRun'));
        set(c,{iWcState:false,iEthercatState:8,iStatusWord:0x1627,iModeDisplay:8,bEnable:true});
        cycle(c);assert(!get(c,'bEnable'));assert.equal(get(c,'nStopReason'),reason);
    });
}
test('normal-stop record cannot hide the first later fault',()=>{
    const c=running();set(c,{bRun:false});cycle(c);
    assert.equal(get(c,'nStopReason'),256);
    set(c,{iWcState:true});cycle(c);assert.equal(get(c,'nStopReason'),1);
    set(c,{iWcState:false,iEthercatState:4,bEnable:true});cycle(c);
    assert.equal(get(c,'nStopReason'),1);
});
test('arrival timeout is latched, never automatic restart',()=>{
    const c=running();for(let i=0;i<3510;i++)cycle(c);
    assert(get(c,'bArrivalTimeout'));assert.equal(get(c,'nStopReason'),16);
    assert.equal(get(c,'qControlWord'),0);
});
test('configuration rejects zero/NaN/too-fast/out-of-range parameters',()=>{
    for(const param of [{nCountsPerRev:0},{nCountsPerRev:40001},
        {fMoveTimeSec:1.499},{fMoveTimeSec:61},{fMoveTimeSec:NaN}]) {
        const c=ready();set(c,{...param,bRun:true});cycle(c);
        assert.equal(get(c,'nStopReason'),64);assert(get(c,'bStopLatched'));
    }
});
test('position limit checked before DINT conversion',()=>{
    const c=ready(1999999500);set(c,{bRun:true});cycle(c);
    assert(get(c,'bPositionLimit'));assert.equal(get(c,'nStopReason'),32);
});
test('explicit Enable=false disables immediately',()=>{
    const c=running();cycle(c,true);set(c,{bEnable:false});cycle(c);
    assert.equal(get(c,'qControlWord'),0);assert(!get(c,'bRun'));
    assert.equal(get(c,'nStopReason'),128);
});
test('safe reset only; no automatic drive-fault reset',()=>{
    const c=running();set(c,{iStatusWord:8});cycle(c);
    set(c,{bResetDiag:true});cycle(c);assert(get(c,'bStopLatched'));
    set(c,{bResetFault:true});cycle(c);assert.equal(get(c,'qControlWord'),128);
    cycle(c);assert.equal(get(c,'qControlWord'),0);
    set(c,{iStatusWord:0x1627,bResetDiag:true,iActualVelocity:1});cycle(c);
    assert(get(c,'bStopLatched'));
    set(c,{bResetDiag:true,iActualVelocity:0});cycle(c);
    assert(!get(c,'bStopLatched'));assert.equal(get(c,'nStopReason'),0);
});
test('30 successive circles have no round-counter cutoff or target reversal',()=>{
    const c=running(1140748);let last=get(c,'qTargetPosition');
    while(get(c,'nCompletedTurns')<30) {
        cycle(c,true);const now=get(c,'qTargetPosition');
        assert(now>=last);last=now;
        assert(!get(c,'bStopLatched'));assert.equal(get(c,'qControlWord'),15);
        assert(get(c,'nCycleCount')<50000);
    }
    assert.equal(last,1170748);
});
console.log(passed+' source-driven ST checks passed; not TwinCAT/hardware tested.');
module.exports={make,set,get,cycle,ready,running};
