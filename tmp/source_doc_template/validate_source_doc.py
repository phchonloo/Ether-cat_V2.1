import hashlib
import json
import zipfile
from pathlib import Path

from lxml import etree


ROOT = Path(r"C:\Users\pc\Desktop\01_FCM4E1553_SQI_82_CTT_V1.4")
SOURCE_ROOT = ROOT / "program" / "FCM4E1553 Ethercat"
DOCX = ROOT / "docs" / "FCM4E1553_源代码_60页.docx"


def read_source(path: Path) -> str:
    data = path.read_bytes()
    for encoding in ("utf-8-sig", "gb18030", "latin-1"):
        try:
            return data.decode(encoding)
        except UnicodeDecodeError:
            pass
    raise RuntimeError(f"Unable to decode {path}")


files = sorted(
    p
    for p in SOURCE_ROOT.rglob("*")
    if p.is_file()
    and p.suffix.lower() in {".c", ".h", ".s", ".asm"}
    and "MDK-ARM/RTE" not in p.as_posix()
)

expected = []
for path in files:
    text = read_source(path).replace("\r\n", "\n").replace("\r", "\n")
    if text.endswith("\n"):
        text = text[:-1]
    expected.extend(line.expandtabs(4) for line in text.split("\n"))

expected = expected[:1650] + expected[-1650:]

with zipfile.ZipFile(DOCX) as archive:
    document_xml = archive.read("word/document.xml")

root = etree.fromstring(document_xml)
ns = {"w": "http://schemas.openxmlformats.org/wordprocessingml/2006/main"}
paragraphs = root.xpath("//w:body/w:p", namespaces=ns)
actual = ["".join(p.xpath(".//w:t/text()", namespaces=ns)) for p in paragraphs]

first_mismatch = None
for index, (left, right) in enumerate(zip(expected, actual), 1):
    if left != right:
        first_mismatch = {"line": index, "expected": left, "actual": right}
        break
if first_mismatch is None and len(expected) != len(actual):
    first_mismatch = {
        "line": min(len(expected), len(actual)) + 1,
        "expected": "<missing>" if len(expected) < len(actual) else expected[len(actual)],
        "actual": "<missing>" if len(actual) < len(expected) else actual[len(expected)],
    }

expected_blob = "\n".join(expected).encode("utf-8")
actual_blob = "\n".join(actual).encode("utf-8")
print(
    json.dumps(
        {
            "source_files": len(files),
            "expected_lines": len(expected),
            "document_lines": len(actual),
            "exact_match": expected == actual,
            "expected_sha256": hashlib.sha256(expected_blob).hexdigest(),
            "document_sha256": hashlib.sha256(actual_blob).hexdigest(),
            "first_mismatch": first_mismatch,
        },
        ensure_ascii=False,
    )
)
