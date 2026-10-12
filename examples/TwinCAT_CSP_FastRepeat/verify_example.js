'use strict';
// This interpreter executes the restricted ST subset used by MAIN, not a JS
// reimplementation of the state machine. It is NOT a TwinCAT compiler/runtime.
const fs = require('fs');
const path = require('path');
const assert = require('node:assert/strict');
const gvl = fs.readFileSync(path.join(__dirname, 'GVL_CSP_FAST.st'), 'utf8');
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
declarations(gvl, 'GVL_CSP_FAST.');
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
        if (e[1] === 'DINT_TO_LREAL') return Number(x);
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
const key = s => 'GVL_CSP_FAST.' + s;
function set(c, obj) { for (const [k, v] of Object.entries(obj)) c.v[key(k)] = v; }
function get(c, k) { return c.v[key(k)]; }
function cycle(c, follow = false) {
    if (follow) set(c, { iActualPosition: get(c, 'qTargetPosition'), iActualVelocity: 0 });
    c.calls = new Set(); execute(tree, c);
    assert.equal(c.calls.size, 3);
}
function healthy() {
    const c = make();
    set(c, { iWcState: false, iEthercatState: 8, iStatusWord: 0x227, iModeDisplay: 8 });
    cycle(c); return c;
}
function ready() {
    const c = healthy(); set(c, { bEnable: true }); cycle(c);
    assert(get(c, 'bReady')); return c;
}
function running() {
    const c = ready(); set(c, { bRun: true }); cycle(c);
    assert.equal(get(c, 'nPhase'), 1); return c;
}
let passed = 0;
function test(name, f) { f(); passed++; process.stdout.write('PASS ' + name + '\n'); }
test('separate panes, unique GVL, four outputs and six inputs', () => {
    assert(!/\b(?:PROGRAM|VAR_GLOBAL|END_VAR)\b/.test(clean(impl)));
    assert.equal((gvl.match(/AT %Q\*/g) || []).length, 4);
    assert.equal((gvl.match(/AT %I\*/g) || []).length, 6);
    assert(!/GVL_CSP_REPEAT|PRG_CSP_REPEAT|GVL_CSP_1RPS/.test(gvl + decl + impl));
    assert.equal(make().v.cTaskSec, 0.002);
});
test('power-up disconnected does not latch a motion-stop event', () => {
    const c = make(); for (let i = 0; i < 20; i++) cycle(c);
    assert.equal(get(c, 'bDiagValid'), false);
    assert.equal(get(c, 'bStopLatched'), false);
    assert.equal(get(c, 'qControlWord'), 0);
});
test('CiA402 6, 7, 15 handshake and CSP mode display guard', () => {
    const c = healthy(); set(c, { bEnable: true, iStatusWord: 0x40 }); cycle(c);
    assert.equal(get(c, 'qControlWord'), 6);
    set(c, { iStatusWord: 0x21 }); cycle(c); assert.equal(get(c, 'qControlWord'), 7);
    set(c, { iStatusWord: 0x23 }); cycle(c); assert.equal(get(c, 'qControlWord'), 15);
    set(c, { iStatusWord: 0x227, iModeDisplay: 9 }); cycle(c);
    assert.equal(get(c, 'bReady'), false);
    set(c, { iModeDisplay: 8 }); cycle(c); assert(get(c, 'bReady'));
});
test('one-cycle bad WC is captured and remains after recovery', () => {
    const c = running(); cycle(c, true);
    const target = get(c, 'qTargetPosition');
    set(c, { iWcState: true }); cycle(c);
    assert.equal(get(c, 'nStopReason'), 1);
    assert.equal(get(c, 'diagWcState'), true);
    assert.equal(get(c, 'diagEnableRequest'), true);
    assert.equal(get(c, 'diagRunRequest'), true);
    assert.equal(get(c, 'diagControlWord'), 15);
    assert.equal(get(c, 'diagTargetPosition'), target);
    assert.equal(get(c, 'diagPhase'), 1);
    assert.equal(get(c, 'qControlWord'), 0);
    set(c, { iWcState: false }); for (let i = 0; i < 5; i++) cycle(c);
    assert(get(c, 'bBusOk'));
    assert.equal(get(c, 'diagWcState'), true);
    assert.equal(get(c, 'bEnable'), false);
    assert.equal(get(c, 'bRun'), false);
    assert.equal(get(c, 'nStopCount'), 1);
});
test('non-OP stop captures state and never auto-resumes', () => {
    const c = running(); set(c, { iEthercatState: 4 }); cycle(c);
    assert.equal(get(c, 'nStopReason'), 2);
    assert.equal(get(c, 'diagEthercatState'), 4);
    set(c, { iEthercatState: 8, bEnable: true }); cycle(c);
    assert.equal(get(c, 'bEnable'), false);
    assert.equal(get(c, 'qControlWord'), 0);
});
test('simultaneous WC and state failures preserve both bits', () => {
    const c = running(); set(c, { iWcState: true, iEthercatState: 4 }); cycle(c);
    assert.equal(get(c, 'nStopReason'), 3);
});
test('drive fault requires an explicit single-cycle reset pulse', () => {
    const c = running(); set(c, { iStatusWord: 0x8 }); cycle(c);
    assert.equal(get(c, 'nStopReason'), 4);
    assert.equal(get(c, 'qControlWord'), 0);
    set(c, { bResetFault: true }); cycle(c);
    assert.equal(get(c, 'qControlWord'), 0x80);
    assert.equal(get(c, 'bResetFault'), false);
    cycle(c); assert.equal(get(c, 'qControlWord'), 0);
});
test('loss of CSP display while enabled stops with bit 8', () => {
    const c = running(); set(c, { iModeDisplay: 9 }); cycle(c);
    assert.equal(get(c, 'nStopReason'), 8);
    assert.equal(get(c, 'diagModeDisplay'), 9);
    assert.equal(get(c, 'qControlWord'), 0);
});
test('loss of operation-enabled while requested also stops with bit 8', () => {
    const c = running(); set(c, { iStatusWord: 0x21 }); cycle(c);
    assert.equal(get(c, 'nStopReason'), 8);
});
test('arrival timeout disables output without removing the protection', () => {
    const c = running();
    for (let i = 0; i < 3400; i++) cycle(c); // Hold actual position at zero.
    assert.equal(get(c, 'nStopReason'), 16);
    assert.equal(get(c, 'bArrivalTimeout'), true);
    assert.equal(get(c, 'qControlWord'), 0);
    assert.equal(get(c, 'bRun'), false);
});
test('positive position boundary is checked before interpolation', () => {
    const c = healthy();
    set(c, { iActualPosition: 1999999500, bEnable: true }); cycle(c);
    set(c, { bRun: true }); cycle(c);
    assert.equal(get(c, 'nStopReason'), 32);
    assert.equal(get(c, 'bPositionLimit'), true);
    assert.equal(get(c, 'qTargetPosition'), 1999999500);
});
test('out-of-window negative origin is rejected', () => {
    const c = healthy();
    set(c, { iActualPosition: -2000000001, bEnable: true }); cycle(c);
    set(c, { bRun: true }); cycle(c);
    assert.equal(get(c, 'nStopReason'), 32);
});
for (const params of [
    { fMoveTimeSec: 1.0 }, { fMoveTimeSec: 61 }, { fMoveTimeSec: NaN },
    { nCountsPerRev: 0 }, { nCountsPerRev: -1 }, { nCountsPerRev: 1000001 }
]) test('invalid configuration ' + JSON.stringify(params), () => {
    const c = ready(); set(c, { ...params, bRun: true }); cycle(c);
    assert.equal(get(c, 'nStopReason'), 64);
    assert.equal(get(c, 'bConfigError'), true);
    assert.equal(get(c, 'qControlWord'), 0);
});
test('external enable cancellation records bit 128, cancels Run, no false bit 8', () => {
    const c = running(); set(c, { bEnable: false }); cycle(c);
    assert.equal(get(c, 'nStopReason'), 128);
    assert.equal(get(c, 'bStopLatched'), false);
    assert.equal(get(c, 'bRun'), false);
    assert.equal(get(c, 'qControlWord'), 0);
    cycle(c);
    assert.equal(get(c, 'nStopCount'), 1);
});
test('normal run cancellation records bit 256 and completes current turn', () => {
    const c = running();
    for (let i = 0; i < 100; i++) cycle(c, true);
    set(c, { bRun: false }); cycle(c, true);
    assert.equal(get(c, 'nStopReason'), 256);
    assert.equal(get(c, 'bStopLatched'), false);
    assert.equal(get(c, 'qControlWord'), 15);
    for (let i = 0; i < 1200; i++) cycle(c, true);
    assert.equal(get(c, 'nCompletedTurns'), 1);
    assert.equal(get(c, 'nPhase'), 0);
    assert.equal(get(c, 'qTargetPosition'), 1000);
    assert.equal(get(c, 'bEnable'), true);
});
test('safe reset clears the diagnostic and allows manual enable', () => {
    const c = running(); set(c, { iWcState: true }); cycle(c);
    set(c, { bResetDiag: true }); cycle(c);
    assert(get(c, 'bDiagValid')); // Still bad WC: reset is rejected.
    set(c, { iWcState: false, bResetDiag: true }); cycle(c);
    assert.equal(get(c, 'bDiagValid'), false);
    assert.equal(get(c, 'bStopLatched'), false);
    assert.equal(get(c, 'nStopReason'), 0);
    assert.equal(get(c, 'bResetDiag'), false);
    set(c, { bEnable: true }); cycle(c); assert(get(c, 'bReady'));
    set(c, { bRun: true }); cycle(c); assert.equal(get(c, 'nPhase'), 1);
});
test('reset while moving or enabled cannot erase first-stop history', () => {
    const c = running(); set(c, { bRun: false }); cycle(c, true);
    set(c, { bResetDiag: true }); cycle(c, true);
    assert(get(c, 'bDiagValid'));
    assert.equal(get(c, 'nStopReason'), 256);
    assert.equal(get(c, 'bResetDiag'), false);
});
test('trajectory is monotonic, exactly one turn, peak 75 rpm, peak 250 rpm/s', () => {
    const c = running(); let last = 0, peak = 0, lastRpm = 0, peakAccel = 0, ticks = 0;
    while (get(c, 'nPhase') === 1) {
        cycle(c, true); ticks++;
        const pos = get(c, 'qTargetPosition'), rpm = get(c, 'fCommandRpm');
        assert(pos >= last && pos <= 1000);
        assert(pos - last <= 3);
        peak = Math.max(peak, rpm);
        peakAccel = Math.max(peakAccel, Math.abs(rpm - lastRpm) / c.dt);
        last = pos; lastRpm = rpm;
    }
    assert.equal(ticks, 600);
    assert.equal(last, 1000);
    assert(Math.abs(peak - 75) < 1e-8);
    assert(peakAccel <= 250 + 1e-7);
    const header = fs.readFileSync(path.join(__dirname,
        '../../program/FCM4E1553 Ethercat/User/EthercatMotorControl.h'), 'utf8');
    const limit = Number(header.match(/ECAT_MOTOR_DEFAULT_PROFILE_RPM\s+(\d+)L/)[1]);
    const accel = Number(header.match(/ECAT_MOTOR_ACCEL_RPM_PER_SEC\s+(\d+)L/)[1]);
    assert(peak < limit && peakAccel < accel);
});
test('three turns have full one-second pauses after settling, with no double counts', () => {
    const c = running(); let pauseTicks = 0, fullPauses = 0, lastCount = 0;
    for (let i = 0; i < 4000; i++) {
        const phase = get(c, 'nPhase'); cycle(c, true);
        if (phase === 3) pauseTicks++;
        if (phase === 3 && get(c, 'nPhase') === 1) {
            assert(pauseTicks * c.dt >= 1); fullPauses++; pauseTicks = 0;
        }
        const count = get(c, 'nCompletedTurns');
        assert(count === lastCount || count === lastCount + 1);
        lastCount = count;
    }
    assert.equal(get(c, 'nCompletedTurns'), 3);
    assert.equal(fullPauses, 3);
    assert.equal(get(c, 'bDiagValid'), false);
});
test('hold disturbance restarts settling and full pause, not another turn count', () => {
    const c = running();
    while (get(c, 'nPhase') !== 3) cycle(c, true);
    assert.equal(get(c, 'nCompletedTurns'), 1);
    for (let i = 0; i < 200; i++) cycle(c, true);
    set(c, { iActualPosition: 900 }); cycle(c);
    assert.equal(get(c, 'nPhase'), 2);
    let ticks = 0;
    while (get(c, 'nPhase') !== 1 && ticks < 1000) { cycle(c, true); ticks++; }
    assert(ticks >= 550);
    assert.equal(get(c, 'nCompletedTurns'), 1);
});
test('parameters are latched for a turn; next turn uses changed valid parameters', () => {
    const c = running(); cycle(c, true);
    set(c, { nCountsPerRev: 2000, fMoveTimeSec: 2.0 });
    while (get(c, 'nPhase') === 1) cycle(c, true);
    assert.equal(get(c, 'qTargetPosition'), 1000);
    while (get(c, 'nPhase') !== 1) cycle(c, true);
    assert.equal(c.v.fDelta, 2000);
    assert.equal(c.v.fDuration, 2.0);
});
test('negative initial position and DINT rounding remain within one-turn endpoints', () => {
    const c = healthy(); set(c, { iActualPosition: -12345, bEnable: true }); cycle(c);
    set(c, { bRun: true }); cycle(c);
    for (let i = 0; i < 600; i++) cycle(c, true);
    assert.equal(get(c, 'qTargetPosition'), -11345);
});
test('UDINT diagnostic and turn counters saturate, not wrap', () => {
    const c = running();
    set(c, { nCycleCount: 4294967295, nCompletedTurns: 4294967295 });
    for (let i = 0; i < 660; i++) cycle(c, true);
    assert.equal(get(c, 'nCycleCount'), 4294967295);
    assert.equal(get(c, 'nCompletedTurns'), 4294967295);
    set(c, { nStopCount: 4294967295, nBadWcCycles: 4294967295, iWcState: true }); cycle(c);
    assert.equal(get(c, 'nStopCount'), 4294967295);
    assert.equal(get(c, 'nBadWcCycles'), 4294967295);
});
process.stdout.write('\n' + passed + ' source-driven ST logic tests passed.\n');
process.stdout.write('Not TwinCAT compilation or hardware/motor verification.\n');
