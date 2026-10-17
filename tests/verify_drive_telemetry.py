"""Run telemetry and CoE/PDO boundary tests on the compiled ARM image.

Synthetic ADC/gain values are test stimuli, NOT MT9220 calibration evidence.
No physical motor, analog accuracy, timing or EtherCAT certification is implied.
"""
from pathlib import Path
import math
import struct
import xml.etree.ElementTree as ET

exec(compile(Path(__file__).with_name('verify_pdi_faults.py').read_text('utf8')
             .split('\npassed = 0')[0], 'telemetry_fixture', 'exec'))

CLOCK = 144000000
DWT = 0xE0001004
passed = 0


def now(f, ticks):
    f.uc.mem_write(DWT, struct.pack('<I', ticks & 0xffffffff))


def reset(f):
    f.write('SystemCoreClock', CLOCK)
    assert f.call('COE_ObjDictionaryInit') == 0
    assert f.call('CiA402_Init') == 0
    f.call('EthercatMotorControl_Init')
    f.call('DriveTelemetry_Init')
    f.write('nAlStatus', 2, 1)
    assert f.call('APPL_GenerateMapping', f.data + 128, f.data + 132) == 0
    assert bytes(f.uc.mem_read(f.data + 128, 2)) == b'\x0c\x00'
    assert bytes(f.uc.mem_read(f.data + 132, 2)) == b'\x0c\x00'
    f.hook('DMA_GetCurrDataCounter', lambda *_: 10)


def value(f, name):
    return f.get('gDriveTelemetry', name)


def frame(f, cycle=144000, a=2068, b=2068, bus=584):
    now(f, cycle)
    f.uc.mem_write(f.symbols['adc_value'], struct.pack('<10I', a, b, bus, 0, a, b, a, b, a, b))
    f.call('DriveTelemetry_AdcComplete')
    f.call('DriveTelemetry_Update')


def sdo(f, index, val=0, size=4, sub=0, ca=0, write=False):
    handle = f.call('OBJ_GetObjectHandle', index)
    assert handle, hex(index)
    f.uc.mem_write(f.data, struct.pack('<I', val & 0xffffffff))
    # OBJ_Read/Write use the real stack access checks before the callback.
    f.uc.mem_write(0x2003F000, struct.pack('<II', f.data, ca))
    result = f.call('OBJ_Write' if write else 'OBJ_Read', index, sub, size, handle)
    f.uc.mem_write(0x2003F000, bytes(8))
    return result, int.from_bytes(f.uc.mem_read(f.data, size), 'little')


def configure(f):
    for index, val in ((0x2011, 200000), (0x2012, 1666667), (0x2013, 1666667), (0x6075, 2000)):
        assert sdo(f, index, val, write=True)[0] == 0


def test(name, fn):
    global passed
    f = Fixture()
    try:
        reset(f)
        fn(f)
        passed += 1
        print('PASS ' + name, flush=True)
    finally:
        f.close()


def startup(f):
    assert value(f, 'status') & 0x1907 == 0x1900
    assert value(f, 'busVoltageMv') == 0xffffffff
    assert sdo(f, 0x6079)[0] == 0x18
    assert sdo(f, 0x6077, size=2)[0] == 0x0d
    assert sdo(f, 0x2002)[0] == 0x0d
    assert sdo(f, 0x206E, size=2) == (0, 0x0102)
test('startup never advertises fabricated current, voltage, torque or bus current', startup)


def voltage(f):
    frame(f)
    expected = (584 * 3300 * 51 + 2047) // 4095
    assert value(f, 'status') & 7 == 1
    assert sdo(f, 0x6079) == (0, expected)
    assert sdo(f, 0x2065, size=2) == (0, (expected + 50) // 100)
    assert sdo(f, 0x6078, size=2)[0] == 0x18
    assert value(f, 'rawA') == 2068 and value(f, 'rawBus') == 584
test('PE12 51:1 voltage scaling works without guessing current sensitivity', voltage)


def currents(f):
    configure(f)
    frame(f, a=2400, b=1800)
    def phase(n):
        # C integer division truncates toward zero.
        return math.trunc(((n * 3300 * 1000 // 4095) - 1666667) * 3000 / 400000)
    a, b = phase(2400), phase(1800)
    magnitude = math.isqrt(a * a + b * b)
    assert value(f, 'phaseAMa') == a & 0xffffffff
    assert value(f, 'phaseBMa') == b & 0xffffffff
    assert sdo(f, 0x2064, size=2) == (0, magnitude)
    assert sdo(f, 0x6078, size=2) == (0, magnitude * 1000 // 2000)
    assert value(f, 'status') & 0x1f == 7
test('synthetic calibrated signed phases, vector magnitude and rated-current scaling', currents)


def guards(f):
    before = f.get('gDriveTelemetryConfig', 'sensitivityUvA')
    assert sdo(f, 0x2011, 9999, write=True)[0] == 0x12
    assert sdo(f, 0x2010, 3601, write=True)[0] == 0x12
    assert sdo(f, 0x2012, 0, write=True)[0] == 0x12
    assert sdo(f, 0x6075, 10001, write=True)[0] == 0x12
    assert sdo(f, 0x2011, 200000, size=2, write=True)[0] != 0
    assert sdo(f, 0x2011, 200000, sub=1, write=True)[0] != 0
    assert sdo(f, 0x2011, 200000, ca=1, write=True)[0] != 0
    assert sdo(f, 0x6079, 1, write=True)[0] == 7
    assert f.get('gDriveTelemetryConfig', 'sensitivityUvA') == before
    f.write('nAlStatus', 8, 1)
    assert sdo(f, 0x2011, 200000, write=True)[0] != 0
    f.write('nAlStatus', 2, 1)
    f.write('gEcatMotorEnabled', 1, 1)
    assert sdo(f, 0x2011, 200000, write=True)[0] == 0x1a
test('real SDO access checks reject RO writes, malformed requests and unsafe calibration', guards)


def calibration_invalidation(f):
    configure(f)
    frame(f)
    assert value(f, 'status') & 7 == 7
    assert sdo(f, 0x2011, 0, write=True)[0] == 0
    assert sdo(f, 0x6078, size=2)[0] == 0x18
    frame(f, cycle=288000)
    assert value(f, 'status') & 7 == 1
test('configuration changes invalidate old converted values immediately', calibration_invalidation)


def stale(f):
    frame(f)
    now(f, 144000 + CLOCK // 200)
    f.call('DriveTelemetry_Latch')
    assert value(f, 'status') & 0x101 == 0x100
    assert sdo(f, 0x6079)[0] == 0x18
    # Simulate DWT's 32-bit wrap to the apparent age of an old frame.
    now(f, 145000)
    f.call('DriveTelemetry_Update')
    assert sdo(f, 0x6079)[0] == 0x18
    frame(f, cycle=400000)
    assert sdo(f, 0x6079)[0] == 0
test('5 ms expiration stays invalid across DWT wrap and recovers only on a new frame', stale)


def dma_fault(f):
    frame(f)
    f.call('DriveTelemetry_AdcError')
    f.call('DriveTelemetry_Latch')
    assert value(f, 'status') & 0x301 == 0x300
    frame(f, cycle=288000)
    assert value(f, 'status') & 0x301 == 1
test('DMA error invalidation and fresh-frame recovery', dma_fault)


def rails(f):
    configure(f)
    frame(f, a=0, bus=584)
    assert value(f, 'status') & 0x407 == 0x401
    frame(f, cycle=288000, a=2068, bus=4095)
    assert value(f, 'status') & 0x407 == 0x406
    frame(f, cycle=432000, bus=0)
    assert sdo(f, 0x6079) == (0, 0)  # a discharged bus may really be 0 V
test('ADC rails invalidate only affected measurements; a zero-volt bus is valid', rails)


def late_dma(f):
    frame(f)
    f.hook('DMA_GetCurrDataCounter', lambda *_: 9)
    frame(f, cycle=288000)
    assert value(f, 'sampleCount') == 1
    counts = iter([10, 9])
    f.hook('DMA_GetCurrDataCounter', lambda *_: next(counts))
    frame(f, cycle=432000)
    assert value(f, 'sampleCount') == 1
    calls = [0]
    def slow(*_):
        calls[0] += 1
        if calls[0] == 2:
            now(f, 576000 + CLOCK // 32000)
        return 10
    f.hook('DMA_GetCurrDataCounter', slow)
    frame(f, cycle=576000)
    assert value(f, 'sampleCount') == 1
test('late DMA, changing buffer and preempted copies are discarded without spinning', late_dma)


def optional_pdo(f):
    configure(f)
    frame(f, a=2400, b=1800)
    f.write('sTxPDOassign', 0x1a03, 2, offset=2)
    assert f.call('APPL_GenerateMapping', f.data + 128, f.data + 132) == 0
    assert int.from_bytes(f.uc.mem_read(f.data + 128, 2), 'little') == 42
    assert int.from_bytes(f.uc.mem_read(f.data + 132, 2), 'little') == 12
    f.uc.mem_write(f.data, bytes([0xa5]) * 64)
    f.call('APPL_InputMapping', f.data + 4)
    pdo = bytes(f.uc.mem_read(f.data + 4, 42))
    assert bytes(f.uc.mem_read(f.data, 4)) == bytes([0xa5]) * 4
    assert bytes(f.uc.mem_read(f.data + 46, 4)) == bytes([0xa5]) * 4
    assert struct.unpack_from('<H', pdo, 12)[0] & 0x1f == 7
    assert struct.unpack_from('<h', pdo, 14)[0] == value(f, 'currentPermille')
    assert struct.unpack_from('<I', pdo, 16)[0] == value(f, 'busVoltageMv')
    assert struct.unpack_from('<I', pdo, 20)[0] == value(f, 'phaseAMa')
    assert struct.unpack_from('<I', pdo, 24)[0] == value(f, 'phaseBMa')
    assert struct.unpack_from('<i', pdo, 28)[0] == -2147483648
    assert struct.unpack_from('<h', pdo, 32)[0] == -32768
    assert struct.unpack_from('<I', pdo, 38)[0] == value(f, 'sampleCount')
    assert sdo(f, 0x1a03, 0, size=1, write=True)[0] == 7
test('optional 42-byte TxPDO, exact offsets, sentinels, RO mapping and guard bytes', optional_pdo)


def expired_pdo(f):
    optional_pdo(f)
    now(f, 144000 + CLOCK // 200)
    f.call('APPL_InputMapping', f.data)
    pdo = bytes(f.uc.mem_read(f.data, 42))
    assert struct.unpack_from('<H', pdo, 12)[0] & 0x107 == 0x100
    assert struct.unpack_from('<I', pdo, 16)[0] == 0xffffffff
    assert struct.unpack_from('<i', pdo, 20)[0] == -2147483648
test('PDO expires old measurements even when foreground has not run', expired_pdo)


def coherent_pdo(f):
    optional_pdo(f)
    expected = bytes(f.uc.mem_read(f.data + 4, 42))
    changed = []
    def interrupt_reader(uc, access, address, size, val, _):
        if not changed:
            changed.append(True)
            # Simulate another reader/watch publisher modifying the public
            # display structure between individual PDO fields.
            uc.mem_write(f.symbols['gDriveTelemetry'],
                         bytes([0x5a]) * f.loc('gDriveTelemetry', ())[1])
    hook = f.uc.hook_add(UC_HOOK_MEM_WRITE, interrupt_reader,
                        begin=f.data, end=f.data + 41)
    f.call('APPL_InputMapping', f.data)
    f.uc.hook_del(hook)
    assert changed and bytes(f.uc.mem_read(f.data, 42)) == expected
test('PDO retains one private telemetry snapshot between all mapped fields', coherent_pdo)


def invalid_map(f):
    f.put('LocalAxes', 6, 'Objects', 'sTxPDOMap0', 'u16SubIndex0')
    assert f.call('APPL_GenerateMapping', f.data, f.data + 4) != 0
test('mapping count cannot overrun its object storage', invalid_map)


def following(f):
    frame(f)
    assert sdo(f, 0x60f4)[0] == 0x18  # CSV has no position tracking error
    f.put('LocalAxes', 8, 'Objects', 'objModesOfOperationDisplay')
    f.put('gEcatPulseDiag', 2147483647, 'targetPosition')
    f.put('gEcatPulseDiag', 0x80000000, 'countedPosition')
    frame(f, cycle=288000)
    assert sdo(f, 0x60f4) == (0, 2147483647)
    f.put('gEcatPulseDiag', 0x80000000, 'targetPosition')
    f.put('gEcatPulseDiag', 2147483647, 'countedPosition')
    frame(f, cycle=432000)
    assert sdo(f, 0x60f4) == (0, 0x80000001)
test('internal command error saturates without signed overflow; not encoder feedback', following)

def xml_contract(f):
    root = ET.parse(ROOT / 'FCM4E1553_CiA402_SingleAxis_Rev03.xml')
    objects = {int(o.findtext('Index').replace('#x',''),16):o
               for o in root.findall('.//Dictionary/Objects/Object')}
    new_indices = list(range(0x2001,0x2009)) + list(range(0x2010,0x2014)) + [
        0x2064,0x2065,0x206d,0x206e,0x206f,0x6075,0x6078,0x6079,0x60f4]
    type_codes = {'INT':3, 'DINT':4, 'UINT':6, 'UDINT':7}
    dwarf = f.elf.get_dwarf_info()
    var = next(d for cu in dwarf.iter_CUs() for d in cu.iter_DIEs()
               if d.tag == 'DW_TAG_variable' and d.attributes.get('DW_AT_name')
               and d.attributes['DW_AT_name'].value == b'ApplicationObjDic')
    typ = unwrap_type(var.get_DIE_from_attribute('DW_AT_type'))
    member = next(d for d in typ.iter_children() if d.tag == 'DW_TAG_member'
                  and d.attributes['DW_AT_name'].value == b'pEntryDesc')
    desc_off = member.attributes['DW_AT_data_member_location'].value
    if not isinstance(desc_off, int):
        from elftools.dwarf.dwarf_expr import DWARFExprParser
        ops = DWARFExprParser(dwarf.structs).parse_expr(desc_off)
        assert len(ops) == 1 and ops[0].op_name == 'DW_OP_plus_uconst'
        desc_off = ops[0].args[0]
    for index in new_indices:
        o = objects[index]
        handle = f.call('OBJ_GetObjectHandle', index)
        assert handle, hex(index)
        desc = int.from_bytes(f.uc.mem_read(handle + desc_off, 4), 'little')
        typ, bits, access = struct.unpack('<HHH', f.uc.mem_read(desc, 6))
        assert typ == type_codes[o.findtext('Type')], hex(index)
        assert bits == int(o.findtext('BitSize')), hex(index)
        assert access & 7 == 7
        if o.findtext('Flags/Access') == 'rw':
            assert access & 0x38 == 8
            expected = int.from_bytes(bytes.fromhex(o.findtext('Info/DefaultData')), 'little')
            assert sdo(f, index) == (0, expected)
        else:
            assert access & 0x38 == 0
    for index in (0x1600,0x1601,0x1602,0x1a00,0x1a01,0x1a02,0x1a03):
        items = objects[index].findall('Info/SubItem')
        for sub, item in enumerate(items):
            expected = int.from_bytes(bytes.fromhex(item.findtext('Info/DefaultData')), 'little')
            assert sdo(f, index, size=1 if sub == 0 else 4, sub=sub) == (0, expected), (hex(index),sub)
test('ESI types, access, defaults and all 7 PDO records match the real ARM dictionary', xml_contract)

print(f'{passed} ARM telemetry tests passed (software only).')
