'use strict';
// This interpreter executes the restricted ST subset used by MAIN, not a JS
// reimplementation of the state machine. It is NOT a TwinCAT compiler/runtime.
const fs = require('fs');
const path = require('path');
const assert = require('node:assert/strict');
const gvl = fs.readFileSync(path.join(__dirname, 'GVL_ID256_CSP.st'), 'utf8');
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
declarations(gvl, 'GVL_ID256_CSP.');
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
const key = s => 'GVL_ID256_CSP.' + s;
function set(c, obj) { for (const [k, v] of Object.entries(obj)) c.v[key(k)] = v; }
function get(c, k) { return c.v[key(k)]; }
function cycle(c, follow = false) {
    if (follow) set(c, { iActualPosition: get(c, 'qTargetPosition'), iActualVelocity: 0 });
    c.calls = new Set(); execute(tree, c);
    assert.equal(c.calls.size, 3);
}
function healthy() {
    const c = make();
    set(c, { iWcState: false, iEthercatState: 8, iStatusWord: 0x1237, iModeDisplay: 8 });
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
test('five outputs, eight inputs; no old driver GVL dependency', () => {
    assert.equal((gvl.match(/AT %Q\*/g) || []).length, 5);
    assert.equal((gvl.match(/AT %I\*/g) || []).length, 8);
    assert(!/GVL_CSP_FAST|GVL_CSP_REPEAT/.test(gvl + impl));
    assert.equal(make().v.cTaskSec, 0.002);
    assert.equal(make().v.cPauseTime, 0.5);
});
test('ID256 XML default PDO identities/types and resolution match example', () => {
    const xml = fs.readFileSync('C:/Users/pc/Desktop/斯达普/ID256EC.xml', 'utf8');
    const rx = xml.match(/<RxPdo\b[^>]*>[\s\S]*?<\/RxPdo>/)[0];
    const tx = xml.match(/<TxPdo\b[^>]*>[\s\S]*?<\/TxPdo>/)[0];
    const entries = x => [...x.matchAll(/<Entry>([\s\S]*?)<\/Entry>/g)].map(m => ({
        index: m[1].match(/<Index>#x([\dA-F]+)<\/Index>/)[1],
        type: m[1].match(/<DataType>([^<]+)<\/DataType>/)[1],
        bits: Number(m[1].match(/<BitLen>(\d+)<\/BitLen>/)[1])
    }));
    assert.deepEqual(entries(rx).map(x => x.index), ['6040', '607A', '60FF', '60B8', '6060']);
    assert.deepEqual(entries(tx).map(x => x.index), ['603F', '6041', '6061', '6064', '606C', '60FD']);
    assert.equal(entries(rx).reduce((s, x) => s + x.bits, 0), 104);
    assert.equal(entries(tx).reduce((s, x) => s + x.bits, 0), 136);
    assert.equal(schema[key('qMode')].type, 'SINT');
    assert.equal(schema[key('iErrorCode')].type, 'UINT');
    assert.equal(schema[key('iDigitalInputs')].type, 'UDINT');
    assert.equal(get(make(), 'nCountsPerRev'), 10000);
});
test('disconnected idle cannot silently start or latch a fictitious stop', () => {
    const c = make(); for (let i = 0; i < 5; i++) cycle(c);
    assert.equal(get(c, 'bDiagValid'), false);
    assert.equal(get(c, 'qControlWord'), 0);
});
test('enable handshake reaches 6, 7, 15 then ID256 run word 31', () => {
    const c = healthy(); set(c, { bEnable: true, iStatusWord: 0x250 }); cycle(c);
    assert.equal(get(c, 'qControlWord'), 6);
    set(c, { iStatusWord: 0x231 }); cycle(c); assert.equal(get(c, 'qControlWord'), 7);
    set(c, { iStatusWord: 0x233 }); cycle(c); assert.equal(get(c, 'qControlWord'), 15);
    set(c, { iStatusWord: 0x1237 }); cycle(c); assert.equal(get(c, 'qControlWord'), 31);
    assert(get(c, 'bReady'));
    assert.equal(get(c, 'qTouchProbeFunction'), 0);
});
test('initial Ready waits for mode 8 and the follow-master status bit', () => {
    const c = healthy(); set(c, { bEnable: true, iStatusWord: 0x237 }); cycle(c);
    assert.equal(get(c, 'bReady'), false);
    assert.equal(get(c, 'qControlWord'), 31);
    set(c, { iStatusWord: 0x1237, iModeDisplay: 1 }); cycle(c);
    assert.equal(get(c, 'bReady'), false);
    set(c, { iModeDisplay: 8 }); cycle(c); assert(get(c, 'bReady'));
});
test('first enable seeds nonzero feedback, no zero-position jump', () => {
    const c = healthy(); set(c, { bEnable: true, iActualPosition: 76543 }); cycle(c);
    assert.equal(get(c, 'qTargetPosition'), 76543);
    assert(get(c, 'bAtTarget'));
});
test('default performs only one 10000-pulse turn, then holds enabled', () => {
    const c = running(); let last = 0, ticks = 0, peak = 0, peakAccel = 0, lastRpm = 0;
    while (get(c, 'nPhase') === 1) {
        cycle(c, true); ticks++;
        const pos = get(c, 'qTargetPosition'), rpm = get(c, 'fCommandRpm');
        assert(pos >= last && pos <= 10000); assert(pos - last <= 8);
        peak = Math.max(peak, rpm);
        peakAccel = Math.max(peakAccel, Math.abs(rpm - lastRpm) / c.dt);
        last = pos; lastRpm = rpm;
    }
    assert(ticks >= 2000 && ticks <= 2001); assert.equal(last, 10000);
    assert(Math.abs(peak - 22.5) < 1e-8);
    assert(peakAccel <= 22.5 + 1e-8);
    for (let i = 0; i < 1500; i++) cycle(c, true);
    assert.equal(get(c, 'nCompletedTurns'), 1);
    assert.equal(get(c, 'bRun'), false); assert.equal(get(c, 'bEnable'), true);
    assert.equal(get(c, 'qControlWord'), 31); assert.equal(get(c, 'bDiagValid'), false);
});
test('repeat waits full 500 ms after feedback settling', () => {
    const c = running(); set(c, { bRepeat: true });
    let pauses = 0, pauseTicks = 0;
    for (let i = 0; i < 5200; i++) {
        const p = get(c, 'nPhase'); cycle(c, true);
        if (p === 3) pauseTicks++;
        if (p === 3 && get(c, 'nPhase') === 1) {
            assert(pauseTicks * c.dt >= 0.5); pauses++; pauseTicks = 0;
        }
    }
    assert.equal(get(c, 'nCompletedTurns'), 2);
    assert.equal(pauses, 2); assert.equal(get(c, 'bDiagValid'), false);
});
test('one-cycle bad WC latches prior command and cannot auto-resume', () => {
    const c = running(); set(c, { iWcState: true }); cycle(c);
    assert.equal(get(c, 'nStopReason'), 1);
    assert.equal(get(c, 'diagControlWord'), 31);
    assert.equal(get(c, 'diagWcState'), true);
    set(c, { iWcState: false, bEnable: true }); cycle(c);
    assert.equal(get(c, 'bEnable'), false);
    assert.equal(get(c, 'qControlWord'), 0);
    assert.equal(get(c, 'diagWcState'), true);
});
test('non-OP and WC failure preserve combined mask 3', () => {
    const c = running(); set(c, { iEthercatState: 4, iWcState: true }); cycle(c);
    assert.equal(get(c, 'nStopReason'), 3);
});
test('drive fault captures manufacturer code and physical input word', () => {
    const c = running(); set(c, { iStatusWord: 0x238, iErrorCode: 0x8611, iDigitalInputs: 2 }); cycle(c);
    assert.equal(get(c, 'nStopReason'), 4);
    assert.equal(get(c, 'diagErrorCode'), 0x8611);
    assert.equal(get(c, 'diagDigitalInputs'), 2);
    assert.equal(get(c, 'qControlWord'), 0);
    set(c, { bResetFault: true }); cycle(c);
    assert.equal(get(c, 'qControlWord'), 0x80);
    cycle(c); assert.equal(get(c, 'qControlWord'), 0);
});
test('loss of follow-master status while running trips bit 8', () => {
    const c = running(); set(c, { iStatusWord: 0x237 }); cycle(c);
    assert.equal(get(c, 'nStopReason'), 8);
});
test('abnormal-stop status bit is captured, including its raw input diagnostics', () => {
    const c = running(); set(c, { iStatusWord: 0x1337, iDigitalInputs: 2 }); cycle(c);
    assert.equal(get(c, 'nStopReason'), 512);
    assert.equal(get(c, 'diagDigitalInputs'), 2);
    assert.equal(get(c, 'qControlWord'), 0);
    assert(get(c, 'bStopLatched'));
});
test('failed physical arrival times out after the trajectory', () => {
    const c = running(); for (let i = 0; i < 5000; i++) cycle(c);
    assert.equal(get(c, 'nStopReason'), 16);
    assert(get(c, 'bArrivalTimeout'));
});
test('position boundary prevents an out-of-window relative turn', () => {
    const c = healthy();
    set(c, { bEnable: true, iActualPosition: 1999995000 }); cycle(c);
    set(c, { bRun: true }); cycle(c);
    assert.equal(get(c, 'nStopReason'), 32);
    assert.equal(get(c, 'qTargetPosition'), 1999995000);
});
for (const params of [
    { nCountsPerRev: 199 }, { nCountsPerRev: 102401 },
    { fMoveTimeSec: 1.2 }, { fMoveTimeSec: 61 }, { fMoveTimeSec: NaN }
]) test('invalid parameters cannot start ' + JSON.stringify(params), () => {
    const c = ready(); set(c, { ...params, bRun: true }); cycle(c);
    assert.equal(get(c, 'nStopReason'), 64);
    assert.equal(get(c, 'qControlWord'), 0);
});
test('normal Run cancellation completes the current turn with diagnostic 256', () => {
    const c = running(); for (let i = 0; i < 10; i++) cycle(c, true);
    set(c, { bRun: false }); cycle(c, true);
    assert.equal(get(c, 'nStopReason'), 256);
    for (let i = 0; i < 2100; i++) cycle(c, true);
    assert.equal(get(c, 'nCompletedTurns'), 1);
    assert.equal(get(c, 'qTargetPosition'), 10000);
    assert.equal(get(c, 'qControlWord'), 31);
});
test('external enable removal immediately cancels Run with mask 128', () => {
    const c = running(); set(c, { bEnable: false }); cycle(c);
    assert.equal(get(c, 'nStopReason'), 128);
    assert.equal(get(c, 'qControlWord'), 0);
    assert.equal(get(c, 'bRun'), false);
});
test('diagnostic reset is refused on invalid bus and accepted only safely', () => {
    const c = running(); set(c, { iWcState: true }); cycle(c);
    set(c, { bResetDiag: true }); cycle(c);
    assert(get(c, 'bDiagValid'));
    set(c, { iWcState: false, bResetDiag: true }); cycle(c);
    assert.equal(get(c, 'bDiagValid'), false);
    assert.equal(get(c, 'bStopLatched'), false);
    set(c, { bEnable: true }); cycle(c); assert(get(c, 'bReady'));
});
test('near-stationary feedback tolerance accepts at most 5 pulse/s and 5 pulses', () => {
    const c = ready();
    set(c, { iActualVelocity: 5, iActualPosition: 5 }); cycle(c);
    assert(get(c, 'bAtTarget'));
    set(c, { iActualVelocity: 6 }); cycle(c);
    assert.equal(get(c, 'bAtTarget'), false);
});
process.stdout.write('\n' + passed + ' ID256 source-driven ST tests passed.\n');
process.stdout.write('Not TwinCAT compilation, manufacturer certification, or hardware verification.\n');
