"""Run actual ARM-compiled pulse counter and firmware in an offline emulator.
No probes, GPIO, TwinCAT, or motor connection are used.
"""
from pathlib import Path
import struct
import sys
import time
import os
import subprocess
import json

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests" / ".pulse_test_deps"))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2
from unicorn.arm_const import UC_ARM_REG_R3, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC
from elftools.elf.elffile import ELFFile

USER = ROOT / "program" / "FCM4E1553 Ethercat" / "User"
FIRMWARE = USER.parent / "MDK-ARM" / "N32G455" / "N32G455.axf"

BUILD = ROOT / "tests" / ".pulse_test_build"
BUILD.mkdir(exist_ok=True)
BIN = Path(os.environ.get("KEIL_ARMCC_BIN", "D:/App/Keil_v5/ARM/ARMCC/bin"))
subprocess.run([str(BIN / "armcc.exe"), "--cpu=Cortex-M4", "--c99", "-O2",
                "--library_type=microlib", "-c",
                str(ROOT / "tests" / "pulse_position_arm_test.c"),
                "-o", str(BUILD / "pulse_test.o")], check=True)
subprocess.run([str(BIN / "armlink.exe"), "--cpu=Cortex-M4",
                "--library_type=microlib", "--entry=pulse_test_main",
                "--ro_base=0x08000000", "--rw_base=0x20000000",
                "--libpath=" + str(BIN.parent / "lib"),
                str(BUILD / "pulse_test.o"), "-o", str(BUILD / "pulse_test.axf")],
                check=True)
for name in ("EthercatMotorControl.c", "EthercatMotorControl.h",
             "PulseHandle.c", "PwmInit.c", "EcatPulsePosition.h"):
    if (USER / name).stat().st_mtime > FIRMWARE.stat().st_mtime:
        raise SystemExit("Rebuild Keil firmware first: " + name)

class ArmImage:
    def __init__(self, path):
        self.stream = open(path, "rb")
        self.elf = ELFFile(self.stream)
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        self.uc.mem_map(0x08000000, 0x80000)
        self.uc.mem_map(0x20000000, 0x40000)
        self.stop = 0x0FFF0000
        self.uc.mem_map(self.stop, 0x1000)
        self.symbols = {}
        for segment in self.elf.iter_segments():
            address = segment["p_paddr"]
            if segment["p_type"] == "PT_LOAD" and segment["p_filesz"]:
                if 0x08000000 <= address < 0x08080000:
                    self.uc.mem_write(address, segment.data())
        for section in self.elf.iter_sections():
            if section["sh_flags"] & 2 and section["sh_type"] != "SHT_NOBITS":
                self.uc.mem_write(section["sh_addr"], section.data())
        for symbol in self.elf.get_section_by_name(".symtab").iter_symbols():
            if symbol.name and symbol["st_value"]:
                self.symbols[symbol.name] = symbol["st_value"]
        if "Region$$Table$$Base" in self.symbols:
            # Execute the real copy/decompress/zero handlers only, NOT main().
            begin = self.symbols["Region$$Table$$Base"]
            end = self.symbols["Region$$Table$$Limit"]
            for address in range(begin, end, 16):
                source, dest, size, handler = struct.unpack(
                    "<IIII", self.uc.mem_read(address, 16))
                self.call(handler, source, dest, size, handler, count=2000000)
    def call(self, name, *args, count=200000, timeout=0):
        registers = [UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3]
        for reg, value in zip(registers, args):
            self.uc.reg_write(reg, value & 0xFFFFFFFF)
        self.uc.reg_write(UC_ARM_REG_SP, 0x2003F000)
        self.uc.reg_write(UC_ARM_REG_LR, self.stop | 1)
        address = name if isinstance(name, int) else self.symbols[name]
        self.uc.emu_start(address | 1, self.stop,
                          timeout=timeout, count=count)
        assert self.uc.reg_read(UC_ARM_REG_PC) == self.stop, str(name) + " did not return"
        return self.uc.reg_read(UC_ARM_REG_R0)
    def read(self, name, size=4, signed=False, offset=0):
        return int.from_bytes(self.uc.mem_read(self.symbols[name] + offset, size),
                              "little", signed=signed)
    def write(self, name, value, size=4, offset=0):
        self.uc.mem_write(self.symbols[name] + offset,
                          (value & ((1 << (size * 8)) - 1)).to_bytes(size, "little"))
    def close(self):
        self.stream.close()

def check(condition, message):
    if not condition:
        raise AssertionError(message)

core = ArmImage(ROOT / "tests" / ".pulse_test_build" / "pulse_test.axf")
start = time.perf_counter()
result = core.call("pulse_test_main", count=1000000000, timeout=60000000)
check(result == 0, "ARM pulse core assertion " + str(result))
print("PASS ARM core: 102 signed endpoints / 17 resolutions, fractional restart, "
      "1,250,000 pulses forward and return, DINT bounds (%.2f s)" %
      (time.perf_counter() - start), flush=True)
core.close()

def unwrap_type(die):
    while die.tag in {"DW_TAG_typedef", "DW_TAG_volatile_type",
                      "DW_TAG_const_type", "DW_TAG_array_type"}:
        die = die.get_DIE_from_attribute("DW_AT_type")
    return die

def field(image, variable, *names):
    dwarf = image.elf.get_dwarf_info()
    root_die = next(d for cu in dwarf.iter_CUs() for d in cu.iter_DIEs()
                    if d.tag == "DW_TAG_variable" and
                    d.attributes.get("DW_AT_name") and
                    d.attributes["DW_AT_name"].value == variable.encode())
    die = unwrap_type(root_die.get_DIE_from_attribute("DW_AT_type"))
    offset = 0
    for name in names:
        member = next(d for d in die.iter_children() if d.tag == "DW_TAG_member"
                      and d.attributes["DW_AT_name"].value == name.encode())
        location = member.attributes["DW_AT_data_member_location"].value
        if not isinstance(location, int):
            from elftools.dwarf.dwarf_expr import DWARFExprParser
            ops = DWARFExprParser(dwarf.structs).parse_expr(location)
            assert len(ops) == 1 and ops[0].op_name == "DW_OP_plus_uconst"
            location = ops[0].args[0]
        offset += location
        die = unwrap_type(member.get_DIE_from_attribute("DW_AT_type"))
    return offset, die.attributes["DW_AT_byte_size"].value

fw = ArmImage(FIRMWARE)
locations = {}
def get(variable, *names, signed=False):
    key = (variable,) + names
    if key not in locations:
        locations[key] = field(fw, variable, *names)
    offset, size = locations[key]
    return fw.read(variable, size, signed, offset)
def put(variable, value, *names):
    key = (variable,) + names
    if key not in locations:
        locations[key] = field(fw, variable, *names)
    offset, size = locations[key]
    fw.write(variable, value, size, offset)
def diag(name, signed=False):
    return get("gEcatPulseDiag", name, signed=signed)

def reset(mode=8):
    put("LocalAxes", 0, "Objects", "objTargetPosition")
    fw.call("EthercatMotorControl_Init")
    put("nAlStatus", 8)
    put("LocalAxes", 1, "bAxisIsActive")
    put("LocalAxes", 0x10, "i16State")
    put("LocalAxes", mode, "Objects", "objModesOfOperation")
    put("LocalAxes", 15, "Objects", "objControlWord")
    put("LocalAxes", 2000, "u32CycleTime")
    fw.call("EthercatMotorControl_OnSyncCycle")
    fw.call("EthercatMotorControl_Update")
    fw.call("EthercatMotorControl_OnSyncCycle")
    fw.call("EthercatMotorControl_Update")
    check(fw.read("gEcatMotorEnabled", 1) == 1 and
          diag("resolutionChanged") == 0, "firmware reset/enable failed")

def tick(skip=False):
    angle = fw.read("Iangle")
    fw.call("EthercatMotorControl_BeginPhaseCycle", angle)
    fw.call("PulseFunction")
    step = fw.read("BaseRpm", signed=True)
    if not skip:
        fw.call("PhaseGet", step)
    fw.call("EthercatMotorControl_EndPhaseCycle", fw.read("Iangle"))
    return step

def finish_pp(target, max_ticks=80000):
    for cycle in range(max_ticks):
        tick()
        if cycle % 32 == 31:
            fw.call("EthercatMotorControl_Update")
            if diag("atTarget") and diag("lastPhaseStep", True) == 0:
                check(diag("countedPosition", True) == target, "PP endpoint")
                return cycle + 1
    raise AssertionError("PP did not finish: target=%s counted=%s" %
                         (target, diag("countedPosition", True)))

reset(1)
put("LocalAxes", 1000, "Objects", "objTargetPosition")
for _ in range(64):
    tick()
fw.call("EthercatMotorControl_Update")
check(diag("countedPosition", True) == 0, "PP must not move at controlword 15")
put("LocalAxes", 47, "Objects", "objControlWord")
fw.call("EthercatMotorControl_Update")
put("LocalAxes", 63, "Objects", "objControlWord")
fw.call("EthercatMotorControl_Update")
ticks = finish_pp(1000)
check(diag("mismatchCount") == 0, "library PhaseGet contract mismatch")
print("PASS full firmware PP: 15 holds; 47->63 accepts; exact +1000 (%s ticks)" %
      ticks, flush=True)
for cycle in range(1024):
    check(tick() == 0, "held-high PP new-setpoint bit repeated the move")
    if cycle % 32 == 31:
        fw.call("EthercatMotorControl_Update")
    check(diag("countedPosition", True) == 1000, "PP endpoint did not hold")
check(get("LocalAxes", "Objects", "objModesOfOperationDisplay") == 1,
      "PP test was not actually in mode 1")
print("PASS full firmware PP: one finite target holds, no repeat at held controlword 63",
      flush=True)
put("LocalAxes", 47, "Objects", "objControlWord")
fw.call("EthercatMotorControl_Update")
put("LocalAxes", -1, "Objects", "objTargetPosition")
put("LocalAxes", 63, "Objects", "objControlWord")
fw.call("EthercatMotorControl_Update")
finish_pp(-1)
print("PASS full firmware PP: reverse across origin to -1; no tolerance deadband",
      flush=True)

reset(8)
fw.call("EthercatMotorControl_PublishPulseCommand", 8, 100, 100, 1)
fw.write("gEcatMotorEnabled", 1, 1)
for _ in range(100):
    tick()
before = diag("countedPosition", True)
phase_before = get("sPulsePosition", "phase", signed=True)
tick(skip=True)
check(get("sPulsePosition", "phase", signed=True) == phase_before,
      "skipped PhaseGet fabricated a position")
check(diag("countedPosition", True) == before and diag("mismatchCount") == 1,
      "skipped PhaseGet must be diagnosed without fake feedback")
for _ in range(10000):
    tick()
    if diag("atTarget"):
        break
check(diag("countedPosition", True) == 100 and diag("atTarget"),
      "CSP excitation budget did not clip to endpoint")
phase_before = get("sPulsePosition", "phase", signed=True)
for _ in range(64):
    tick()
check(get("sPulsePosition", "phase", signed=True) == phase_before,
      "stationary CSP endpoint advanced")
print("PASS full firmware CSP: actual PhaseGet accounting, skipped cycle, "
      "last-step clipping and stable hold", flush=True)

put("nAlStatus", 4)
phase_before = get("sPulsePosition", "phase", signed=True)
check(tick() == 0, "loss of OP did not clear the phase command")
check(get("sPulsePosition", "phase", signed=True) == phase_before,
      "loss of OP generated counts")
print("PASS full firmware: OP-loss protection retained", flush=True)
put("nAlStatus", 8)
fw.write("gEcatMotorEnabled", 0, 1)
check(tick() == 0, "disabled motor still advanced")
print("PASS full firmware: disable protection retained", flush=True)

reset(8)
put("LocalAxes", 1000, "Objects", "objTargetPosition")
fw.call("EthercatMotorControl_OnSyncCycle")
fw.call("EthercatMotorControl_Update")
fw.write("sxifen", 2000)
check(tick() == 0 and diag("resolutionChanged") == 1,
      "resolution changed while running without a stop")
print("PASS full firmware: live resolution change cannot corrupt counts", flush=True)
fw.write("sxifen", 1000)
def csp_cycle(target):
    put("LocalAxes", target, "Objects", "objTargetPosition")
    fw.call("EthercatMotorControl_OnSyncCycle")
    fw.call("EthercatMotorControl_Update")
    for _ in range(32):
        tick()

reset(8)
for start_position, end_position in ((0, 1000), (1000, 0)):
    for cycle in range(1, 2001):
        u = cycle / 2000.0
        target = round(start_position + (end_position - start_position) *
                       u * u * (3.0 - 2.0 * u))
        csp_cycle(target)
    for _ in range(2500):
        csp_cycle(end_position)
        if diag("atTarget") and diag("lastPhaseStep", True) == 0:
            break
    else:
        raise AssertionError("CSP trajectory did not settle in 5 seconds")
    check(diag("countedPosition", True) == end_position, "CSP cyclic endpoint")
check(diag("mismatchCount") == 0, "CSP library mismatch")
print("PASS full firmware CSP: 2 ms PDO trajectory, +one revolution and return",
      flush=True)

# CSP must not carry old feed-forward direction into a newly selected target.
reset(8)
put("LocalAxes", 1000, "Objects", "objTargetPosition")
fw.call("EthercatMotorControl_OnSyncCycle")
fw.call("EthercatMotorControl_Update")
put("LocalAxes", 1, "Objects", "objTargetPosition")
fw.call("EthercatMotorControl_OnSyncCycle")
fw.call("EthercatMotorControl_Update")
for _ in range(64):
    check(tick() >= 0, "stale target history generated a reverse step")
print("PASS CSP: target history cannot generate wrong-direction feed-forward",
      flush=True)

# Abrupt external reversal may stop, but never advances farther from new target.
# Normal motion should reverse only after the master's own zero-speed endpoint.
reset(8)
for _ in range(40):
    csp_cycle(1000)
before_reversal = get("sPulsePosition", "phase", signed=True)
put("LocalAxes", -1000, "Objects", "objTargetPosition")
fw.call("EthercatMotorControl_OnSyncCycle")
fw.call("EthercatMotorControl_Update")
negative_seen = False
for _ in range(12000):
    step = tick()
    check(step <= 0, "old positive ramp travelled away from reversed target")
    negative_seen = negative_seen or step < 0
    check(get("sPulsePosition", "phase", signed=True) <= before_reversal,
          "reversal overshot the previous phase")
check(negative_seen and diag("blockedAwayCount") > 0,
      "intentional reversal was blocked permanently or not diagnosed")
print("PASS CSP: legitimate reversal allowed after old-direction braking; "
      "no old-direction emitted travel", flush=True)

for resolution, duration in ((1000, 1.5), (40000, 1.5), (1000, 2.0)):
    fw.write("sxifen", resolution)
    reset(8)
    total = round(duration / 0.002)
    largest_jump = 0
    previous_step = 0
    for start_position, end_position in ((0, resolution), (resolution, 0)):
        direction = 1 if end_position > start_position else -1
        for cycle in range(1, total + 501):
            u = min(cycle / total, 1.0)
            shape = u*u*u*(10.0 + u*(-15.0 + 6.0*u))
            target = round(start_position + (end_position-start_position)*shape)
            put("LocalAxes", target, "Objects", "objTargetPosition")
            fw.call("EthercatMotorControl_OnSyncCycle")
            fw.call("EthercatMotorControl_Update")
            for _ in range(32):
                step = tick()
                check(step * direction >= 0, "monotone CSP reversed")
                largest_jump = max(largest_jump, abs(step - previous_step))
                previous_step = step
                phase = get("sPulsePosition", "phase", signed=True)
                goal = round(target * 15360000 / resolution)
                check((goal-phase)*direction >= 0, "CSP crossed current target")
        check(diag("atTarget") and diag("countedPosition", True) == end_position,
              "smooth trajectory did not finish its exact budget")
    check(largest_jump <= 2, "smooth CSP step-rate jumped over 2 phase units")
    check(diag("mismatchCount") == 0, "phase-library accounting mismatch")
    print("PASS quintic CSP %s counts/rev %.1f s: no unintended reverse or "
          "phase overshoot; exact endpoints; max jump %.4f rpm" %
          (resolution, duration, largest_jump/16), flush=True)
fw.write("sxifen", 1000)

# Close the loop between the actual new PLC ST source and ARM PhaseGet/counting.
# Peripheral/motor mechanics and TwinCAT scheduling are still not emulated.
reset(8)
put("LocalAxes", 0x1227, "Objects", "objStatusWord")
fw.call("EthercatMotorControl_Update")
bridge = subprocess.Popen(
    ["node", str(ROOT / "tests" / "csp_smooth_plc_bridge.js")],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, cwd=ROOT)
previous_target = 0
try:
    for cycle in range(5000):
        request = {
            "iWcState": False, "iEthercatState": 8,
            "iStatusWord": get("LocalAxes", "Objects", "objStatusWord"),
            "iModeDisplay": 8,
            "iActualPosition": diag("countedPosition", True),
            "iActualVelocity": get("LocalAxes", "Objects",
                                   "objVelocityActualValue", signed=True),
        }
        if cycle == 0:
            request.update(bEnable=True, bRun=True)
        bridge.stdin.write(json.dumps(request) + "\n")
        bridge.stdin.flush()
        line = bridge.stdout.readline()
        check(bool(line), "ST bridge exited early")
        result = json.loads(line)
        check(not result["bStopLatched"], "integrated PLC stopped: " + str(result))
        target = result["qTargetPosition"]
        check(target >= previous_target, "new PLC target reversed")
        previous_target = target
        put("LocalAxes", result["qControlWord"], "Objects", "objControlWord")
        put("LocalAxes", target, "Objects", "objTargetPosition")
        fw.call("EthercatMotorControl_OnSyncCycle")
        fw.call("EthercatMotorControl_Update")
        for _ in range(32):
            check(tick() >= 0, "integrated positive repeat reversed")
        fw.call("EthercatMotorControl_Update")
        if result["nCompletedTurns"] == 3:
            break
    else:
        raise AssertionError("integrated PLC did not finish three circles")
    check(target == 3000 and diag("countedPosition", True) == 3000,
          "integrated PLC/firmware endpoint mismatch")
    print("PASS new PLC ST + ARM firmware: three complete circles, 500 ms "
          "pauses, no reverse commands or premature completion", flush=True)
finally:
    bridge.stdin.close()
    bridge.wait(timeout=10)

reset(9)
put("LocalAxes", 60, "Objects", "objTargetVelocity")
fw.call("EthercatMotorControl_OnSyncCycle")
fw.call("EthercatMotorControl_Update")
for _ in range(4000):
    tick()
check(diag("lastPhaseStep", True) == 960, "CSV rpm calibration changed")
phase_before = get("sPulsePosition", "phase", signed=True)
for _ in range(16000):
    tick()
check(get("sPulsePosition", "phase", signed=True) - phase_before == 15360000,
      "CSV one-second 60 rpm count")
print("PASS full firmware CSV: 60 rpm still gives one revolution per second",
      flush=True)
put("ErrNumFlag", 10)
fw.call("EthercatMotorControl_Update")
check(fw.read("gEcatMotorEnabled", 1) == 0, "motor protection no longer disables")
check(tick() == 0, "fault-disabled motor emitted a step")
print("PASS full firmware: latched motor fault protection retained", flush=True)
put("ErrNumFlag", 0)

fw.close()
print("All pulse-position ARM checks passed; no hardware test was performed.",
      flush=True)
