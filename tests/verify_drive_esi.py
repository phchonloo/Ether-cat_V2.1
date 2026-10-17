"""Check the delivered ESI interface; read XML from stdin for protected files."""
import sys
import xml.etree.ElementTree as ET

sys.stdin.reconfigure(encoding='utf-8')
root = ET.fromstring(sys.stdin.read())
device = root.find('.//Device')
assert device.find('Type').get('ProductCode') == '#x15530111'
assert device.find('Type').get('RevisionNo') == '#x3'
assert root.findtext('Vendor/Id').lower() != '#x00000da0'
objects = device.findall('.//Dictionary/Objects/Object')
by_index = {int(o.findtext('Index').replace('#x', ''), 16): o for o in objects}
assert len(by_index) == len(objects)
expected = {
    0x2000: ('UINT', '16', 'rw', None, '0000'),
    0x605D: ('INT', '16', 'rw', None, '0100'),
    0x607F: ('UDINT', '32', 'rw', None, 'E8030000'),
    0x6085: ('UDINT', '32', 'rw', None, '2C010000'),
    0x60B0: ('DINT', '32', 'rw', 'r', '00000000'),
    0x6502: ('UDINT', '32', 'ro', 't', '85010000'),
}
for index, values in expected.items():
    o = by_index[index]
    actual = tuple(o.findtext(t) for t in
                   ('Type', 'BitSize', 'Flags/Access', 'Flags/PdoMapping', 'Info/DefaultData'))
    assert actual == values, (hex(index), actual, values)
assert by_index[0x2000].find('Flags/Access').get('WriteRestrictions') == 'PreOP'
for index in (0x20F1, 0x6098, 0x60B8, 0x60BA, 0x60FD):
    assert index not in by_index
for tag, expected_indices in (
        ('RxPdo', ['#x6040', '#x607A', '#x60FF', '#x6060', '0']),
        ('TxPdo', ['#x6041', '#x6064', '#x606C', '#x6061', '0'])):
    entries = device.find(tag).findall('Entry')
    assert [e.findtext('Index') for e in entries] == expected_indices
    assert sum(int(e.findtext('BitLen')) for e in entries) == 96
print('PASS ESI XML syntax, unique objects, types/defaults/access, own identity, '
      'unchanged 12-byte PDOs and excluded hardware features (not XSD/CTT validation).')

telemetry_types = {
    0x2001: ('UINT',16), 0x2002: ('DINT',32), 0x2003: ('DINT',32),
    0x2004: ('DINT',32), 0x2005: ('UINT',16), 0x2006: ('UINT',16),
    0x2007: ('UINT',16), 0x2008: ('UDINT',32), 0x2064: ('INT',16),
    0x2065: ('UINT',16), 0x206D: ('UINT',16), 0x206E: ('UINT',16),
    0x206F: ('UINT',16), 0x6078: ('INT',16), 0x6079: ('UDINT',32),
    0x60F4: ('DINT',32),
}
for index, (typ, bits) in telemetry_types.items():
    o = by_index[index]
    assert (o.findtext('Type'), int(o.findtext('BitSize')),
            o.findtext('Flags/Access'), o.findtext('Flags/PdoMapping')) == (typ, bits, 'ro', 't')
for index, default in ((0x2010,3300), (0x2011,200000), (0x2012,1666667),
                       (0x2013,1666667), (0x6075,0)):
    o = by_index[index]
    assert o.findtext('Type') == 'UDINT' and o.findtext('BitSize') == '32'
    assert o.findtext('Flags/Access') == 'rw'
    assert o.find('Flags/Access').get('WriteRestrictions') == 'PreOP'
    assert o.find('Flags/PdoMapping') is None
    assert int.from_bytes(bytes.fromhex(o.findtext('Info/DefaultData')), 'little') == default

def index_of(e):
    return int(e.findtext('Index').replace('#x',''), 16)

types = {t.findtext('Name'): t for t in device.findall('.//Dictionary/DataTypes/DataType')}
for index, count in ((0x1600,5),(0x1601,3),(0x1602,3),(0x1A00,5),
                     (0x1A01,3),(0x1A02,5),(0x1A03,14)):
    obj = by_index[index]
    dt = types[obj.findtext('Type')]
    assert int(dt.findtext('BitSize')) == 16 + count * 32
    items = dt.findall('SubItem')
    assert len(items) == count + 1
    assert [int(s.findtext('BitOffs')) for s in items] == [0] + list(range(16,16 + count*32,32))
    assert obj.findtext('Flags/Access') == ('ro' if index == 0x1A03 else 'rw')
    assert int.from_bytes(bytes.fromhex(obj.findtext('Info/SubItem/Info/DefaultData')), 'little') == count

optional = next(p for p in device.findall('TxPdo') if index_of(p) == 0x1A03)
assert optional.get('Sm') is None  # only explicit assignment may change default process data
entries = optional.findall('Entry')
assert [index_of(e) for e in entries] == [0x6041,0x6064,0x606C,0x6061,0,
    0x2001,0x6078,0x6079,0x2003,0x2004,0x2002,0x6077,0x60F4,0x2008]
assert sum(int(e.findtext('BitLen')) for e in entries) == 42 * 8
for e in entries:
    index = index_of(e)
    if index:
        o = by_index[index]
        assert o.findtext('Type') == e.findtext('DataType')
        assert int(o.findtext('BitSize')) == int(e.findtext('BitLen'))
        assert o.findtext('Flags/PdoMapping') == 't'
assert 'unavailable' in by_index[0x6077].findtext('Name')
print('PASS 21 added telemetry/config objects, 7 PDO records and optional 42-byte TxPDO.')
