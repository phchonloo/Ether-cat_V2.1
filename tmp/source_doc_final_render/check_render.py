import json
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFont


ROOT = Path(r"C:\Users\pc\Desktop\01_FCM4E1553_SQI_82_CTT_V1.4\tmp\source_doc_final_render")
PAGE_DIR = ROOT / "pages60"
pages = sorted(PAGE_DIR.glob("page-*.png"))
if len(pages) != 60:
    raise RuntimeError(f"Expected 60 rendered pages, found {len(pages)}")

font = ImageFont.load_default()
checks = []
contacts = []
thumb_w, thumb_h = 200, 283
cols, rows = 5, 3

for page_no, path in enumerate(pages, 1):
    with Image.open(path).convert("RGB") as image:
        white = Image.new("RGB", image.size, "white")
        diff = ImageChops.difference(image, white).convert("L")
        bbox = diff.point(lambda x: 255 if x > 18 else 0).getbbox()
        if bbox is None:
            checks.append({"page": page_no, "blank": True, "bbox": None, "edge_risk": True})
            continue
        left, top, right, bottom = bbox
        edge_risk = left < 12 or top < 12 or right > image.width - 12 or bottom > image.height - 12
        checks.append({
            "page": page_no,
            "blank": False,
            "bbox": [left, top, right, bottom],
            "edge_risk": edge_risk,
            "size": list(image.size),
        })

for group_start in range(0, 60, cols * rows):
    sheet = Image.new("RGB", (cols * thumb_w, rows * (thumb_h + 20)), "#d8dde5")
    draw = ImageDraw.Draw(sheet)
    for offset, path in enumerate(pages[group_start:group_start + cols * rows]):
        page_no = group_start + offset + 1
        with Image.open(path).convert("RGB") as image:
            image.thumbnail((thumb_w - 8, thumb_h - 8))
            x = (offset % cols) * thumb_w + (thumb_w - image.width) // 2
            y = (offset // cols) * (thumb_h + 20) + 16
            sheet.paste(image, (x, y))
            draw.text((x, 2 + (offset // cols) * (thumb_h + 20)), f"Page {page_no}", fill="black", font=font)
    output = ROOT / f"contact-{group_start + 1:02d}-{min(group_start + cols * rows, 60):02d}.png"
    sheet.save(output)
    contacts.append(str(output))

report = {
    "pages": len(pages),
    "blank_pages": [x["page"] for x in checks if x["blank"]],
    "edge_risk_pages": [x["page"] for x in checks if x["edge_risk"]],
    "contacts": contacts,
    "sample_bboxes": [checks[i] for i in (0, 29, 30, 59)],
}
(ROOT / "render_report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
print(json.dumps(report, ensure_ascii=False))
