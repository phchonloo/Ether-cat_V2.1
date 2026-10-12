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
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
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

print(str(passed) + ' ARM firmware fault-injection tests passed (mocked hardware).')
