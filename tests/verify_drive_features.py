"""Execute common-feature paths in the actual Keil ARM image, without hardware.

Not a shaft-position/current/real-time or EtherCAT conformance certification.
"""
from pathlib import Path

# Reuse the existing image/MMIO/DWARF fixture, without executing its test suite.
exec(compile(Path(__file__).with_name('verify_pdi_faults.py').read_text('utf8')
             .split('\npassed = 0')[0], 'feature_fixture', 'exec'))

passed = 0


def obj(f, name, value=None):
    if value is not None:
        f.put('LocalAxes', value, 'Objects', name)
    return f.get('LocalAxes', 'Objects', name)


def update(f):
    f.call('EthercatMotorControl_OnSyncCycle')
    f.call('EthercatMotorControl_Update')


def reset(f, mode=3):
    assert f.call('COE_ObjDictionaryInit') == 0
    assert f.call('CiA402_Init') == 0
    f.call('EthercatMotorControl_Init')
    assert f.call('APPL_GenerateMapping', f.data + 16, f.data + 20) == 0
    assert int.from_bytes(f.uc.mem_read(f.data + 16, 2), 'little') == 12
    assert int.from_bytes(f.uc.mem_read(f.data + 20, 2), 'little') == 12
    f.write('nAlStatus', 8, 1)
    f.put('LocalAxes', 1, 'bAxisIsActive')
    f.put('LocalAxes', 0x10, 'i16State')
    obj(f, 'objModesOfOperation', mode)
    obj(f, 'objControlWord', 15)
    obj(f, 'objProfileAcceleration', 10000)
    f.put('LocalAxes', 2000, 'u32CycleTime')
    update(f)
    update(f)
    assert f.read('gEcatMotorEnabled', 1) == 1


def tick(f):
    f.call('EthercatMotorControl_BeginPhaseCycle', f.read('Iangle'))
    f.call('PulseFunction')
    step = f.read('BaseRpm', signed=True)
    f.call('PhaseGet', step)
    f.call('EthercatMotorControl_EndPhaseCycle', f.read('Iangle'))
    return step


def run(f, count, states=False):
    steps = []
    for n in range(count):
        steps.append(tick(f))
        if n % 32 == 31:
            if states:
                f.call('CiA402_Application', f.symbols['LocalAxes'])
                f.call('CiA402_StateMachine')
            update(f)
    return steps


def moving(f, mode=3, speed=60):
    reset(f, mode)
    obj(f, 'objTargetVelocity', speed)
    update(f)
    run(f, 160)
    assert f.get('gEcatPulseDiag', 'lastPhaseStep') == (speed * 16) & 0xFFFFFFFF


def sdo(f, index, value, size=4, sub=0, ca=0):
    f.uc.mem_write(f.data, (value & 0xFFFFFFFF).to_bytes(4, 'little'))
    # The fifth AAPCS argument is on the stack at the call entry.
    f.uc.mem_write(0x2003F000, int(ca).to_bytes(4, 'little'))
    result = f.call('WriteMotionParameter', index, sub, size, f.data)
    f.uc.mem_write(0x2003F000, bytes(4))
    return result


def test(name, fn):
    global passed
    f = Fixture()
    try:
        fn(f)
        passed += 1
        print('PASS ' + name, flush=True)
    finally:
        f.close()


def pv_halt(f):
    moving(f)
    obj(f, 'objProfileDeceleration', 600)
    obj(f, 'objControlWord', 0x10F)
    update(f)
    assert not (obj(f, 'objStatusWord') & 0x400)
    steps = run(f, 1664)
    assert steps[0] > 0 and steps[-1] == 0
    assert all(a >= b >= 0 for a, b in zip(steps, steps[1:]))
    assert obj(f, 'objStatusWord') & 0x1400 == 0x1400
    pos = obj(f, 'objPositionActualValue')
    run(f, 320)
    assert obj(f, 'objPositionActualValue') == pos
    obj(f, 'objControlWord', 15)
    update(f)
    assert run(f, 160)[-1] == 960
test('PV Halt ramps to zero, reports speed zero, holds, and explicitly resumes', pv_halt)


def quick(f, option):
    moving(f)
    obj(f, 'objQuickStopOptionCode', option)
    obj(f, 'objQuickStopDeclaration', 600)
    obj(f, 'objControlWord', 2)
    f.call('CiA402_StateMachine')
    assert f.get('LocalAxes', 'u16PendingOptionCode') == 0x605A
    assert obj(f, 'objStatusWord') & 0x6F == 7
    before = obj(f, 'objVelocityActualValue')
    f.call('CiA402_Application', f.symbols['LocalAxes'])
    assert obj(f, 'objVelocityActualValue') == before == 60
    assert f.get('LocalAxes', 'u16PendingOptionCode') == 0x605A
    update(f)
    steps = run(f, 1696, states=True)
    assert steps[0] > 0 and steps[-1] == 0
    assert all(a >= b >= 0 for a, b in zip(steps, steps[1:]))
    assert f.get('LocalAxes', 'u16PendingOptionCode') == 0
    assert f.get('LocalAxes', 'i16State') == (2 if option == 2 else 0x20)
    if option == 6:
        obj(f, 'objControlWord', 15)
        run(f, 320, states=True)
        assert f.get('LocalAxes', 'i16State') == 0x20
        assert f.get('gEcatPulseDiag', 'lastPhaseStep') == 0
test('Quick stop 2 waits for emitted motion to stop then disables', lambda f: quick(f, 2))
test('Quick stop 6 holds; old enable alone never restarts', lambda f: quick(f, 6))


def stop_override(f, cause):
    moving(f)
    obj(f, 'objControlWord', 2)
    f.call('CiA402_StateMachine')
    update(f)
    assert tick(f) > 0
    if cause == 'voltage':
        obj(f, 'objControlWord', 0)
        # Even a stalled foreground must not defeat voltage removal.
        assert tick(f) == 0
        f.call('CiA402_StateMachine')
        assert f.get('LocalAxes', 'u16PendingOptionCode') == 0
    elif cause == 'pdi':
        f.put('gEscPdiDiag', 1, 'faultLatched')
        assert tick(f) == 0
    else:
        f.write('nAlStatus', 4, 1)
        assert tick(f) == 0
    assert f.call('EthercatMotorControl_IsStopped') == 1
for cause in ('voltage', 'pdi', 'al'):
    test('Pending ramp cannot mask ' + cause + ' stop', lambda f, c=cause: stop_override(f, c))


def disable_operation(f):
    moving(f)
    obj(f, 'objProfileDeceleration', 600)
    obj(f, 'objControlWord', 7)
    f.call('CiA402_StateMachine')
    assert f.get('LocalAxes', 'u16PendingOptionCode') == 0x605C
    update(f)
    assert run(f, 1696, states=True)[-1] == 0
    assert f.get('LocalAxes', 'i16State') == 8
    assert f.read('gEcatMotorEnabled', 1) == 0
test('Disable operation decelerates then enters Switched On', disable_operation)


def quick_zero(f):
    moving(f)
    obj(f, 'objQuickStopOptionCode', 0)
    obj(f, 'objControlWord', 2)
    f.call('CiA402_StateMachine')
    update(f)
    assert tick(f) == 0 and f.get('LocalAxes', 'i16State') == 2
test('Quick stop option 0 disables without waiting for a ramp', quick_zero)


def parameter_checks(f):
    reset(f)
    for idx in (0x607F, 0x6081, 0x6083, 0x6084, 0x6085):
        assert sdo(f, idx, 0) == 0x12
        assert sdo(f, idx, 0xFFFFFFFF) == 0x12
        assert sdo(f, idx, 100) == 0
        assert sdo(f, idx, 100, sub=1) == 0x11
        assert sdo(f, idx, 100, size=2) == 0x0E
        assert sdo(f, idx, 100, ca=1) == 0x1D
    for idx, allowed in ((0x605A, (0,1,2,5,6)), (0x605B, (0,1)),
                         (0x605C, (0,1)), (0x605D, (1,2)), (0x605E, (0,1,2))):
        for val in range(9):
            assert sdo(f, idx, val, 2) == (0 if val in allowed else 0x12)
    f.put('LocalAxes', 0x605A, 'u16PendingOptionCode')
    assert sdo(f, 0x6085, 200) == 0x1A
test('SDO ranges, lengths, subindices, CA, stop capabilities and pending-write guard', parameter_checks)


def units(f):
    reset(f)
    assert sdo(f, 0x2000, 1, 2) == 0x1A
    obj(f, 'objControlWord', 0)
    f.put('LocalAxes', 2, 'i16State')
    update(f)
    tick(f)
    f.write('nAlStatus', 2, 1)
    assert sdo(f, 0x2000, 2, 2) == 0x12
    assert sdo(f, 0x2000, 1, 2) == 0
    assert obj(f, 'objProfileVelocity') == 1666
    assert obj(f, 'objProfileAcceleration') == 5000
    assert obj(f, 'objTargetVelocity') == 0
    assert sdo(f, 0x6083, 166666) == 0
    assert sdo(f, 0x6081, 1) == 0x12  # below the PP planner's 1 rpm resolution
    f.write('nAlStatus', 8, 1)
    f.put('LocalAxes', 0x10, 'i16State')
    obj(f, 'objControlWord', 15)
    obj(f, 'objTargetVelocity', 1000)  # 1000 counts/s at 1000 counts/rev = 60 rpm
    update(f)
    assert run(f, 160)[-1] == 960
    assert obj(f, 'objVelocityActualValue') == 1000
    obj(f, 'objTargetVelocity', -1000)
    update(f)
    assert run(f, 4000)[-1] == -960
    assert obj(f, 'objVelocityActualValue') == (1 << 32) - 1000
test('Optional count/s units are PREOP-only, reset dependent values, and convert signed velocity', units)


def offset(f):
    reset(f, 8)
    obj(f, 'objTargetPosition', 100)
    obj(f, 'objPositionOffset', 20)
    update(f)
    tick(f)
    assert f.get('gEcatPulseDiag', 'targetPosition') == 120
    obj(f, 'objTargetPosition', 0x7FFFFFFF)
    update(f)
    tick(f)
    assert f.get('gEcatPulseDiag', 'targetPosition') == 0x7FFFFFFF
    assert obj(f, 'objStatusWord') & 0x800
test('CSP offset uses a widened sum and saturates without sign wrap', offset)


def max_speed(f):
    reset(f)
    obj(f, 'objMaxProfileVelocity', 30)
    obj(f, 'objTargetVelocity', 0x80000000)
    update(f)
    assert run(f, 128)[-1] == -480
    assert obj(f, 'objStatusWord') & 0x800
test('Maximum velocity clamps negative/INT32_MIN targets safely', max_speed)


def halt_ramp_choice(f):
    moving(f)
    obj(f, 'objProfileDeceleration', 30)
    obj(f, 'objQuickStopDeclaration', 600)
    obj(f, 'objHaltOptionCode', 2)
    obj(f, 'objControlWord', 0x10F)
    update(f)
    assert run(f, 1632)[-1] == 0
test('Halt option 2 uses 6085 rather than the slower profile deceleration', halt_ramp_choice)


def pp_halt(f):
    reset(f, 1)
    obj(f, 'objTargetPosition', 1000)
    obj(f, 'objControlWord', 0x2F)
    update(f)
    obj(f, 'objControlWord', 0x3F)
    update(f)
    run(f, 320)
    assert obj(f, 'objStatusWord') & 0x1000
    obj(f, 'objControlWord', 0x12F)
    obj(f, 'objProfileDeceleration', 1000)
    update(f)
    run(f, 1664)
    assert obj(f, 'objStatusWord') & 0x400
    pos = obj(f, 'objPositionActualValue')
    assert 0 < pos < 1000
    run(f, 128)
    assert obj(f, 'objPositionActualValue') == pos
    obj(f, 'objControlWord', 0x2F)
    update(f)
    assert run(f, 160)[-1] > 0
test('PP Halt stops short, reports zero motion and resumes the retained endpoint', pp_halt)


def normal_enable(f):
    reset(f)
    f.put('LocalAxes', 2, 'i16State')
    obj(f, 'objControlWord', 0)
    for _ in range(3):
        f.call('CiA402_StateMachine')
    assert f.get('LocalAxes', 'i16State') == 2
    assert obj(f, 'objStatusWord') & 0x6F == 0x40
    for command, state in ((6, 4), (7, 8), (15, 0x10)):
        obj(f, 'objControlWord', command)
        f.call('CiA402_StateMachine')
        assert f.get('LocalAxes', 'i16State') == state
test('OP alone does not advance disabled state; 6/7/15 sequence still works', normal_enable)


def hardware_fault(f):
    moving(f)
    f.put('ErrNumFlag', 10)
    # Keep the real TIM8 ISR/phase accounting, mock peripheral status only.
    f.hook('TIM_GetIntStatus', lambda *_: 1)
    f.hook('TIM_ClrIntPendingBit', lambda *_: 0)
    f.hook('ErrorHandle', lambda *_: 0)
    f.call('TIM8_UP_IRQHandler')
    assert f.call('EthercatMotorControl_IsStopped') == 1
    update(f)
    assert f.get('LocalAxes', 'i16State') == 0x40
    f.call('CiA402_StateMachine')
    f.call('CiA402_Application', f.symbols['LocalAxes'])
    assert f.get('LocalAxes', 'i16State') == 0x80
    assert obj(f, 'objVelocityActualValue') == 0
test('Hardware fault retires the ISR ramp; fault reaction cannot deadlock', hardware_fault)


def fault_overrides_pending(f):
    moving(f)
    obj(f, 'objControlWord', 2)
    f.call('CiA402_StateMachine')
    assert f.get('LocalAxes', 'u16PendingOptionCode') == 0x605A
    f.call('CiA402_LocalError', 0x1000)
    assert f.get('LocalAxes', 'u16PendingOptionCode') == 0
    f.call('CiA402_Application', f.symbols['LocalAxes'])
    assert f.get('LocalAxes', 'i16State') == 0x40
    assert tick(f) == 0
    f.call('CiA402_StateMachine')
    f.call('CiA402_Application', f.symbols['LocalAxes'])
    f.call('CiA402_StateMachine')
    assert f.get('LocalAxes', 'i16State') == 0x80
    assert obj(f, 'objStatusWord') & 0x6F == 8
test('New fault cancels an older pending stop before an application IRQ can overwrite it', fault_overrides_pending)


def reset_edge(f):
    reset(f)
    calls = []
    f.hook('ErrorHandle_TryReset', lambda *_: calls.append(1) or 1)
    f.hook('ECAT_TryResetPdiFault', lambda *_: 1)
    # Held reset before a new fault is not a new reset command.
    obj(f, 'objControlWord', 0x8F)
    f.call('CiA402_StateMachine')
    f.put('LocalAxes', 0x80, 'i16State')
    f.call('CiA402_StateMachine')
    assert calls == [] and f.get('LocalAxes', 'i16State') == 0x80
    obj(f, 'objControlWord', 0)
    f.call('CiA402_StateMachine')
    obj(f, 'objControlWord', 0x80)
    f.call('CiA402_StateMachine')
    assert calls == [1] and f.get('LocalAxes', 'i16State') == 2
    assert f.read('gEcatMotorEnabled', 1) == 1  # foreground has not run yet
    update(f)
    assert f.read('gEcatMotorEnabled', 1) == 0
test('Fault reset requires a fresh bit-7 edge and cannot enable motion', reset_edge)


def pwm_gate(f):
    moving(f)
    writes = []
    for channel in range(1, 5):
        f.hook('TIM_SetCmp' + str(channel),
               lambda timer, value, *_: writes.append(value))
    def check(expected):
        writes.clear()
        f.call('UpPwm', 100, 200, 300, 400)
        assert writes == expected, writes
    check([100, 200, 300, 400])
    obj(f, 'objControlWord', 0x10F)
    check([100, 200, 300, 400])  # Halt retains holding-current capability
    obj(f, 'objControlWord', 0)
    check([0, 0, 0, 0])  # raw voltage removal, no foreground required
    obj(f, 'objControlWord', 15)
    f.put('LocalAxes', 2, 'i16State')
    check([0, 0, 0, 0])
    f.put('LocalAxes', 0x20, 'i16State')
    check([100, 200, 300, 400])  # Quick Stop ramp/hold can remain powered
    f.put('gEscPdiDiag', 1, 'faultLatched')
    check([0, 0, 0, 0])
    f.put('gEscPdiDiag', 0, 'faultLatched')
    f.put('ErrNumFlag', 10)
    check([0, 0, 0, 0])
    f.put('ErrNumFlag', 0)
    f.write('nAlStatus', 4, 1)
    check([0, 0, 0, 0])
test('Final PWM boundary rejects disabled/non-OP/fault writes; Halt/Quick Stop can retain power', pwm_gate)


def dictionary(f):
    reset(f)
    for idx, member in ((0x2000, 'objMotionUnits'), (0x605D, 'objHaltOptionCode'),
                        (0x607F, 'objMaxProfileVelocity'), (0x60B0, 'objPositionOffset')):
        assert f.call('OBJ_GetObjectHandle', idx) != 0
        assert f.loc('LocalAxes', ('Objects', member))[1] in (2, 4)
    for idx in (0x20F1, 0x6098, 0x60B8):
        assert f.call('OBJ_GetObjectHandle', idx) == 0
    assert obj(f, 'objSupportedDriveModes') == 0x185
    f.put('sRxPDOassign', 0x1602, 'aEntries')
    assert f.call('APPL_GenerateMapping', f.data + 16, f.data + 20) == 0
    assert obj(f, 'objSupportedDriveModes') & 0x20 == 0
test('New objects are registered; unwired/current/homing features are not advertised', dictionary)

print(f'{passed} ARM common-feature tests passed (software only).', flush=True)
