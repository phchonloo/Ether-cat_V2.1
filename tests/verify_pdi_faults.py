"""Execute the built ARM firmware with mocked QSPI/ESC hardware (no motor).

This verifies software failure paths, NOT wire timing, WKC, or real recovery.
Reuses only helper definitions from verify_pulse_position.py, not its test run.
"""
import ast
import sys
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests' / '.pulse_test_deps'))
from unicorn import (Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS,
                     UC_HOOK_CODE, UC_HOOK_MEM_WRITE)
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
    UC_ARM_REG_R3, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_PRIMASK)
from elftools.elf.elffile import ELFFile

helpers = ast.parse((ROOT / 'tests' / 'verify_pulse_position.py').read_text('utf8'))
helpers.body = [n for n in helpers.body if isinstance(n, (ast.ClassDef, ast.FunctionDef))
                and n.name in ('ArmImage', 'unwrap_type', 'field')]
exec(compile(helpers, 'pulse_test_helpers', 'exec'))
FIRMWARE = ROOT / 'program' / 'FCM4E1553 Ethercat' / 'MDK-ARM' / 'N32G455' / 'N32G455.axf'
for folder in ('User', 'Ethercat/src', 'Ethercat/port', 'Ethercat/Inc'):
    for src in (FIRMWARE.parents[2] / folder).glob('*.[ch]'):
        assert src.stat().st_mtime <= FIRMWARE.stat().st_mtime, 'Rebuild first: ' + str(src)


class Fixture(ArmImage):
    def __init__(self):
        super().__init__(FIRMWARE)
        self.uc.mem_map(0xA0001000, 0x1000)
        self.uc.mem_map(0xE0000000, 0x100000)
        self.callbacks = {}
        self.locations = {}
        self.sent = []
        self.rx = [0x12, 0x34, 0x56, 0x78]
        self.fifo = 4
        self.busy = lambda: 0
        self.aborts = 0
        self.data = 0x20030000
        self.uc.mem_write(self.data, bytes([0xA5]) * 512)
        self.hook('QspiSendWord', lambda x, *_: self.sent.append(x))
        self.hook('QspiReadWord', lambda *_: self.rx.pop(0))
        self.hook('QspiReadRxFifoNum', lambda *_: self.fifo)
        self.hook('GetQspiBusyStatus', lambda *_: self.busy())
        self.hook('QSPI_DeInit', self.deinit)
        for n in ('QspiInitConfig', 'QSPI_Cmd', 'RCC_EnableAHBPeriphClk',
                  'GPIO_ConfigPinRemap'):
            self.hook(n, lambda *_: 0)
        self.uc.hook_add(UC_HOOK_CODE, self.dispatch)

    def deinit(self, *_):
        self.aborts += 1

    def hook(self, name, fn):
        self.callbacks[self.symbols[name] & ~1] = fn

    def dispatch(self, uc, address, size, _):
        fn = self.callbacks.get(address)
        if fn:
            result = fn(*(uc.reg_read(r) for r in
                (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)))
            uc.reg_write(UC_ARM_REG_R0, (result or 0) & 0xFFFFFFFF)
            uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

    def loc(self, var, names):
        key = (var,) + names
        if key not in self.locations:
            self.locations[key] = field(self, var, *names)
        return self.locations[key]

    def get(self, var, *names):
        off, size = self.loc(var, names)
        return self.read(var, size, offset=off)

    def put(self, var, value, *names):
        off, size = self.loc(var, names)
        self.write(var, value, size, offset=off)

    def pdi(self, name):
        return self.get('gEscPdiDiag', name)

    def comm(self, name):
        return self.get('gEcatCommDiag', name)


passed = 0
def test(name, fn):
    global passed
    fw = Fixture()
    try:
        fn(fw)
        passed += 1
        print('PASS ' + name, flush=True)
    finally:
        fw.close()


def normal(f):
    assert f.call('qspi_read', f.data, 0x1100, 4) == 1
    assert bytes(f.uc.mem_read(f.data, 4)) == bytes([0x12, 0x34, 0x56, 0x78])
    assert f.pdi('transferCount') == 1 and f.pdi('firstError') == 0
    assert f.call('qspi_write', f.data, 0x1400, 4) == 1
    assert f.pdi('transferCount') == 2
test('complete four-byte read/write succeed', normal)


def invalid(f):
    assert f.call('qspi_read', f.data, 0x1100, 0) == 0
    assert f.call('qspi_write', f.data, 0x1400, 5) == 0
    assert f.pdi('firstError') == 1 and f.pdi('faultLatched') == 1
    assert f.pdi('readFailureCount') == 1 and f.pdi('writeFailureCount') == 1
    assert f.sent == []
test('zero/oversized low-level transfers rejected before NDF/FIFO access', invalid)


def idle_timeout(f):
    f.busy = lambda: 1
    assert f.call('qspi_read', f.data, 0x1100, 4, count=2000000) == 0
    assert f.pdi('firstError') == 2 and f.pdi('timeoutCount') == 1
    assert f.pdi('firstAddress') == 0x1100 and f.pdi('firstLength') == 4
    assert f.sent == [] and bytes(f.uc.mem_read(f.data, 4)) == b'\xA5' * 4
    f.busy = lambda: 0
    assert f.call('qspi_read', f.data, 0x1100, 4) == 1
    assert f.pdi('faultLatched') == 1 and f.pdi('firstError') == 2
test('busy timeout is bounded; successful traffic never auto-clears fault', idle_timeout)


def timed_timeout(f):
    polls = []
    f.write('SystemCoreClock', 1000000)
    f.uc.mem_write(0xE0001000, struct.pack('<II', 1, 0xFFFFFFF0))
    def busy():
        polls.append(1)
        value = int.from_bytes(f.uc.mem_read(0xE0001004, 4), 'little')
        f.uc.mem_write(0xE0001004, struct.pack('<I', (value + 16) & 0xFFFFFFFF))
        return 1
    f.busy = busy
    assert f.call('qspi_read', f.data, 0x1100, 4) == 0
    assert len(polls) == 7 and f.pdi('firstError') == 2
test('DWT deadline remains bounded across CYCCNT rollover', timed_timeout)


def read_timeout(f):
    f.busy = lambda: int(len(f.sent) != 0)
    assert f.call('qspi_read', f.data, 0x1104, 4, count=2000000) == 0
    assert f.pdi('firstError') == 3 and f.rx == [0x12, 0x34, 0x56, 0x78]
    assert bytes(f.uc.mem_read(f.data, 4)) == b'\xA5' * 4
test('read completion timeout does not consume/publish FIFO', read_timeout)


def short_fifo(f):
    f.fifo = 3
    assert f.call('qspi_read', f.data, 0x1108, 4) == 0
    assert f.pdi('firstError') == 4 and f.pdi('timeoutCount') == 0
    assert bytes(f.uc.mem_read(f.data, 4)) == b'\xA5' * 4
    assert f.rx == [0x12, 0x34, 0x56, 0x78]
    f.busy = lambda: 1
    assert f.call('qspi_read', f.data, 0x220, 4, count=2000000) == 0
    assert f.pdi('firstError') == 4 and f.pdi('firstAddress') == 0x1108
    assert f.pdi('lastError') == 2
test('short FIFO fails atomically and first-error evidence is preserved', short_fifo)


def write_timeout(f):
    f.busy = lambda: int(len(f.sent) != 0)
    assert f.call('qspi_write', f.data, 0x1400, 4, count=2000000) == 0
    assert f.pdi('firstError') == 5 and len(f.sent) == 6
    assert f.pdi('writeFailureCount') == 1
test('write completion timeout aborts without replay', write_timeout)


def aligned_read(f):
    calls = []
    def read(ptr, addr, count, _):
        calls.append(addr)
        assert count == 4 and (addr & 3) == 0
        f.uc.mem_write(ptr, bytes((addr + i) & 255 for i in range(4)))
        return 1
    f.hook('qspi_read', read)
    for offset in range(4):
        for length in (1, 2, 3, 4, 5, 12, 128, 256):
            calls.clear()
            assert f.call('HW_EscReadIsr', f.data, 0x1100 + offset, length) == 1
            assert bytes(f.uc.mem_read(f.data, length)) == bytes(
                (offset + i) & 255 for i in range(length))
            assert len(calls) == (offset + length + 3) // 4
    assert f.uc.reg_read(UC_ARM_REG_PRIMASK) == 0
test('32 aligned/unaligned block reads; bounded staging and PRIMASK restore', aligned_read)


def partial_read(f):
    calls = []
    def read(ptr, addr, count, _):
        calls.append(addr)
        if len(calls) == 2:
            return 0
        f.uc.mem_write(ptr, b'\x55' * count)
        return 1
    f.hook('qspi_read', read)
    assert f.call('HW_EscRead', f.data, 0x1100, 12) == 0
    assert calls == [0x1100, 0x1104]
    assert bytes(f.uc.mem_read(f.data, 12)) == b'\0' * 12
    assert f.uc.reg_read(UC_ARM_REG_PRIMASK) == 0
test('middle-chunk read failure returns zeros/status, no mixed PDO', partial_read)


def partial_write(f):
    calls = []
    f.hook('qspi_write', lambda ptr, addr, count, _: calls.append(addr) or int(len(calls) != 2))
    assert f.call('HW_EscWrite', f.data, 0x1400, 12) == 0
    assert calls == [0x1400, 0x1404]
    assert f.uc.reg_read(UC_ARM_REG_PRIMASK) == 0
test('middle-chunk write failure stops further ESC writes', partial_write)


def mapping_failure(f):
    calls = []
    f.hook('HW_EscReadIsr', lambda *_: 0)
    f.hook('APPL_OutputMapping', lambda *_: calls.append('mapped'))
    f.write('nPdOutputSize', 12, 2)
    f.call('PDO_OutputMapping')
    assert calls == [] and f.comm('pdoReadErrorCount') == 1
    assert f.comm('pdoReadOkCount') == 0
    f.hook('APPL_InputMapping', lambda *_: 0)
    f.hook('HW_EscWriteIsr', lambda *_: 0)
    f.call('PDO_InputMapping')
    assert f.comm('pdoWriteErrorCount') == 1
test('failed PDO read never reaches application; failed TxPDO is counted', mapping_failure)


def reset_gate(f):
    f.put('gEscPdiDiag', 1, 'faultLatched')
    f.put('gEscPdiDiag', 2, 'errorEpoch')
    f.put('gEscPdiDiag', 100, 'successSinceError')
    f.put('gEcatCommDiag', 2, 'pdoErrorEpoch')
    f.put('gEcatCommDiag', 32, 'pdoSincePdiError')
    f.put('gEcatCommDiag', 1, 'resetRequestPending')
    f.write('nAlStatus', 8, 1)
    f.write('bEcatOutputUpdateRunning', 1, 1)
    f.put('LocalAxes', 0x80, 'Objects', 'objControlWord')
    f.put('LocalAxes', 0, 'Objects', 'objVelocityActualValue')
    for variable, names, bad, good in [
        ('gEcatCommDiag', ('pdoSincePdiError',), 31, 32),
        ('gEcatCommDiag', ('pdoErrorEpoch',), 1, 2),
        ('gEcatCommDiag', ('resetRequestPending',), 0, 1),
        ('gEscPdiDiag', ('successSinceError',), 0, 100),
        ('LocalAxes', ('Objects', 'objControlWord'), 0x8F, 0x80),
        ('LocalAxes', ('Objects', 'objVelocityActualValue'), 1, 0)]:
        f.put(variable, bad, *names)
        assert f.call('ECAT_TryResetPdiFault') == 0
        assert f.pdi('faultLatched') == 1
        f.put(variable, good, *names)
    f.write('nAlStatus', 4, 1)
    assert f.call('ECAT_TryResetPdiFault') == 0
    f.write('nAlStatus', 8, 1)
    assert f.call('ECAT_TryResetPdiFault') == 1
    assert f.pdi('faultLatched') == 0 and f.comm('manualResetCount') == 1
    assert f.read('gEcatMotorEnabled', 1) == 0
test('manual re-arm requires OP, fresh PDOs, low-before-reset and disabled/stationary', reset_gate)


def reset_edge(f):
    f.hook('HW_EscReadIsr', lambda *_: 1)
    f.hook('APPL_OutputMapping', lambda *_: 0)
    f.put('gEscPdiDiag', 1, 'faultLatched')
    f.put('gEscPdiDiag', 5, 'errorEpoch')
    f.put('gEscPdiDiag', 100, 'successSinceError')
    f.put('LocalAxes', 0, 'Objects', 'objControlWord')
    f.call('PDO_OutputMapping')
    assert f.comm('resetLowSeen') == 1
    f.put('LocalAxes', 0x80, 'Objects', 'objControlWord')
    f.call('PDO_OutputMapping')  # Too early: only 2 valid PDOs.
    for _ in range(40):
        f.call('PDO_OutputMapping')
    assert f.comm('resetRequestPending') == 0  # Held high cannot re-arm later.
    f.put('LocalAxes', 0, 'Objects', 'objControlWord')
    f.call('PDO_OutputMapping')
    f.put('LocalAxes', 0x80, 'Objects', 'objControlWord')
    assert f.comm('resetRequestPending') == 0  # OD change alone is insufficient.
    f.call('PDO_OutputMapping')
    assert f.comm('resetRequestPending') == 1
    f.put('gEscPdiDiag', 6, 'errorEpoch')
    f.call('PDO_OutputMapping')
    assert f.comm('resetRequestPending') == 0 and f.comm('pdoSincePdiError') == 1
test('fresh PDO reset edge required; early/held-high/SDO-only requests rejected', reset_edge)


def motion_inhibit(f):
    f.call('EthercatMotorControl_Init')
    f.put('gEscPdiDiag', 1, 'faultLatched')
    f.write('gEcatMotorEnabled', 1, 1)
    assert f.call('EthercatMotorControl_NextPhaseStep') == 0
    assert f.read('gEcatMotorEnabled', 1) == 0
    f.put('LocalAxes', 1, 'bAxisIsActive')
    f.put('LocalAxes', 0x10, 'i16State')
    f.call('EthercatMotorControl_Update')
    assert f.get('LocalAxes', 'Objects', 'objErrorCode') == 0x7500
    assert f.read('u16ErrorRegister', 1) & 0x11 == 0x11
test('PDI fault immediately inhibits phase commands and reports CiA402 communication error', motion_inhibit)


def pwm_inhibit(f):
    calls = []
    f.put('gEscPdiDiag', 1, 'faultLatched')
    f.write('gEcatMotorEnabled', 1, 1)
    f.hook('TIM_GetIntStatus', lambda *_: 1)
    f.hook('TIM_ClrIntPendingBit', lambda *_: 0)
    f.hook('ErrorHandle', lambda *_: 0)
    f.hook('UpPwm', lambda *args: calls.append(args))
    f.hook('MainFunt', lambda *_: calls.append('algorithm'))
    f.call('TIM8_UP_IRQHandler')
    assert calls == [(0, 0, 0, 0)]
    assert f.read('gEcatMotorEnabled', 1) == 0
test('real motor ISR skips algorithm and commands zero PWM on PDI fault', pwm_inhibit)


def sm_failure(f):
    calls = []
    f.hook('HW_EscWrite', lambda *_: 0)
    f.hook('HW_EscRead', lambda *_: calls.append(1) or 0)
    f.call('DisableSyncManChannel', 2)
    assert calls == []
    f.hook('HW_EscWrite', lambda *_: 1)
    def read(ptr, addr, count, _):
        calls.append(1)
        f.uc.mem_write(ptr, b'\0' * count)
        return 1
    f.hook('HW_EscRead', read)
    f.call('DisableSyncManChannel', 2, count=2000000)
    assert len(calls) == 256 and f.pdi('firstError') == 6
test('SyncManager waits exit on write failure or bounded acknowledgement timeout', sm_failure)


def al_record(f):
    f.hook('HW_EscWrite', lambda *_: 1)
    f.write('nAlStatus', 8, 1)
    f.call('SetALStatus', 0x14, 0x1A)
    assert f.comm('firstOpExitValid') == 1
    assert f.comm('firstOpExitFrom') == 8 and f.comm('firstOpExitTo') == 0x14
    assert f.comm('firstOpExitCode') == 0x1A
    f.call('SetALStatus', 8, 0)
    f.call('SetALStatus', 1, 0)
    assert f.comm('firstOpExitCode') == 0x1A and f.comm('alTransitionCount') == 3
test('first OP exit survives recovery and later AL transitions', al_record)


def al_preassigned(f):
    f.hook('HW_EscWrite', lambda *_: 1)
    f.write('nAlStatus', 8, 1)
    f.call('SetALStatus', 8, 0)
    # AL_ControlInd assigns nAlStatus before publishing the AL registers.
    f.write('nAlStatus', 0x14, 1)
    f.call('SetALStatus', 0x14, 0x1A)
    assert f.comm('firstOpExitValid') == 1
    assert f.comm('firstOpExitFrom') == 8
    assert f.comm('firstOpExitTo') == 0x14
    assert f.comm('firstOpExitCode') == 0x1A
test('OP exit is recorded even when SSC preassigns nAlStatus', al_preassigned)


def al_real_local_error(f):
    f.hook('HW_EscWrite', lambda *_: 1)
    for name in ('CheckSmSettings', 'APPL_StopOutputHandler',
                 'StopOutputHandler', 'DisableSyncManChannel'):
        f.hook(name, lambda *_: 0)
    f.write('nAlStatus', 8, 1)
    f.write('bEcatOutputUpdateRunning', 1, 1)
    f.write('nPdOutputSize', 12, 2)
    f.put('gEcatCommDiag', 1234, 'pdoReadOkCount')
    f.uc.mem_write(0xE0001004, struct.pack('<I', 0x12345678))
    f.call('SetALStatus', 8, 0)
    # Execute SSC's real path, including its assignment before SetALStatus.
    f.call('AL_ControlInd', 4, 0x1B)
    assert f.read('nAlStatus', 1) == 0x14
    assert f.comm('firstOpExitValid') == 1
    assert f.comm('firstOpExitCode') == 0x1B
    assert f.comm('firstOpExitFrom') == 8 and f.comm('firstOpExitTo') == 0x14
    assert f.comm('firstOpExitCycle') == 0x12345678
    assert f.comm('firstOpExitPdoCount') == 1234
    assert f.comm('diagVersion') == 0x20261014
test('real SSC AL_ControlInd path records OP exit and timing evidence', al_real_local_error)


def al_retained_code(f):
    f.hook('HW_EscWrite', lambda *_: 1)
    f.write('nAlStatus', 8, 1)
    f.call('SetALStatus', 8, 0x2C)
    f.write('nAlStatus', 0x18, 1)
    f.call('SetALStatus', 0x18, 0xFFFF)
    assert f.comm('firstOpExitValid') == 1
    assert f.comm('firstOpExitCode') == 0x2C
    assert f.comm('lastAlCode') == 0x2C
    assert f.comm('firstOpExitTo') == 0x18
    f.call('SetALStatus', 4, 0x1A)
    assert f.comm('firstOpExitTo') == 0x18
    assert f.comm('firstOpExitCode') == 0x2C
test('OP error-bit transition preserves retained AL code and first evidence', al_retained_code)


def enabled_pdo_gap(f):
    f.hook('HW_EscReadIsr', lambda *_: 1)
    f.hook('APPL_OutputMapping', lambda *_: 0)
    f.uc.mem_write(0xE0001000, struct.pack('<I', 1))
    def sample(cycle, enabled):
        f.uc.mem_write(0xE0001004, struct.pack('<I', cycle))
        f.write('gEcatMotorEnabled', enabled, 1)
        f.call('PDO_OutputMapping')
    sample(10, 0)
    assert f.comm('lastPdoGapValid') == 0
    assert f.comm('lastPdoGapCycles') == 0
    sample(100000, 1)  # Start gap must not count as a running gap.
    assert f.comm('maxEnabledPdoGapCycles') == 0
    sample(100800, 1)
    assert f.comm('maxEnabledPdoGapCycles') == 800
    assert f.comm('lastPdoGapValid') == 1
    assert f.comm('lastPdoGapCycles') == 800
    sample(40000000, 0)
    sample(80000000, 1)
    sample(80000200, 1)
    assert f.comm('maxEnabledPdoGapCycles') == 800
    f.uc.mem_write(0xE0001000, struct.pack('<I', 0))
    sample(90000000, 1)
    assert f.comm('lastPdoGapValid') == 0
    f.uc.mem_write(0xE0001000, struct.pack('<I', 1))
    sample(0xFFFFFFF0, 1)  # First sample after enabling DWT is not a gap.
    assert f.comm('lastPdoGapValid') == 0
    sample(0x400, 1)
    assert f.comm('maxEnabledPdoGapCycles') == 0x410
    assert f.comm('lastPdoGapCycles') == 0x410
    assert f.comm('lastPdoGapValid') == 1
    assert f.comm('diagVersion') == 0x20261014
    assert f.comm('maxPdoGapCycles') > f.comm('maxEnabledPdoGapCycles')
test('enabled PDO gap excludes start/disabled gaps; DWT enable and wrap handled', enabled_pdo_gap)


def disable_evidence(f):
    f.call('EthercatMotorControl_Init')
    f.write('sxifen', f.get('gEcatPulseDiag', 'resolution'))
    f.put('LocalAxes', 1, 'Objects', 'objModesOfOperation')
    f.put('LocalAxes', 1, 'bAxisIsActive')
    f.put('LocalAxes', 0x10, 'i16State')
    f.write('nAlStatus', 8, 1)
    f.call('EthercatMotorControl_Update')  # Mode change settles disabled first.
    f.call('EthercatMotorControl_Update')
    assert f.read('gEcatMotorEnabled', 1) == 1
    f.put('gEcatCommDiag', 5678, 'pdoReadOkCount')
    f.put('gEcatCommDiag', 288001, 'maxEnabledPdoGapCycles')
    f.put('gEcatCommDiag', 12345, 'lastPdoGapCycles')
    f.put('gEcatCommDiag', 1, 'lastPdoGapValid')
    f.put('gEcatCommDiag', 0x23456000, 'lastPdoCycle')
    f.put('gEcatCommDiag', 0, 'lastPdoControlWord')
    f.uc.mem_write(0xE0001004, struct.pack('<I', 0x23456789))
    f.put('LocalAxes', 0, 'Objects', 'objControlWord')
    f.put('LocalAxes', 0x40, 'i16State')
    f.call('EthercatMotorControl_Update')
    assert f.comm('firstDisableValid') == 1
    assert f.comm('firstDisableCycle') == 0x23456789
    assert f.comm('firstDisablePdoCount') == 5678
    assert f.comm('firstDisableMaxEnabledPdoGapCycles') == 288001
    assert f.comm('firstDisableLastPdoGapCycles') == 12345
    assert f.comm('firstDisableLastPdoGapValid') == 1
    assert f.comm('firstDisableLastPdoCycle') == 0x23456000
    assert f.comm('firstDisableLastPdoControlWord') == 0
    assert f.uc.reg_read(UC_ARM_REG_PRIMASK) == 0
    assert f.comm('firstDisableControlWord') == 0
    assert f.read('gEcatMotorEnabled', 1) == 0
    f.put('gEcatCommDiag', 9999, 'pdoReadOkCount')
    f.put('gEcatCommDiag', 777, 'lastPdoGapCycles')
    f.call('EthercatMotorControl_Update')
    assert f.comm('firstDisablePdoCount') == 5678
    assert f.comm('firstDisableLastPdoGapCycles') == 12345
test('master disable preserves first-cycle/PDO/gap evidence and still stops motor', disable_evidence)


def burst_fixture(f, controls, fail_second=False):
    # Run the actual ARM PDI/OutputMapping functions. Only ESC accesses and
    # application payload decoding are mocked; no NVIC/timing simulation.
    f.write('bEscIntEnabled', 1, 1)
    f.write('bEcatOutputUpdateRunning', 1, 1)
    f.write('bDcRunning', 1, 1)
    f.write('bDcSyncActive', 1, 1)
    f.write('nPdOutputSize', 12, 2)
    f.write('nPdInputSize', 11, 2)
    f.write('nEscAddrOutputData', 0x1100, 2)
    f.put('sSyncManOutPar', 2, 'u16SyncType')
    f.put('sSyncManInPar', 2, 'u16SyncType')
    f.put('sSyncManOutPar', 4, 'u16SmEventMissedCounter')
    f.uc.mem_write(0xE0001000, struct.pack('<I', 1))
    trace = {'events': 0, 'copies': 0, 'published': []}
    def event(*_):
        trace['events'] += 1
        assert trace['events'] <= 2, 'Unexpected/unbounded AL event polling'
        return 0x0400
    def copy(dst, address, length, *_):
        idx = trace['copies']
        trace['copies'] += 1
        assert idx < 2 and address == 0x1100 and length == 12
        if fail_second and idx == 1:
            # Mock the failed ESC copy and its PDI error latch. Low-level
            # timeout/latch generation is executed separately above.
            f.put('gEscPdiDiag', 1, 'faultLatched')
            f.put('gEscPdiDiag', 1, 'errorEpoch')
            return 0
        f.uc.mem_write(dst, struct.pack('<H', controls[idx]) + bytes(10))
        f.uc.mem_write(0xE0001004, struct.pack('<I', 100 + idx * 633600))
        return 1
    def publish(src, *_):
        cw = struct.unpack('<H', f.uc.mem_read(src, 2))[0]
        trace['published'].append(cw)
        f.put('LocalAxes', cw, 'Objects', 'objControlWord')
    f.hook('HW_GetALEventRegister_Isr', event)
    f.hook('HW_EscReadIsr', copy)
    f.hook('APPL_OutputMapping', publish)
    return trace


def burst_stop(f):
    trace = burst_fixture(f, [15, 0])
    f.call('PDI_Isr')
    assert trace == {'events': 2, 'copies': 2, 'published': [15, 0]}
    assert f.get('LocalAxes', 'Objects', 'objControlWord') == 0
    assert f.comm('lastPdoControlWord') == 0
    assert f.comm('lastPdoGapValid') == 1
    assert f.comm('lastPdoGapCycles') == 633600  # Synthetic 4.4 ms at 144 MHz.
    assert f.comm('pdoReadOkCount') == 2
    assert f.get('gEcatSyncDiag', 'outputRetriggerReadCount') == 1
    assert f.get('sSyncManOutPar', 'u16CycleExceededCounter') == 1
    assert f.get('sSyncManOutPar', 'u16SmEventMissedCounter') == 3
test('ARM burst publishes a second controlword-zero PDO instead of discarding it', burst_stop)


def burst_failure(f):
    trace = burst_fixture(f, [15, 0], fail_second=True)
    f.call('PDI_Isr')
    assert trace == {'events': 2, 'copies': 2, 'published': [15]}
    assert f.comm('pdoReadOkCount') == 1
    assert f.comm('pdoReadErrorCount') == 1
    assert f.comm('lastPdoControlWord') == 15
    assert f.comm('lastPdoGapValid') == 0
    assert f.pdi('faultLatched') == 1
    assert f.get('gEcatSyncDiag', 'outputRetriggerReadCount') == 1
    assert f.call('ECAT_TryResetPdiFault') == 0
test('ARM burst read failure publishes no partial/zero data and cannot auto-clear fault', burst_failure)


def irq_port_fixture(f, mock_pdi=True):
    # Execute the real compiled EXTI/TIM6 handlers. Model only GPIO, EXTI W1C
    # and software-pending side effects; Unicorn does not model NVIC scheduling.
    f.uc.mem_map(0x40000000, 0x40000)
    f.uc.mem_write(0x40010400, struct.pack('<I', 1))  # EXTI0 IMASK enabled.
    state = {'low': False, 'pending': 1, 'pdi': 0, 'requests': 0,
             'timer': 0, 'acks': [], 'new_edge': False}
    def ack(line, *_):
        state['acks'].append(line)
        state['pending'] &= ~line
    def pdi(*_):
        state['pdi'] += 1
        f.put('gEcatSyncDiag',
              (f.get('gEcatSyncDiag', 'pdiIsrCount') + 1) & 0xFFFFFFFF,
              'pdiIsrCount')
        if state['new_edge']:
            state['pending'] |= 1
            state['low'] = True
    def request(line, *_):
        assert line == 1
        state['requests'] += 1
        state['pending'] |= line
    def timer(*_):
        state['timer'] += 1
    f.hook('EXTI_ClrITPendBit', ack)
    f.hook('GPIO_ReadInputDataBit', lambda *_: 0 if state['low'] else 1)
    f.hook('EXTI_GetITStatus',
           lambda line, *_: 1 if state['pending'] & line else 0)
    f.hook('EXTI_TriggerSWInt', request)
    f.hook('ECAT_CheckTimer', timer)
    f.hook('TIM_ClrIntPendingBit', lambda *_: 0)
    if mock_pdi:
        f.hook('PDI_Isr', pdi)
    return state


def irq_new_edge_retained(f):
    s = irq_port_fixture(f)
    s['new_edge'] = True
    f.call('EXTI0_IRQHandler')
    assert s['acks'] == [1] and s['pdi'] == 1
    assert s['pending'] == 1 and s['low']
    assert s['requests'] == 0  # No tight software self-pend loop.
    assert f.get('gEscIrqDiag', 'lowAfterServiceCount') == 1
    assert f.get('gEscIrqDiag', 'version') == 0x20261015
test('ARM IRQ clears entry edge only; a new edge during service stays pending',
     irq_new_edge_retained)


def irq_burst_budget(f):
    s = irq_port_fixture(f, mock_pdi=False)
    s['low'] = True  # Persistent level cannot multiply the two-copy budget.
    trace = burst_fixture(f, [15, 0])
    f.call('EXTI0_IRQHandler')
    assert trace == {'events': 2, 'copies': 2, 'published': [15, 0]}
    assert f.get('LocalAxes', 'Objects', 'objControlWord') == 0
    assert f.get('gEcatSyncDiag', 'pdiIsrCount') == 1
    assert f.get('gEscIrqDiag', 'lowAfterServiceCount') == 1
    assert s['requests'] == 0 and s['acks'] == [1]
test('ARM hardware IRQ retains second-PDO stop and never exceeds two full copies',
     irq_burst_budget)


def irq_burst_failure(f):
    s = irq_port_fixture(f, mock_pdi=False)
    s['low'] = True
    trace = burst_fixture(f, [15, 0], fail_second=True)
    f.call('EXTI0_IRQHandler')
    assert trace == {'events': 2, 'copies': 2, 'published': [15]}
    assert f.pdi('faultLatched') == 1 and f.comm('pdoReadErrorCount') == 1
    assert f.call('ECAT_TryResetPdiFault') == 0
    assert s['requests'] == 0
test('ARM IRQ burst failure retains PDI fault and publishes no partial command',
     irq_burst_failure)


def sync0_new_edge_retained(f):
    s = irq_port_fixture(f)
    s['pending'] = 2
    calls = []
    def sync(*_):
        assert s['pending'] == 0
        calls.append(1)
        s['pending'] = 2
    f.hook('Sync0_Isr', sync)
    f.call('EXTI1_IRQHandler')
    assert calls == [1] and s['acks'] == [2] and s['pending'] == 2
test('ARM SYNC0 clears before service and retains an edge arriving during it',
     sync0_new_edge_retained)


def irq_stuck_recovery(f):
    s = irq_port_fixture(f)
    s.update(low=True, pending=0)
    f.call('TIM6_IRQHandler')
    assert s['requests'] == 0
    f.call('TIM6_IRQHandler')
    assert s['requests'] == 1 and s['pending'] == 1
    assert s['pdi'] == 0 and s['timer'] == 2
    assert f.get('gEscIrqDiag', 'recoveryRequestCount') == 1
    for _ in range(8):
        f.call('TIM6_IRQHandler')
    assert s['requests'] == 1 and s['timer'] == 10
    # Normal IRQ entry consumes the pending request; the timer never runs PDI.
    s['low'] = False
    f.call('EXTI0_IRQHandler')
    assert s['pdi'] == 1 and s['pending'] == 0
test('ARM two idle low observations request IRQ once; pending work is not duplicated',
     irq_stuck_recovery)


def irq_live_progress(f):
    s = irq_port_fixture(f)
    s.update(low=True, pending=0)
    for count in (1, 2, 3, 0xFFFFFFFF, 0, 1):
        f.put('gEcatSyncDiag', count, 'pdiIsrCount')
        f.call('TIM6_IRQHandler')
    assert s['requests'] == 0
    f.call('TIM6_IRQHandler')
    assert s['requests'] == 0
    f.call('TIM6_IRQHandler')
    assert s['requests'] == 1
test('ARM live PDI progress including counter wrap resets stuck-line qualification',
     irq_live_progress)


def irq_recovery_filters(f):
    s = irq_port_fixture(f)
    s.update(low=True, pending=0)
    f.call('TIM6_IRQHandler')  # One low observation.
    s['low'] = False
    f.call('TIM6_IRQHandler')
    s['low'] = True
    f.call('TIM6_IRQHandler')
    assert s['requests'] == 0
    s['pending'] = 1
    for _ in range(3):
        f.call('TIM6_IRQHandler')
    assert s['requests'] == 0
    s['pending'] = 0
    f.uc.mem_write(0x40010400, struct.pack('<I', 0))
    for _ in range(3):
        f.call('TIM6_IRQHandler')
    assert s['requests'] == 0
    f.uc.mem_write(0x40010400, struct.pack('<I', 1))
    f.call('TIM6_IRQHandler')
    assert s['requests'] == 0
    f.call('TIM6_IRQHandler')
    assert s['requests'] == 1
test('ARM high line, pending edge or masked EXTI resets the recovery qualifier',
     irq_recovery_filters)


def irq_recovery_repeat_and_fault(f):
    s = irq_port_fixture(f)
    s.update(low=True, pending=0)
    f.put('gEscPdiDiag', 1, 'faultLatched')
    f.put('gEscPdiDiag', 3, 'firstError')
    f.write('nAlStatus', 0x14, 1)
    f.put('LocalAxes', 0, 'Objects', 'objControlWord')
    for expected in (1, 2, 3):
        s['pending'] = 0  # Model a serviced request with a still-stuck line.
        f.call('TIM6_IRQHandler')
        assert s['requests'] == expected - 1
        f.call('TIM6_IRQHandler')
        assert s['requests'] == expected
    assert s['pdi'] == 0 and s['timer'] == 6
    assert f.pdi('faultLatched') == 1 and f.pdi('firstError') == 3
    assert f.read('nAlStatus', 1) == 0x14
    assert f.get('LocalAxes', 'Objects', 'objControlWord') == 0
    assert f.read('gEcatMotorEnabled', 1) == 0
test('ARM repeated stuck-line requests are bounded and preserve fault/AL/drive state',
     irq_recovery_repeat_and_fault)


def irq_diagnostics_saturate(f):
    s = irq_port_fixture(f)
    s['low'] = True
    f.put('gEscIrqDiag', 0xFFFFFFFF, 'lowAfterServiceCount')
    f.put('gEscIrqDiag', 0xFFFFFFFF, 'recoveryRequestCount')
    f.call('EXTI0_IRQHandler')
    for _ in range(3):  # First check observes the PDI progress.
        f.call('TIM6_IRQHandler')
    assert s['requests'] == 1
    assert f.get('gEscIrqDiag', 'lowAfterServiceCount') == 0xFFFFFFFF
    assert f.get('gEscIrqDiag', 'recoveryRequestCount') == 0xFFFFFFFF
test('ARM IRQ diagnostics saturate without disabling subsequent recovery requests',
     irq_diagnostics_saturate)


def irq_duration_diagnostic(f):
    s = irq_port_fixture(f)
    f.uc.mem_write(0xE0001000, struct.pack('<II', 1, 0xFFFFFFE0))
    f.hook('PDI_Isr',
           lambda *_: f.uc.mem_write(0xE0001004, struct.pack('<I', 0x20)))
    f.call('EXTI0_IRQHandler')
    assert f.get('gEscIrqDiag', 'maxServiceCycles') == 0x40
    f.uc.mem_write(0xE0001000, struct.pack('<II', 0, 0))
    f.hook('PDI_Isr',
           lambda *_: f.uc.mem_write(0xE0001004, struct.pack('<I', 1000)))
    f.call('EXTI0_IRQHandler')
    assert f.get('gEscIrqDiag', 'maxServiceCycles') == 0x40
    f.uc.mem_write(0xE0001000, struct.pack('<II', 1, 0))
    f.hook('PDI_Isr',
           lambda *_: f.uc.mem_write(0xE0001000, struct.pack('<II', 0, 2000)))
    f.call('EXTI0_IRQHandler')
    assert f.get('gEscIrqDiag', 'maxServiceCycles') == 0x40
test('ARM IRQ timing handles DWT wrap and ignores disabled or stopped counters',
     irq_duration_diagnostic)


def irq_priority_profile(f):
    # Execute the real ARM initializers and NVIC writes, not a source replay.
    # Peripheral registers are memory only: this does NOT emulate preemption.
    f.uc.mem_map(0x40000000, 0x40000)
    f.uc.mem_write(0xE000E400, b'\xFF' * 128)
    f.hook('RCC_GetClocksFreqValue',
           lambda ptr, *_: f.uc.mem_write(ptr, struct.pack('<I', 144000000)))
    for name in ('exti_init_irq', 'exti_init_sync0', 'exti_init_sync1',
                 'timer_pwm_adc_init'):
        f.call(name)
    f.call('timer_init', 5)
    f.call('dma_config')
    # IRQ numbers and four implemented priority bits: n32g45x.h.
    expected = {6: 0x00, 7: 0x40, 8: 0xFF,
                44: 0x80, 54: 0x80, 56: 0x90}
    for irq, priority in expected.items():
        actual = f.uc.mem_read(0xE000E400 + irq, 1)[0]
        assert actual == priority, (irq, actual, priority)
    aircr = int.from_bytes(f.uc.mem_read(0xE000ED0C, 4), 'little')
    assert aircr & 0x700 == 0x500  # Group 2: two preempt/two subpriority bits.
    assert [irq for irq, priority in expected.items()
            if priority >> 6 == 0] == [6]
    assert expected[6] >> 6 < expected[7] >> 6 < expected[44] >> 6
test('ARM priorities: ESC IRQ above SYNC0 above motor; SYNC1 unconfigured',
     irq_priority_profile)


def unwired_sync1_stays_masked(f):
    f.uc.mem_map(0x40000000, 0x40000)
    # Seed a formerly enabled line, then verify disable is repeatable.
    exti = 0x40010400
    for offset in (0, 4, 8, 12):
        f.uc.mem_write(exti + offset, struct.pack('<I', 7))
    writes = []
    def record(uc, access, address, size, value, _):
        writes.append((address, value))
    f.uc.hook_add(UC_HOOK_MEM_WRITE, record,
                  begin=0xE000E100, end=0xE000E2FF)
    f.call('exti_init_sync1')
    for offset in (0, 4, 8, 12):
        value = int.from_bytes(f.uc.mem_read(exti + offset, 4), 'little')
        assert value == 3, (offset, value)  # Other EXTI lines preserved.
    assert (0xE000E180, 1 << 8) in writes  # NVIC disable request.
    assert (0xE000E280, 1 << 8) in writes  # NVIC pending clear request.

    # Run real HW_Init with ESC register responses and pin/reset work mocked.
    registers = {0x3064: 0x87654321, 0x3050: 0x13530000}
    def esc_read(ptr, address, size, _):
        f.uc.mem_write(ptr, registers.get(address, 0).to_bytes(size, 'little'))
        return 1
    def esc_write(ptr, address, size, _):
        registers[address] = int.from_bytes(f.uc.mem_read(ptr, size), 'little')
        return 1
    for name in ('GPIO_Config', 'FCM4E1553_reset', 'delay_ms',
                 'QSPI_GPIO', 'qspi_init'):
        if name in f.symbols:
            f.hook(name, lambda *_: 0)
    f.hook('HW_EscRead', esc_read)
    f.hook('HW_EscWrite', esc_write)
    assert f.call('HW_Init') == 0
    assert f.read('g_esc_pdi_init_stage', 1) == 0x80
    assert any(address == 0xE000E100 and value & (1 << 6)
               for address, value in writes)  # ESC IRQ still enabled.
    assert any(address == 0xE000E100 and value & (1 << 7)
               for address, value in writes)  # SYNC0 still enabled.
    assert not any(address == 0xE000E100 and value & (1 << 8)
                   for address, value in writes)  # No SYNC1 re-enable.
    assert int.from_bytes(f.uc.mem_read(exti, 4), 'little') & 7 == 3
    calls = []
    f.hook('Sync1_Isr', lambda *_: calls.append(1))
    f.call('EXTI2_IRQHandler')
    assert calls == []  # Even an unexpected vector entry cannot dispatch SYNC1.
test('unwired SYNC1 masked and cleared; HW_Init cannot re-enable it',
     unwired_sync1_stays_masked)


def main_keeps_motor_below_irq(f):
    # Boot flow is executed offline with hardware/protocol work replaced.
    # In particular pwm_config is stubbed to isolate the second TIM8 setup.
    for name in ('systick_config', 'rcu_config', 'SystemCoreClockUpdate',
                 'HW_Init', 'GpioIni', 'com_gpio_init', 'com_usart_init',
                 'eeprom_init', 'eeprom_test', 'uart_write',
                 'eeprom_console_init', 'modbus_init', 'dma_config',
                 'adc_config', 'DataIni', 'shuzu_initial', 'IniSaveFunc',
                 'MainInit', 'CiA402_Init', 'EthercatMotorControl_Init',
                 'APPL_GenerateMapping', 'pwm_config', 'UpPwm',
                 'EthercatMotorControl_Update', 'modbus_process',
                 'eeprom_console_process', 'CiA402_DeallocateAxis',
                 'HW_Release'):
        if name in f.symbols:
            f.hook(name, lambda *_: 0)
    loops = []
    def stop_loop(*_):
        loops.append(1)
        f.write('bRunApplication', 0, 1)
    f.hook('MainLoop', stop_loop)
    f.call('NVIC_PriorityGroupConfig', 0x500)
    f.uc.mem_write(0xE000E400 + 44, b'\x00')  # Old motor priority.
    f.call('main')
    assert loops == [1]
    assert f.uc.mem_read(0xE000E400 + 44, 1)[0] == 0x80
    assert f.uc.mem_read(0xE000E400 + 6, 1)[0] == 0x00
test('main boot configuration cannot restore motor priority above ESC IRQ',
     main_keeps_motor_below_irq)

print(str(passed) + ' ARM firmware fault-injection/configuration tests passed (mocked hardware).')
