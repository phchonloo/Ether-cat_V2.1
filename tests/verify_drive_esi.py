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
