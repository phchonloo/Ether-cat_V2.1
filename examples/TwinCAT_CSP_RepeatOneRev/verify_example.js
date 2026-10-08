"use strict";
// Source/native consistency and numerical control model only; NOT a TwinCAT compiler.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const read = name => fs.readFileSync(path.join(__dirname, name), "utf8");
const gvl = read("GVL_CSP_REPEAT.st");
const decl = read("MAIN_Declaration.st");
const impl = read("MAIN_Implementation.st");
const pou = read("MAIN.TcPOU");
const nativeGvl = read("GVL_CSP_REPEAT.TcGVL");
const cdata = xml => [...xml.matchAll(/<!\[CDATA\[([\s\S]*?)\]\]>/g)].map(m => m[1]);
assert.deepEqual(cdata(pou), [decl, impl]);
assert.deepEqual(cdata(nativeGvl), [gvl]);

function checkXmlTags(xml) {
    const stripped = xml.replace(/<!\[CDATA\[[\s\S]*?\]\]>/g, "");
    const stack = [];
    for (const match of stripped.matchAll(/<(\/?)([A-Za-z_][\w:.-]*)([^>]*)>/g)) {
        if (match[1]) assert.equal(stack.pop(), match[2]);
        else if (!match[3].trimEnd().endsWith("/")) stack.push(match[2]);
    }
    assert.equal(stack.length, 0);
}
checkXmlTags(pou);
checkXmlTags(nativeGvl);
assert.ok(pou.includes('<POU Name="MAIN"'));
assert.ok(nativeGvl.includes('<GVL Name="GVL_CSP_REPEAT"'));
assert.ok(!decl.includes("GVL_CSP_REPEAT."));
assert.ok(!/\bVAR\b/.test(impl.replace(/\/\/[^\n]*/g, "")));
const declared = new Set([...gvl.matchAll(/\b(\w+)\s*(?:AT\s+%[IQ]\*)?\s*:/g)].map(m => m[1]));
for (const match of impl.matchAll(/GVL_CSP_REPEAT\.(\w+)/g)) assert.ok(declared.has(match[1]), match[1]);
assert.equal((gvl.match(/AT %Q\*/g) || []).length, 4);
assert.equal((gvl.match(/AT %I\*/g) || []).length, 6);
const noComments = impl.replace(/\/\/[^\n]*/g, "");
assert.equal((noComments.match(/\bIF\b/g) || []).length,
    (noComments.match(/\bEND_IF\b/g) || []).length);
assert.equal((noComments.match(/\bCASE\b/g) || []).length,
    (noComments.match(/\bEND_CASE\b/g) || []).length);

function value(text, name) {
    const m = text.match(new RegExp("\\b" + name + "\\s*:\\s*\\w+\\s*:=\\s*([0-9.]+)\\s*;"));
    assert.ok(m, name);
    return Number(m[1]);
}
function time(name) {
    const m = decl.match(new RegExp("\\b" + name + "\\s*:\\s*TIME\\s*:=\\s*T#(\\d+)(ms|s)\\s*;"));
    assert.ok(m, name);
    return Number(m[1]) * (m[2] === "s" ? 1000 : 1);
}
const dt = value(decl, "cTaskSec");
const minTime = value(decl, "cMinMoveSec");
const maxTime = value(decl, "cMaxMoveSec");
const maxCounts = value(decl, "cMaxCountsPerRev");
const limit = value(decl, "cPositionLimit");
const tolerance = value(decl, "cToleranceCounts");
const counts = value(gvl, "nCountsPerRev");
const moveTime = value(gvl, "fMoveTimeSec");
assert.equal(dt, 0.002);
assert.equal(counts, 1000);
assert.equal(moveTime, 2);
assert.equal(time("cPauseTime"), 1000);
assert.equal(90 / moveTime, 45);
assert.equal(360 / (moveTime * moveTime), 90);
for (const name of ["fbSettled", "fbPause", "fbArrival"]) {
    assert.equal((impl.match(new RegExp("\\b" + name + "\\s*\\(", "g")) || []).length, 1);
}

class TonModel {
    constructor(pt) { this.pt = pt; this.start = null; this.q = false; }
    step(input, now) {
        if (!input) { this.start = null; this.q = false; }
        else {
            if (this.start === null) this.start = now;
            this.q = now - this.start >= this.pt;
        }
        return this.q;
    }
}

class RepeatModel {
    constructor(position = 12345) {
        this.enable = true; this.run = true; this.phase = 0;
        this.hold = position; this.target = Math.round(position);
        this.tick = 0; this.done = 0; this.counted = false;
        this.counts = counts; this.durationRequest = moveTime;
        this.arrivalError = false; this.limitError = false;
        this.starts = []; this.ends = []; this.settles = [];
        this.settled = new TonModel(time("cSettleTime"));
        this.pause = new TonModel(time("cPauseTime"));
        this.arrival = new TonModel(time("cArrivalTimeout"));
    }
    step(input = {}) {
        const now = this.tick++ * dt * 1000;
        const actual = input.actual ?? this.target;
        const velocity = input.velocity ?? (this.phase === 1 ? 1 : 0);
        const bus = input.bus ?? true;
        const fault = input.fault ?? false;
        const configOk = this.counts > 0 && this.counts <= maxCounts
            && this.durationRequest >= minTime && this.durationRequest <= maxTime;
        if (!configOk) this.run = false;
        if (!bus || fault || this.arrivalError || this.limitError) {
            this.enable = false; this.run = false;
        }
        const ready = this.enable && bus && !fault && (input.mode ?? 8) === 8;
        if (!ready) {
            this.hold = actual; this.phase = 0; this.elapsed = 0;
            this.counted = false; this.run = false;
        }
        const atTarget = ready && velocity === 0 && Math.abs(actual - this.hold) <= tolerance;
        const settled = this.settled.step(ready && this.phase === 2 && atTarget, now);
        const pause = this.pause.step(ready && this.run && this.phase === 3, now);
        const arrival = this.arrival.step(ready && this.phase === 2, now);
        let plan = false;
        if (ready) {
            switch (this.phase) {
                case 0: plan = this.run && atTarget; break;
                case 1:
                    this.elapsed = Math.min(this.elapsed + dt, this.duration);
                    const u = this.elapsed / this.duration;
                    this.hold = this.start + this.delta * (3 * u * u - 2 * u * u * u);
                    if (this.elapsed >= this.duration) {
                        this.hold = this.end; this.phase = 2;
                        this.ends.push({time: now, position: this.hold});
                    }
                    break;
                case 2:
                    if (settled) {
                        if (!this.counted) {
                            this.done = Math.min(0xFFFFFFFF, this.done + 1);
                            this.counted = true;
                        }
                        this.settles.push({time: now, position: this.hold});
                        this.phase = this.run ? 3 : 0;
                    } else if (arrival) {
                        this.arrivalError = true; this.enable = false;
                        this.run = false; this.phase = 0;
                    }
                    break;
                case 3:
                    if (!this.run) this.phase = 0;
                    else if (!atTarget) this.phase = 2;
                    else if (pause) plan = true;
                    break;
            }
        }
        if (plan) {
            this.start = this.hold; this.delta = this.counts;
            this.end = this.start + this.delta;
            if (this.end <= limit && this.end >= -limit) {
                this.duration = this.durationRequest;
                this.elapsed = 0; this.counted = false; this.phase = 1;
                this.starts.push({time: now, position: this.start, end: this.end});
            } else {
                this.limitError = true; this.enable = false;
                this.run = false; this.phase = 0;
            }
        }
        this.target = Math.round(this.hold);
        assert.ok(this.target >= -2147483648 && this.target <= 2147483647);
        return now;
    }
}
function runFor(model, seconds, input) {
    for (let n = 0; n < Math.ceil(seconds / dt); n++) model.step(input ? input(model) : {});
}

const normal = new RepeatModel();
runFor(normal, 10);
assert.ok(normal.starts.length >= 3 && normal.done >= 3);
normal.starts.forEach(event => assert.equal(event.end - event.position, counts));
for (let n = 1; n < normal.starts.length; n++) {
    assert.ok(normal.starts[n].time - normal.settles[n - 1].time >= 1000);
}

const delayed = new RepeatModel();
runFor(delayed, 5, model => {
    if (model.phase === 2 && model.ends.length === 1
        && model.tick * dt * 1000 - model.ends[0].time < 300) {
        return {actual: model.hold - 10, velocity: 1};
    }
    return {};
});
assert.ok(delayed.starts[1].time - delayed.ends[0].time >= 1400);

const stopped = new RepeatModel();
runFor(stopped, 0.5); stopped.run = false; runFor(stopped, 5);
assert.equal(stopped.starts.length, 1);
assert.equal(stopped.done, 1);
assert.equal(stopped.phase, 0);
assert.equal(stopped.target, 12345 + counts);

const disturbed = new RepeatModel();
while (disturbed.phase !== 3) disturbed.step();
runFor(disturbed, 0.4);
disturbed.step({actual: disturbed.hold - 10, velocity: 1});
assert.equal(disturbed.phase, 2);
assert.equal(disturbed.done, 1);
runFor(disturbed, 1.5);
assert.equal(disturbed.done, 1);
assert.equal(disturbed.starts.length, 2);
assert.ok(disturbed.starts[1].time - disturbed.settles[1].time >= 1000);

const disconnected = new RepeatModel();
runFor(disconnected, 0.5); disconnected.step({bus: false}); runFor(disconnected, 5);
assert.equal(disconnected.enable, false);
assert.equal(disconnected.run, false);
assert.equal(disconnected.starts.length, 1);

const timedOut = new RepeatModel();
runFor(timedOut, 8, model => ({
    velocity: model.starts.length === 0 ? 0 : 1
}));
assert.equal(timedOut.arrivalError, true);
assert.equal(timedOut.enable, false);
assert.equal(timedOut.starts.length, 1);
assert.equal(timedOut.done, 0);

const boundary = new RepeatModel(limit - 500);
boundary.step();
assert.equal(boundary.limitError, true);
assert.equal(boundary.starts.length, 0);

const invalid = new RepeatModel();
invalid.durationRequest = 1;
runFor(invalid, 2);
assert.equal(invalid.starts.length, 0);
assert.equal(invalid.run, false);

console.log("PASS: native/XML sections equal ST sources; 10 mapped variables and balanced blocks.");
console.log("PASS: one-revolution endpoints; pause >=1 second after estimated settling.");
console.log("PASS: delayed/disturbed feedback; no duplicate completion count; finish-current stop.");
console.log("PASS: communication loss, arrival timeout, position bound and invalid-duration models.");
console.log("NOT VERIFIED: TwinCAT import/build, real-time behavior or physical motor motion.");
