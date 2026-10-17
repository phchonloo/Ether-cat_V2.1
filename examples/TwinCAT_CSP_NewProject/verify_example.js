'use strict';
// Execute the NEW ST source with the repository's restricted-ST replay harness.
// This is not a TwinCAT compiler and does not operate any hardware.
const fs = require('fs');
const path = require('path');
const Module = require('module');
const assert = require('node:assert/strict');
let source = fs.readFileSync(path.join(__dirname, '../TwinCAT_CSP_SmoothRepeat/verify_example.js'), 'utf8')
    .replaceAll('GVL_CSP_SMOOTH', 'GVL_CSP_NEW')
    .replace('4 outputs/6 inputs', '4 outputs/9 inputs')
    .replace("assert.equal((gvl.match(/AT %I\\*/g)||[]).length,6);",
             "assert.equal((gvl.match(/AT %I\\*/g)||[]).length,9);");
assert(source.includes('length,9);'), 'Base harness shape changed; review this adapter');
const replay = new Module(path.join(__dirname, '_replay_in_memory.js'), module);
replay.filename = path.join(__dirname, '_replay_in_memory.js');
replay.paths = module.paths;
replay._compile(source, replay.filename);
const {set,get,cycle,ready,running} = replay.exports;
let passed = 0;
function test(name, fn) { fn(); passed++; console.log('PASS NEW ' + name); }
test('single-turn mode completes once and holds enabled without restart', () => {
    const c = ready(123);
    set(c,{bRepeat:false,bRun:true});
    for(let i=0;i<4000;i++) cycle(c,true);
    assert.equal(get(c,'nCompletedTurns'),1);
    assert.equal(get(c,'qTargetPosition'),1123);
    assert.equal(get(c,'nPhase'),0);
    assert.equal(get(c,'qControlWord'),15);
    assert.equal(get(c,'bRun'),false);
    assert.equal(get(c,'bStopLatched'),false);
});
test('pause parameter latches at turn start, edits affect only the next turn', () => {
    const c=ready();
    set(c,{bRun:true,tPauseTime:0.75});cycle(c);
    assert.equal(c.v.tLatchedPause,0.75);
    set(c,{tPauseTime:0});
    for(let i=0;i<1052;i++)cycle(c,true);
    assert.equal(get(c,'nPhase'),3);
    for(let i=0;i<300;i++)cycle(c,true);
    assert.equal(get(c,'nPhase'),3);
    for(let i=0;i<85;i++)cycle(c,true);
    assert.equal(get(c,'nPhase'),1);
    assert.equal(c.v.tLatchedPause,0);
});
test('invalid pause is rejected before motion', () => {
    for(const pause of [-0.001,60.001]) {
        const c=ready();set(c,{bRun:true,tPauseTime:pause});cycle(c);
        assert.equal(get(c,'nStopReason'),64);
        assert.equal(get(c,'qControlWord'),0);
    }
});
test('unlinked optional diagnostics never create or claim a master fault', () => {
    const c=running();
    set(c,{iMasterDevState:65535,iMasterFrameWcState:65535,iMasterFrameState:65535});
    cycle(c,true);
    assert.equal(get(c,'bStopLatched'),false);
    assert.equal(get(c,'nFrameMissingCycles'),0);
    set(c,{iWcState:true});cycle(c);
    assert.equal(get(c,'nStopReason'),1);
    assert.equal(get(c,'diagMasterDiagValid'),false);
    assert.equal(get(c,'diagFrameMissing'),false);
    assert.equal(get(c,'diagCommandWcError'),false);
});
test('first fault captures missing-frame master inputs and cannot be overwritten', () => {
    const c=running(304000);
    set(c,{bMasterDiagLinked:true,iMasterDevState:0x1000,
           iMasterFrameWcState:0x8000,iMasterFrameState:0,iWcState:true});
    cycle(c);
    assert.equal(get(c,'diagMasterDiagValid'),true);
    assert.equal(get(c,'diagMasterDevState'),0x1000);
    assert.equal(get(c,'diagMasterFrameWcState'),0x8000);
    assert.equal(get(c,'diagFrameMissing'),true);
    assert.equal(get(c,'diagCommandWcError'),false);
    assert.equal(get(c,'nFrameMissingCycles'),1);
    assert.equal(get(c,'diagControlWord'),15);
    set(c,{iMasterDevState:0,iMasterFrameWcState:0,iWcState:false});
    cycle(c);
    assert.equal(get(c,'diagMasterFrameWcState'),0x8000);
    assert.equal(get(c,'qControlWord'),0);
    assert.equal(get(c,'bEnable'),false);
});
test('wrong-command-WC and unsent-frame bits are distinct evidence', () => {
    const c=running();
    set(c,{bMasterDiagLinked:true,iMasterFrameWcState:2,
           iMasterFrameState:0x8000,iWcState:true});
    cycle(c);
    assert.equal(get(c,'diagFrameMissing'),false);
    assert.equal(get(c,'diagCommandWcError'),true);
    assert.equal(get(c,'diagFrameNotSent'),true);
    assert.equal(get(c,'nCommandWcErrorCycles'),1);
});
test('safe reset invalidates the old master snapshot and cannot start motion', () => {
    const c=running();
    set(c,{bMasterDiagLinked:true,iMasterFrameWcState:0x8000,iWcState:true});
    cycle(c);
    set(c,{iMasterFrameWcState:0,iWcState:false,iActualVelocity:0,bResetDiag:true});
    cycle(c);
    assert.equal(get(c,'bStopLatched'),false);
    assert.equal(get(c,'diagMasterDiagValid'),false);
    assert.equal(get(c,'nFrameMissingCycles'),0);
    assert.equal(get(c,'bEnable'),false);
    assert.equal(get(c,'bRun'),false);
    assert.equal(get(c,'qControlWord'),0);
});
test('one bad WKC scan records before/after controlword and never auto-restarts', () => {
    const c=running(304000);
    const target=get(c,'qTargetPosition');
    set(c,{iWcState:true});cycle(c);
    assert.equal(get(c,'diagControlWord'),15);
    assert.equal(get(c,'diagIssuedControlWord'),0);
    assert.equal(get(c,'qControlWord'),0);
    assert.equal(get(c,'qTargetPosition'),target);
    assert.equal(get(c,'nWcBadStreak'),1);
    assert.equal(get(c,'nWcBadStreakMax'),1);
    assert.equal(get(c,'nWcEpisodeCount'),1);
    set(c,{iWcState:false});
    for(let i=0;i<100;i++)cycle(c);
    assert.equal(get(c,'nLastWcEpisodeCycles'),1);
    assert.equal(get(c,'nWcBadStreak'),0);
    assert.equal(get(c,'bStopLatched'),true);
    assert.equal(get(c,'bEnable'),false);
    assert.equal(get(c,'bRun'),false);
    assert.equal(get(c,'qControlWord'),0);
    assert.equal(get(c,'diagIssuedControlWord'),0);
});
test('separate bad-WKC episodes are not counted as consecutive lost frames', () => {
    const c=running();
    for(const bad of [true,false,true,true,false]) {
        set(c,{iWcState:bad});cycle(c);
    }
    assert.equal(get(c,'nBadWcCycles'),3);
    assert.equal(get(c,'nWcEpisodeCount'),2);
    assert.equal(get(c,'nWcBadStreakMax'),2);
    assert.equal(get(c,'nLastWcEpisodeCycles'),2);
    assert.equal(get(c,'nWcBadStreak'),0);
    assert.equal(get(c,'nStopReason'),1);
    assert.equal(get(c,'diagControlWord'),15);
});
test('bad-WKC streak and episode counters saturate', () => {
    const c=running();
    set(c,{nWcEpisodeCount:4294967295,iWcState:true});cycle(c);
    assert.equal(get(c,'nWcEpisodeCount'),4294967295);
    set(c,{nWcBadStreak:4294967295});cycle(c);
    assert.equal(get(c,'nWcBadStreak'),4294967295);
    assert.equal(get(c,'nWcBadStreakMax'),4294967295);
    assert.equal(get(c,'qControlWord'),0);
});
test('safe diagnostic reset clears episode statistics but never enables motion', () => {
    const c=running();
    set(c,{iWcState:true});cycle(c);
    set(c,{bResetDiag:true});cycle(c);
    assert.equal(get(c,'bStopLatched'),true);
    assert.equal(get(c,'nWcBadStreak'),2);
    set(c,{iWcState:false,iActualVelocity:0,bResetDiag:true});cycle(c);
    for(const k of ['nWcBadStreak','nWcBadStreakMax','nWcEpisodeCount',
                    'nLastWcEpisodeCycles','diagIssuedControlWord'])
        assert.equal(get(c,k),0,k);
    assert.equal(get(c,'bEnable'),false);
    assert.equal(get(c,'bRun'),false);
    assert.equal(get(c,'qControlWord'),0);
});
console.log(passed+' additional new-example ST checks passed; not TwinCAT/hardware tested.');
