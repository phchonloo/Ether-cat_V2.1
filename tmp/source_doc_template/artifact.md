# Source code document layout contract

## Reference

- Original reference: `C:\Users\pc\Desktop\源代码.docx`
- SHA-256 observed during read-only inspection: `b68e8189f9a6673d5fb78b1ed3178f5b7258323f8e5ed73335cdeb9acf5a6b16`
- Observed page count: 61
- Observed section count: 1
- Render evidence: `tmp/source_doc_template/reference.pdf` and `tmp/source_doc_template/page-01.png` through `page-61.png`
- The original attachment disappeared from its supplied path after inspection. The retained PDF and page renders are the visual authority for this task.

## Page system

- A4 portrait, 210 x 297 mm.
- Margins: 15 mm on all four sides.
- One continuous section.
- No visible page header, footer, page number, table, image, or border.

## Typography and flow

- The reference is a continuous syntax-highlighted source listing.
- Each source line is one left-aligned paragraph.
- Reference direct formatting reported 10.5 pt on code runs, with a narrow substituted code face and zero visible paragraph spacing.
- Comments are pale blue-gray, strings green, keywords and types blue/purple, numbers pink, identifiers/operators gray or orange.
- Blank source lines are preserved.
- Reference density is 3758 source paragraphs over 61 pages, about 61.6 logical lines per page.

## Output adaptation

- Add one compact black title paragraph at the start for document identification.
- Use Consolas 8 pt with exact 8.3 pt line spacing. This preserves readability while allowing long embedded-source lines to wrap without reducing a page below the requested 50 logical source lines.
- Distribute all logical source lines evenly across pages, 54 or 55 lines per page, with explicit page breaks before each page group.
- Preserve every source line from all `.c`, `.h`, `.s`, and `.asm` files beneath `program/FCM4E1553 Ethercat`.
- Exclude build products, project metadata, binaries, XML, tests, documents, and vendor library directories outside that source root, matching the previously confirmed 26803-line scope.
- Do not add visible file-separator lines, because they are not source code and would alter the declared source-line total.

## Fidelity gates

- Exactly 84 source files and 26803 source lines must be represented.
- Paragraph count must equal 26804: one title plus 26803 source lines.
- A4 geometry and 15 mm margins must match the reference.
- Every full source page must contain at least 50 logical source lines.
- Final Word export must contain no unexpected blank pages, clipped right edges, overlapping text, or missing glyphs.
