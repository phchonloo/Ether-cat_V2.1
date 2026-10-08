"use strict";

// Source consistency and a numerical trajectory model, NOT an ST compiler.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const read = (name) => fs.readFileSync(path.join(__dirname, name), "utf8");
const declaration = read("MAIN_Declaration.st");
const implementation = read("MAIN_Implementation.st");
const globals = read("GVL_CSP_1RPS.st");

function constant(name) {
    const match = declaration.match(new RegExp(
        "\\b" + name + "\\s*:\\s*(?:LREAL|UINT)\\s*:=\\s*([0-9.]+)\\s*;"));
    assert.ok(match, "Missing constant: " + name);
    return Number(match[1]);
}

const dt = constant("cTaskSec");
const counts = constant("cCountsPerRev");
const rpm = constant("cRunRpm");
const acceleration = constant("cAccelRpmPerSec");
const deceleration = constant("cDecelRpmPerSec");
const windowCycles = constant("cOneSecondCycles");
const stopLimit = constant("cPositionStopLimit");
const names = new Set([...globals.matchAll(/\b(\w+)\s*(?:AT\s+%[IQ]\*)?\s*:/g)]
    .map((match) => match[1]));

for (const match of implementation.matchAll(/GVL_CSP_1RPS\.(\w+)/g)) {
    assert.ok(names.has(match[1]), "Undeclared GVL variable: " + match[1]);
}
assert.equal((globals.match(/AT %Q\*/g) || []).length, 4);
assert.equal((globals.match(/AT %I\*/g) || []).length, 6);
assert.equal(dt, 0.002);
assert.equal(counts, 1000);
assert.equal(rpm, 60);
assert.equal(dt * windowCycles, 1);
assert.equal(rpm / 60 * counts * dt, 2);
assert.ok(acceleration > 0 && deceleration > 0);
assert.ok(stopLimit + rpm * rpm * counts / (120 * deceleration)
    + rpm / 60 * counts * dt < 2147483647);
assert.ok(implementation.includes("qMode := SINT#8;"));
assert.ok(implementation.includes("qTargetVelocity := DINT#0;"));
assert.ok(implementation.includes("bRun := FALSE;"));
assert.ok(implementation.includes("bEnable := FALSE;"));

function makeTrajectory(initialPosition, resolution = counts) {
    let position = initialPosition;
    let speed = 0;
    let target = Math.round(position);
    let windowStart = target;
    let cycles = 0;
    let valid = false;
    let delta = 0;
    return {
        step(requestRun) {
            const run = requestRun && position < stopLimit;
            speed = run ? Math.min(rpm, speed + acceleration * dt)
                : Math.max(0, speed - deceleration * dt);
            if (speed >= rpm) {
                if (cycles === 0) windowStart = target;
            } else {
                cycles = 0;
                valid = false;
            }
            const increment = speed / 60 * resolution * dt;
            position += increment;
            target = Math.round(position);
            if (speed >= rpm && ++cycles >= windowCycles) {
                delta = target - windowStart;
                cycles = 0;
                valid = true;
            }
            return {position, speed, increment, target, valid, delta};
        }
    };
}

const model = makeTrajectory(12345);
let sample;
for (let cycle = 0; cycle < windowCycles * 3; cycle++) sample = model.step(true);
assert.equal(sample.speed, 60);
assert.equal(sample.increment, 2);
assert.equal(sample.valid, true);
assert.equal(sample.delta, 1000);
const before = sample.target;
for (let cycle = 0; cycle < windowCycles; cycle++) sample = model.step(true);
assert.equal(sample.target - before, 1000);

for (let cycle = 0; cycle < windowCycles; cycle++) sample = model.step(false);
assert.equal(sample.speed, 0);
assert.equal(sample.valid, false);
const heldPosition = sample.position;
const heldTarget = sample.target;
assert.equal(model.step(false).target, heldTarget);
sample = model.step(true);
assert.ok(sample.position > heldPosition && sample.position - heldPosition < 0.01);

const fractional = makeTrajectory(-200, 1600);
for (let cycle = 0; cycle < windowCycles * 3; cycle++) sample = fractional.step(true);
assert.equal(sample.delta, 1600);
assert.equal(sample.increment, 3.2);

const nearLimit = makeTrajectory(stopLimit - 2000);
for (let cycle = 0; cycle < windowCycles * 10; cycle++) sample = nearLimit.step(true);
assert.equal(sample.speed, 0);
assert.ok(sample.position >= stopLimit && sample.position < 2147483647);

console.log("PASS: GVL references and 10 mapped variables; default 2 ms timing.");
console.log("PASS: +1000 counts/second; acceleration, deceleration, hold and restart model.");
console.log("PASS: fractional counts and pre-overflow forward stopping model.");
console.log("NOT VERIFIED: TwinCAT ST compilation, real-time timing and actual motor motion.");
