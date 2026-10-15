"use strict";
// Source-coupled event-order replay, NOT execution of the ARM binary.
// C functions below are mechanically adapted to JS; ESC and SYNC callbacks
// are mocked. This does not measure IRQ latency, SQI timing, or CPU load.
const fs = require("fs");
const path = require("path");
const vm = require("vm");
const assert = require("assert/strict");
const root = path.resolve(__dirname, "..");
const base = path.join(root, "program", "FCM4E1553 Ethercat", "Ethercat");
const read = (dir, name) => fs.readFileSync(path.join(base, dir, name), "utf8");
const application = read("src", "ecatappl.c");
const slave = read("src", "ecatslv.c");
const coe = read("src", "coeappl.c");
const headers = fs.readdirSync(path.join(base, "Inc"))
    .filter(name => name.endsWith(".h")).map(name => read("Inc", name)).join("\n");

function extract(text, name) {
    text = text.replace(/\/\*[\s\S]*?\*\//g, "").replace(/\/\/[^\r\n]*/g, "");
    const match = new RegExp("\\b(?:static\\s+)?void\\s+" + name + "\\s*\\(").exec(text);
    assert(match, "Missing C function: " + name);
    const open = text.indexOf("{", match.index);
    let depth = 0;
    for (let i = open; i < text.length; i++) {
        if (text[i] === "{") depth++;
        if (text[i] === "}" && --depth === 0)
            return text.slice(match.index, i + 1);
    }
    throw new Error("Unbalanced function: " + name);
}

function adapt(c) {
    assert(!c.includes("#"), "Preprocessor branches need a native C test");
    return c.replace(/(?:static\s+)?void\s+(\w+)\s*\(\s*void\s*\)/g, "function $1()")
        .replace(/void\s+(\w+)\s*\(\s*UINT16\s+(\w+)\s*\)/g, "function $1($2)")
        .replace(/\b(?:volatile\s+)?(?:BOOL|UINT16|UINT32)\s+(\w+)/g, "let $1")
        .replace(/\b(0x[0-9a-f]+|\d+)U\b/gi, "$1")
        // Read macros assign their first argument in C.
        .replace(/HW_EscRead(Word(?:Isr)?|DWord)\(\s*(\w+)\s*,([\s\S]*?)\);/g,
            "$2 = HW_EscRead$1($3);");
}

function constant(name) {
    const line = new RegExp("#define\\s+" + name + "\\s+([^\\r\\n]+)").exec(headers);
    assert(line, "Missing constant: " + name);
    const value = /\b(0x[0-9a-f]+|\d+)\b/i.exec(line[1].replace(/UINT\d+/g, ""));
    assert(value, "Nonliteral constant: " + name);
    return Number(value[1]);
}
const names = ["SYNC0_EVENT", "SYNC1_EVENT", "PROCESS_OUTPUT_EVENT",
    "SYNCTYPE_SM_SYNCHRON", "SYNCTYPE_SM2_SYNCHRON", "SYNCTYPE_DCSYNC0",
    "ESC_DC_SYNC_STATUS", "ESC_PD_WD_STATE", "ESC_PD_WD_TRIGGER_MASK",
    "STATE_SAFEOP", "ALSTATUSCODE_FATALSYNCERROR", "ALSTATUSCODE_SYNCERROR",
    "ALSTATUSCODE_SMWATCHDOG"];
const constants = Object.fromEntries(names.map(name => [name, constant(name)]));
const O = constants.PROCESS_OUTPUT_EVENT;
const S0 = constants.SYNC0_EVENT;
const S1 = constants.SYNC1_EVENT;
const functions = [
    ...["PDI_ProcessOutputEvent", "PDI_ProcessSmApplication",
        "ECAT_RecordSyncError", "PDI_Isr"].map(name => extract(application, name)),
    extract(coe, "COE_UpdateSyncErrorStatus"), extract(slave, "CheckIfEcatError")
].map(adapt).join("\n");
const diagType = /typedef struct\s*\{([^}]+)\}\s*TECAT_SYNC_DIAGNOSTICS/.exec(headers);
assert(diagType, "Missing diagnostic type");
const diagFields = [...diagType[1].matchAll(/\b(?:UINT16|UINT32|BOOL)\s+(\w+)\s*;/g)]
    .map(match => match[1]);

function fixture(events = [0, 0]) {
    let nextEvent = 0;
    const calls = { events: 0, map: 0, input: 0, application: 0,
        sync0: 0, sync1: 0, syncAck: 0, wordAddresses: [], al: [], order: [] };
    const par = () => ({ u16SyncType: constants.SYNCTYPE_DCSYNC0,
        u16SmEventMissedCounter: 0, u16CycleExceededCounter: 0, u8SyncError: 0 });
    const ctx = {
        ...constants, TRUE: 1, FALSE: 0, UINT32_MAX: 0xFFFFFFFF, u16dummy: 0,
        bEscIntEnabled: 1, bDcRunning: 1, bDcSyncActive: 1,
        bEcatOutputUpdateRunning: 1, bEcatInputUpdateRunning: 1,
        bEcatFirstOutputsReceived: 0, bSmSyncSequenceValid: 1,
        nEscAddrOutputData: 0x1100, nPdOutputSize: 12, nPdInputSize: 11,
        u16SmSync0Counter: 4, u16SmSync0Value: 2,
        Sync0WdCounter: 0, Sync0WdValue: 10, EcatWdValue: 0,
        sErrorSettings: { u16SyncErrorCounterLimit: 10 },
        sSyncManOutPar: par(), sSyncManInPar: par(),
        gEcatSyncDiag: Object.fromEntries(diagFields.map(name => [name, 0])),
        // This replay supplies normalized event words, not SQI wire bytes.
        SWAPWORD: value => value,
        HW_GetALEventRegister_Isr() {
            assert(nextEvent < events.length, "Unbounded/unexpected event read");
            calls.events++;
            return events[nextEvent++];
        },
        HW_EscReadWordIsr(address) { calls.wordAddresses.push(address); return 0; },
        HW_EscReadDWord() { calls.syncAck++; return 0; },
        HW_EscReadWord() { return 0; },
        PDO_OutputMapping() { calls.map++; calls.order.push("map"); },
        PDO_InputMapping() { calls.input++; calls.order.push("input"); },
        ECAT_Application() { calls.application++; calls.order.push("app"); },
        Sync0_Isr() { calls.sync0++; calls.order.push("sync0"); },
        Sync1_Isr() { calls.sync1++; calls.order.push("sync1"); },
        AL_ControlInd(state, code) {
            calls.al.push({ state, code, latchedCode: ctx.gEcatSyncDiag.firstAlStatusCode });
            ctx.bEcatOutputUpdateRunning = 0;
            ctx.sSyncManOutPar.u16CycleExceededCounter = 0;
            ctx.sSyncManOutPar.u16SmEventMissedCounter = 0;
        }
    };
    vm.createContext(ctx);
    vm.runInContext(functions, ctx, { timeout: 1000 });
    return { ctx, calls };
}
let passed = 0;
function test(name, fn) {
    fn();
    passed++;
    console.log("PASS " + name);
}
test("normal first PDO maps and updates bookkeeping", () => {
    const { ctx: c, calls: a } = fixture([O, 0]);
    c.sSyncManOutPar.u16SmEventMissedCounter = 3;
    c.PDI_Isr();
    assert.equal(a.map, 1);
    assert.equal(a.events, 2);
    assert.equal(c.u16SmSync0Counter, 0); // SYNC callback is mocked.
    assert.equal(c.sSyncManOutPar.u16SmEventMissedCounter, 2);
    assert.equal(c.sSyncManInPar.u16SmEventMissedCounter, 2);
    assert.equal(c.bEcatFirstOutputsReceived, 1);
    assert.equal(c.gEcatSyncDiag.outputRetriggerCount, 0);
});
test("SYNC0 entry then first late PDO: no false overrun or discard", () => {
    const { ctx: c, calls: a } = fixture([S0, O, 0]);
    c.PDI_Isr();
    assert.equal(a.map, 1);
    assert.equal(a.events, 3);
    assert.equal(a.sync0, 1);
    assert.deepEqual(a.order, ["map", "sync0"]);
    assert.equal(a.wordAddresses.length, 0);
    assert.equal(c.gEcatSyncDiag.lateOutputEventCount, 1);
    assert.equal(c.sSyncManOutPar.u16CycleExceededCounter, 0);
    assert.equal(c.sSyncManOutPar.u8SyncError, 0);
});
test("mailbox entry then first late PDO is serviced", () => {
    const { ctx: c, calls: a } = fixture([0x100, O, 0]);
    c.PDI_Isr();
    assert.equal(a.map, 1);
    assert.equal(c.gEcatSyncDiag.lateOutputEventCount, 1);
    assert.equal(a.syncAck, 0);
});
test("second PDO is fully mapped while retaining the overrun warning", () => {
    const { ctx: c, calls: a } = fixture([O, O]);
    c.PDI_Isr();
    assert.equal(a.map, 2);
    assert.equal(a.events, 2);
    assert.deepEqual(a.wordAddresses, []);
    assert.equal(c.gEcatSyncDiag.outputRetriggerReadCount, 1);
    assert.equal(c.gEcatSyncDiag.outputRetriggerCount, 1);
    assert.equal(c.sSyncManOutPar.u16CycleExceededCounter, 1);
    assert.equal(c.sSyncManInPar.u16CycleExceededCounter, 1);
    assert.equal(c.sSyncManOutPar.u8SyncError, 1);
});
test("late first PDO followed by a second still reports overrun", () => {
    const { ctx: c, calls: a } = fixture([S0, O, O]);
    c.PDI_Isr();
    assert.equal(a.map, 2);
    assert.equal(a.events, 3);
    assert.equal(a.wordAddresses.length, 0);
    assert.equal(c.gEcatSyncDiag.outputRetriggerReadCount, 1);
    assert.equal(c.gEcatSyncDiag.lateOutputEventCount, 1);
    assert.equal(c.gEcatSyncDiag.outputRetriggerCount, 1);
    assert.equal(c.sSyncManOutPar.u16CycleExceededCounter, 1);
});
test("cycle counter saturates instead of wrapping to zero", () => {
    const { ctx: c } = fixture([O, O]);
    c.sSyncManOutPar.u16CycleExceededCounter = 0xFFFF;
    c.gEcatSyncDiag.outputRetriggerCount = 0xFFFFFFFF;
    c.gEcatSyncDiag.outputRetriggerReadCount = 0xFFFFFFFF;
    c.PDI_Isr();
    assert.equal(c.sSyncManOutPar.u16CycleExceededCounter, 0xFFFF);
    assert.equal(c.sSyncManInPar.u16CycleExceededCounter, 0xFFFF);
    assert.equal(c.sSyncManOutPar.u8SyncError, 1);
    assert.equal(c.gEcatSyncDiag.outputRetriggerCount, 0xFFFFFFFF);
    assert.equal(c.gEcatSyncDiag.outputRetriggerReadCount, 0xFFFFFFFF);
});
test("burst does not erase two missed-event counts or run DC application twice", () => {
    const { ctx: c, calls: a } = fixture([O, O | S0]);
    c.sSyncManOutPar.u16SmEventMissedCounter = 5;
    c.PDI_Isr();
    assert.equal(c.sSyncManOutPar.u16SmEventMissedCounter, 4);
    assert.equal(a.sync0, 1);
    assert.deepEqual(a.order, ["map", "map", "sync0"]);
});
test("SM-sync application receives each fully mapped PDO in a burst", () => {
    const { ctx: c, calls: a } = fixture([O, O]);
    c.sSyncManOutPar.u16SyncType = c.SYNCTYPE_SM_SYNCHRON;
    c.sSyncManInPar.u16SyncType = c.SYNCTYPE_SM2_SYNCHRON;
    c.PDI_Isr();
    assert.deepEqual(a.order, ["map", "app", "input", "map", "app", "input"]);
});
test("SAFEOP burst acknowledges both events without publishing commands", () => {
    const { ctx: c, calls: a } = fixture([O, O]);
    c.bEcatOutputUpdateRunning = 0;
    c.PDI_Isr();
    assert.equal(a.map, 0);
    assert.equal(a.wordAddresses.length, 4);
    assert.equal(c.gEcatSyncDiag.outputRetriggerReadCount, 0);
});
test("missed-event counter does not underflow at zero", () => {
    const { ctx: c } = fixture([O, 0]);
    c.PDI_Isr();
    assert.equal(c.sSyncManOutPar.u16SmEventMissedCounter, 0);
});
test("SAFEOP late PDO is acknowledged, not applied", () => {
    const { ctx: c, calls: a } = fixture([0, O, 0]);
    c.bEcatOutputUpdateRunning = 0;
    c.PDI_Isr();
    assert.equal(a.map, 0);
    assert.equal(a.wordAddresses.length, 2);
    assert.equal(c.bEcatFirstOutputsReceived, 1);
    assert.equal(c.sSyncManOutPar.u16CycleExceededCounter, 0);
});
test("entry SYNC0 survives an empty later event snapshot", () => {
    const { ctx: c, calls: a } = fixture([S0, 0]);
    c.PDI_Isr();
    assert.equal(a.sync0, 1);
    assert.equal(a.syncAck, 1);
});
test("repeated SYNC bits are serviced once, SYNC1 remains first", () => {
    const { ctx: c, calls: a } = fixture([S0 | S1 | O, S0 | S1]);
    c.PDI_Isr();
    assert.equal(a.sync0, 1);
    assert.equal(a.sync1, 1);
    assert.equal(a.syncAck, 1);
    assert.deepEqual(a.order, ["sync1", "map", "sync0"]);
});
test("SYNC1 first seen at recheck is serviced", () => {
    const { ctx: c, calls: a } = fixture([0, S1]);
    c.PDI_Isr();
    assert.equal(a.sync1, 1);
    assert.equal(a.syncAck, 1);
});
test("SM late PDO invokes application/input mapping once", () => {
    const { ctx: c, calls: a } = fixture([0, O, 0]);
    c.sSyncManOutPar.u16SyncType = c.SYNCTYPE_SM_SYNCHRON;
    c.sSyncManInPar.u16SyncType = c.SYNCTYPE_SM2_SYNCHRON;
    c.PDI_Isr();
    assert.deepEqual(a.order, ["map", "app", "input"]);
});
test("SM mode does not apply stale outputs on SYNC/mailbox-only IRQ", () => {
    const { ctx: c, calls: a } = fixture([S0, 0]);
    c.sSyncManOutPar.u16SyncType = c.SYNCTYPE_SM_SYNCHRON;
    c.sSyncManInPar.u16SyncType = c.SYNCTYPE_SM_SYNCHRON;
    c.PDI_Isr();
    assert.equal(a.application, 0);
    assert.equal(a.input, 0);
});
test("input-only SM operation is preserved", () => {
    const { ctx: c, calls: a } = fixture([0, 0]);
    c.nPdOutputSize = 0;
    c.sSyncManOutPar.u16SyncType = c.SYNCTYPE_SM_SYNCHRON;
    c.sSyncManInPar.u16SyncType = c.SYNCTYPE_SM_SYNCHRON;
    c.PDI_Isr();
    assert.equal(a.application, 1);
    assert.equal(a.input, 1);
});
test("disabled ESC PDO interrupt still services SYNC bits", () => {
    const { ctx: c, calls: a } = fixture([O | S0 | S1]);
    c.bEscIntEnabled = 0;
    c.PDI_Isr();
    assert.equal(a.events, 1);
    assert.equal(a.map, 0);
    assert.equal(a.sync0, 1);
    assert.equal(a.sync1, 1);
});
test("healthy OP has no AL error or first-error latch", () => {
    const { ctx: c, calls: a } = fixture();
    c.CheckIfEcatError();
    assert.equal(a.al.length, 0);
    assert.equal(c.gEcatSyncDiag.firstAlStatusCode, 0);
});
test("missing DC still requests SAFEOP with 0x002C", () => {
    const { ctx: c, calls: a } = fixture();
    c.bDcRunning = 0;
    c.Sync0WdCounter = 11;
    c.CheckIfEcatError();
    assert.deepEqual(a.al, [{ state: c.STATE_SAFEOP,
        code: 0x2C, latchedCode: 0x2C }]);
    assert.equal(c.gEcatSyncDiag.firstDcRunning, 0);
    assert.equal(c.gEcatSyncDiag.firstSync0WdCounter, 11);
});
test("invalid sequence still requests SAFEOP with 0x001A; first record survives resets", () => {
    const { ctx: c, calls: a } = fixture();
    c.bSmSyncSequenceValid = 0;
    c.sSyncManOutPar.u16CycleExceededCounter = 152;
    c.sSyncManOutPar.u16SmEventMissedCounter = 12;
    c.gEcatSyncDiag.lastEntryEvent = S0;
    c.gEcatSyncDiag.lastPendingEvent = O;
    c.CheckIfEcatError();
    assert.deepEqual(a.al, [{ state: c.STATE_SAFEOP,
        code: 0x1A, latchedCode: 0x1A }]);
    assert.equal(c.gEcatSyncDiag.firstSequenceValid, 0);
    assert.equal(c.gEcatSyncDiag.firstCycleExceededCounter, 152);
    assert.equal(c.gEcatSyncDiag.firstSmEventMissedCounter, 12);
    assert.equal(c.gEcatSyncDiag.firstEntryEvent, S0);
    assert.equal(c.gEcatSyncDiag.firstPendingEvent, O);
    const first = JSON.stringify(c.gEcatSyncDiag);
    c.ECAT_RecordSyncError(0x2C);
    assert.equal(JSON.stringify(c.gEcatSyncDiag), first);
    assert.equal(c.sSyncManOutPar.u16CycleExceededCounter, 0);
});
test("SAFEOP/inactive DC does not produce an OP sync latch", () => {
    for (const flag of ["bEcatOutputUpdateRunning", "bDcSyncActive"]) {
        const { ctx: c, calls: a } = fixture();
        c[flag] = 0;
        c.bSmSyncSequenceValid = 0;
        c.CheckIfEcatError();
        assert.equal(a.al.length, 0);
        assert.equal(c.gEcatSyncDiag.firstAlStatusCode, 0);
    }
});
test("process-data watchdog protection remains active", () => {
    const { ctx: c, calls: a } = fixture();
    c.EcatWdValue = 1; // Mock watchdog state is expired (0).
    c.CheckIfEcatError();
    assert.equal(a.al[0].code, c.ALSTATUSCODE_SMWATCHDOG);
    assert.equal(a.al[0].state, c.STATE_SAFEOP);
});
test("CoE diagnostic flag alone does not bypass/replace sequence protection", () => {
    const { ctx: c, calls: a } = fixture();
    c.sSyncManOutPar.u16CycleExceededCounter = 1;
    c.COE_UpdateSyncErrorStatus();
    assert.equal(c.sSyncManOutPar.u8SyncError, 1);
    c.CheckIfEcatError();
    assert.equal(a.al.length, 0); // Existing SSC behavior: sequence/watchdog decide AL status.
});
test("long replay preserves bounded servicing and SYNC events", () => {
    const { ctx: c, calls: a } = fixture([]);
    let phase = 0;
    c.HW_GetALEventRegister_Isr = () => [S0, O, 0][phase++ % 3];
    for (let i = 0; i < 10000; i++) c.PDI_Isr();
    assert.equal(a.map, 10000);
    assert.equal(a.sync0, 10000);
    assert.equal(phase, 30000);
    assert.equal(c.gEcatSyncDiag.lateOutputEventCount, 10000);
    assert.equal(c.gEcatSyncDiag.outputRetriggerCount, 0);
    assert.equal(c.sSyncManOutPar.u16CycleExceededCounter, 0);
});
console.log("Passed " + passed + " source-coupled replay tests (not ARM/hardware execution).");
