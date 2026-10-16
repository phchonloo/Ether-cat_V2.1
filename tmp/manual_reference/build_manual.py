from __future__ import annotations

import hashlib
import shutil
from pathlib import Path

from docx import Document
from docx.enum.style import WD_STYLE_TYPE
from docx.enum.table import WD_CELL_VERTICAL_ALIGNMENT, WD_TABLE_ALIGNMENT
from docx.enum.text import WD_ALIGN_PARAGRAPH, WD_BREAK, WD_LINE_SPACING
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Inches, Mm, Pt, RGBColor
from PIL import Image, ImageOps


ROOT = Path(r"C:\Users\pc\Desktop\01_FCM4E1553_SQI_82_CTT_V1.4")
REFERENCE = Path(r"C:\Users\pc\Desktop\组态屏软著\使用说明书.docx")
OUTPUT = ROOT / "docs" / "FCM4E1553_EtherCAT单轴步进电机驱动软件_使用说明书.docx"
TMP = ROOT / "tmp" / "manual_reference"
BOARD_SOURCE = Path(r"C:\Users\pc\Documents\WXWork\1688855977836683\Cache\Image\2026-10\d3577549-b5c0-4dba-889a-daa29b5f5b55.jpg")
TWINCAT_SOURCE = Path(r"C:\Users\pc\Documents\WXWork\1688855977836683\Cache\Image\2026-10\企业微信截图_17921317117260.png")
BOARD_IMAGE = TMP / "board_horizontal.jpg"

BLUE = "1F4E78"
PALE_BLUE = "DCE6F1"
VERY_PALE_BLUE = "F3F7FB"
LIGHT_GRAY = "D9D9D9"
TEXT = "1F1F1F"
MUTED = "6B7280"
RED = "C00000"
PAGE_BREAK_NEXT = False


def set_run_font(run, size=None, bold=None, color=None, name="Microsoft YaHei"):
    run.font.name = name
    run._element.get_or_add_rPr().get_or_add_rFonts().set(qn("w:eastAsia"), name)
    run._element.get_or_add_rPr().get_or_add_rFonts().set(qn("w:ascii"), "Arial" if name == "Microsoft YaHei" else name)
    run._element.get_or_add_rPr().get_or_add_rFonts().set(qn("w:hAnsi"), "Arial" if name == "Microsoft YaHei" else name)
    if size is not None:
        run.font.size = Pt(size)
    if bold is not None:
        run.font.bold = bold
    if color is not None:
        run.font.color.rgb = RGBColor.from_string(color)


def clear_body(doc: Document):
    body = doc._element.body
    for child in list(body):
        if child.tag != qn("w:sectPr"):
            body.remove(child)


def set_cell_shading(cell, fill):
    tc_pr = cell._tc.get_or_add_tcPr()
    shd = tc_pr.find(qn("w:shd"))
    if shd is None:
        shd = OxmlElement("w:shd")
        tc_pr.append(shd)
    shd.set(qn("w:fill"), fill)


def set_cell_margins(cell, top=90, start=100, bottom=90, end=100):
    tc = cell._tc
    tc_pr = tc.get_or_add_tcPr()
    tc_mar = tc_pr.first_child_found_in("w:tcMar")
    if tc_mar is None:
        tc_mar = OxmlElement("w:tcMar")
        tc_pr.append(tc_mar)
    for margin, value in (("top", top), ("start", start), ("bottom", bottom), ("end", end)):
        node = tc_mar.find(qn(f"w:{margin}"))
        if node is None:
            node = OxmlElement(f"w:{margin}")
            tc_mar.append(node)
        node.set(qn("w:w"), str(value))
        node.set(qn("w:type"), "dxa")


def set_table_borders(table, color=LIGHT_GRAY, size="6"):
    tbl_pr = table._tbl.tblPr
    borders = tbl_pr.first_child_found_in("w:tblBorders")
    if borders is None:
        borders = OxmlElement("w:tblBorders")
        tbl_pr.append(borders)
    for edge in ("top", "left", "bottom", "right", "insideH", "insideV"):
        tag = borders.find(qn(f"w:{edge}"))
        if tag is None:
            tag = OxmlElement(f"w:{edge}")
            borders.append(tag)
        tag.set(qn("w:val"), "single")
        tag.set(qn("w:sz"), size)
        tag.set(qn("w:space"), "0")
        tag.set(qn("w:color"), color)


def set_repeat_table_header(row):
    tr_pr = row._tr.get_or_add_trPr()
    tbl_header = OxmlElement("w:tblHeader")
    tbl_header.set(qn("w:val"), "true")
    tr_pr.append(tbl_header)


def keep_paragraph(paragraph, with_next=False):
    paragraph.paragraph_format.keep_together = True
    paragraph.paragraph_format.keep_with_next = with_next


def add_body(doc, text="", bold_lead=None, color=None, align=None):
    p = doc.add_paragraph(style="Normal")
    if bold_lead and text.startswith(bold_lead):
        lead = p.add_run(bold_lead)
        set_run_font(lead, bold=True, color=color or TEXT)
        run = p.add_run(text[len(bold_lead):])
        set_run_font(run, color=color or TEXT)
    else:
        run = p.add_run(text)
        set_run_font(run, color=color or TEXT)
    if align is not None:
        p.alignment = align
    return p


def add_bullet(doc, text, level=0):
    style = "List Bullet" if level == 0 else "List Bullet 2"
    p = doc.add_paragraph(style=style)
    p.paragraph_format.space_after = Pt(2)
    set_run_font(p.add_run(text), color=TEXT)
    return p


def add_number(doc, text):
    p = doc.add_paragraph(style="List Number")
    p.paragraph_format.space_after = Pt(2)
    set_run_font(p.add_run(text), color=TEXT)
    return p


def add_heading(doc, text, level=1):
    global PAGE_BREAK_NEXT
    p = doc.add_paragraph(text, style=f"Heading {level}")
    if PAGE_BREAK_NEXT:
        p.paragraph_format.page_break_before = True
        PAGE_BREAK_NEXT = False
    for run in p.runs:
        set_run_font(run, color="000000", bold=True)
    keep_paragraph(p, with_next=True)
    return p


def add_page_break(doc):
    global PAGE_BREAK_NEXT
    PAGE_BREAK_NEXT = True


def add_code(doc, lines):
    for line in lines:
        p = doc.add_paragraph()
        p.paragraph_format.left_indent = Mm(8)
        p.paragraph_format.space_after = Pt(0)
        p.paragraph_format.line_spacing_rule = WD_LINE_SPACING.SINGLE
        run = p.add_run(line)
        set_run_font(run, size=9, color="30363D", name="Consolas")


def add_table(doc, headers, rows, widths=None, font_size=9):
    table = doc.add_table(rows=1, cols=len(headers))
    table.alignment = WD_TABLE_ALIGNMENT.CENTER
    table.autofit = False
    set_table_borders(table)
    hdr = table.rows[0]
    set_repeat_table_header(hdr)
    for idx, text in enumerate(headers):
        cell = hdr.cells[idx]
        set_cell_shading(cell, BLUE)
        set_cell_margins(cell)
        cell.vertical_alignment = WD_CELL_VERTICAL_ALIGNMENT.CENTER
        p = cell.paragraphs[0]
        p.alignment = WD_ALIGN_PARAGRAPH.CENTER
        run = p.add_run(str(text))
        set_run_font(run, size=font_size, bold=True, color="FFFFFF")
        if widths:
            cell.width = Inches(widths[idx])
    for ri, row in enumerate(rows):
        cells = table.add_row().cells
        for ci, value in enumerate(row):
            cell = cells[ci]
            set_cell_margins(cell)
            cell.vertical_alignment = WD_CELL_VERTICAL_ALIGNMENT.CENTER
            if ri % 2 == 1:
                set_cell_shading(cell, VERY_PALE_BLUE)
            p = cell.paragraphs[0]
            p.alignment = WD_ALIGN_PARAGRAPH.CENTER if ci == 0 or len(str(value)) <= 14 else WD_ALIGN_PARAGRAPH.LEFT
            run = p.add_run(str(value))
            set_run_font(run, size=font_size, color=TEXT)
            if widths:
                cell.width = Inches(widths[ci])
    after = doc.add_paragraph()
    after.paragraph_format.space_after = Pt(2)
    return table


def add_figure(doc, image_path, caption, width=6.15):
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p.paragraph_format.space_before = Pt(3)
    p.paragraph_format.space_after = Pt(3)
    p.paragraph_format.keep_with_next = True
    shape = p.add_run().add_picture(str(image_path), width=Inches(width))
    shape._inline.docPr.set("descr", caption)
    cap = doc.add_paragraph(caption, style="Caption")
    cap.alignment = WD_ALIGN_PARAGRAPH.CENTER
    cap.paragraph_format.space_after = Pt(6)
    for run in cap.runs:
        set_run_font(run, size=9, bold=True, color=BLUE)
    return p


def add_warning(doc, text):
    p = doc.add_paragraph()
    p.paragraph_format.space_before = Pt(4)
    p.paragraph_format.space_after = Pt(4)
    lead = p.add_run("警告  ")
    set_run_font(lead, bold=True, color=RED)
    set_run_font(p.add_run(text), color=RED)
    return p


def prepare_board_image():
    image = ImageOps.exif_transpose(Image.open(BOARD_SOURCE)).convert("RGB")
    if image.height > image.width:
        image = image.rotate(90, expand=True)
    image.save(BOARD_IMAGE, quality=94, subsampling=0)


def main():
    if hashlib.sha256(REFERENCE.read_bytes()).hexdigest() != "ebc9c449ce989470c6f13c0d9a2ff1e14418244a04529993d8844430f648e87c":
        raise RuntimeError("Reference document changed; distill it again before authoring")
    if not BOARD_SOURCE.exists() or not TWINCAT_SOURCE.exists():
        raise FileNotFoundError("A supplied image is no longer available")

    TMP.mkdir(parents=True, exist_ok=True)
    prepare_board_image()
    doc = Document(REFERENCE)
    clear_body(doc)

    section = doc.sections[0]
    section.page_width = Mm(210)
    section.page_height = Mm(297)
    section.top_margin = Pt(62.35)
    section.bottom_margin = Pt(62.35)
    section.left_margin = Pt(70.85)
    section.right_margin = Pt(70.85)
    section.header_distance = Pt(36)
    section.footer_distance = Pt(36)

    normal = doc.styles["Normal"]
    normal.font.name = "Arial"
    normal._element.rPr.rFonts.set(qn("w:eastAsia"), "Microsoft YaHei")
    normal.font.size = Pt(10.5)
    normal.font.color.rgb = RGBColor.from_string(TEXT)
    normal.paragraph_format.space_after = Pt(4)
    normal.paragraph_format.line_spacing = 1.4
    for style_name, size, before, after in (("Heading 1", 18, 12, 6), ("Heading 2", 14, 12, 6), ("Heading 3", 12, 8, 6)):
        style = doc.styles[style_name]
        style.font.name = "Arial"
        style._element.rPr.rFonts.set(qn("w:eastAsia"), "Microsoft YaHei")
        style.font.size = Pt(size)
        style.font.bold = True
        style.font.color.rgb = RGBColor(0, 0, 0)
        style.paragraph_format.space_before = Pt(before)
        style.paragraph_format.space_after = Pt(after)
        style.paragraph_format.keep_with_next = True
    title_style = doc.styles["Title"]
    title_style.font.name = "Arial"
    title_style._element.rPr.rFonts.set(qn("w:eastAsia"), "Microsoft YaHei")
    title_style.font.size = Pt(26)
    title_style.font.bold = True
    title_style.font.color.rgb = RGBColor(0, 0, 0)
    title_ppr = title_style._element.get_or_add_pPr()
    title_border = title_ppr.find(qn("w:pBdr"))
    if title_border is not None:
        title_ppr.remove(title_border)

    header = section.header.paragraphs[0]
    header.text = "FCM4E1553 EtherCAT 单轴步进电机驱动软件  使用说明书"
    header.alignment = WD_ALIGN_PARAGRAPH.RIGHT
    for run in header.runs:
        set_run_font(run, size=8, color="A6A6A6")

    # Cover
    spacer = doc.add_paragraph()
    spacer.paragraph_format.space_before = Pt(95)
    p = doc.add_paragraph(style="Title")
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    set_run_font(p.add_run("FCM4E1553 EtherCAT"), size=26, bold=True, color="000000")
    p2 = doc.add_paragraph()
    p2.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p2.paragraph_format.space_after = Pt(18)
    set_run_font(p2.add_run("单轴步进电机驱动软件"), size=22, bold=True, color="000000")
    p3 = doc.add_paragraph()
    p3.alignment = WD_ALIGN_PARAGRAPH.CENTER
    set_run_font(p3.add_run("使用说明书"), size=18, bold=True, color="000000")
    p4 = doc.add_paragraph()
    p4.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p4.paragraph_format.space_before = Pt(36)
    set_run_font(p4.add_run("操作设备前请完整阅读本手册并妥善保存"), size=10, color=TEXT)
    p5 = doc.add_paragraph()
    p5.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p5.paragraph_format.space_before = Pt(130)
    set_run_font(p5.add_run("深圳星火自动化科技有限公司"), size=10, color=TEXT)
    p6 = doc.add_paragraph()
    p6.alignment = WD_ALIGN_PARAGRAPH.CENTER
    set_run_font(p6.add_run("版本 V1.0.0"), size=9, color=TEXT)
    add_page_break(doc)

    add_heading(doc, "前言", 1)
    add_body(doc, "FCM4E1553 EtherCAT 单轴步进电机驱动软件运行于 N32G455 微控制器，通过 EtherCAT CoE 通信实现 CiA402 单轴驱动控制。软件面向两相步进电机的开环位置与速度控制，提供 PP、PV、CSP 和 CSV 四种运行模式，并通过对象字典、PDO、状态字及诊断变量向主站提供统一接口。")
    add_body(doc, "本说明书说明硬件连接、TwinCAT 工程准备、EtherCAT 同步配置、CiA402 使能顺序、运动模式操作、对象地址和故障处理。所有参数均应先在低速、空载、可急停的条件下验证，再投入实际设备。")
    add_heading(doc, "安全注意事项", 1)
    add_warning(doc, "本产品不具备 STO 等功能安全认证。EtherCAT 通信、软件停机和状态字均不得替代独立急停、安全继电器、机械制动或防护装置。")
    add_bullet(doc, "接线、插拔电机线和拆装驱动器前必须断开主电源并确认母线电压已释放。")
    add_bullet(doc, "竖直轴、重力负载和可能造成人身伤害的机构必须配置独立机械制动或支撑。")
    add_bullet(doc, "首次调试使用低电流、低速度和小位移；确保运动方向、相序和急停功能正确。")
    add_bullet(doc, "通信恢复后不得自动继续故障前的旧轨迹，必须重新确认目标、状态和启动条件。")
    add_bullet(doc, "0x6064 和 0x606C 是内部执行链的逻辑位置与速度，不是编码器或轴端传感器测量值。")
    add_page_break(doc)

    add_heading(doc, "目录", 1)
    for item in [
        "前言", "安全注意事项", "一 产品概述", "二 硬件接口与安装", "三 TwinCAT 工程准备",
        "四 EtherCAT 通信配置", "五 CiA402 状态机与使能", "六 PP 轮廓位置模式",
        "七 PV 轮廓速度模式", "八 CSP 周期同步位置模式", "九 CSV 周期同步速度模式",
        "十 对象字典与 PDO", "十一 故障诊断 停机与复位", "十二 常见问题排查",
        "十三 规格与实现边界", "附录 最小调试流程和地址速查",
    ]:
        p = doc.add_paragraph()
        p.paragraph_format.left_indent = Mm(5 if item in {"前言", "安全注意事项"} else 0)
        p.paragraph_format.space_after = Pt(4)
        set_run_font(p.add_run(item), size=10.5, color=TEXT)
    add_page_break(doc)

    add_heading(doc, "一 产品概述", 1)
    add_heading(doc, "1.1 软件定位", 3)
    add_body(doc, "本软件是 FCM4E1553 单轴 EtherCAT 步进驱动器的嵌入式控制固件。主站通过 EtherCAT 周期过程数据发送控制字、目标位置、目标速度和模式请求，驱动器返回状态字、内部执行位置、内部执行速度和当前模式。")
    add_heading(doc, "1.2 已实现功能", 3)
    for text in [
        "CoE SDO 与 PDO 通信，符合单轴 CiA402 状态机的基本控制流程。",
        "PP 轮廓位置、PV 轮廓速度、CSP 周期同步位置和 CSV 周期同步速度。",
        "DC Synchron 与 SYNC0 周期同步；当前硬件未连接 SYNC1，固件已屏蔽该中断。",
        "故障锁存、通信异常停机、控制字故障复位和首次错误诊断快照。",
        "默认 12 字节 RxPDO 与 12 字节 TxPDO，支持 PDO 映射模板。",
    ]:
        add_bullet(doc, text)
    add_heading(doc, "1.3 使用边界", 3)
    add_body(doc, "本机为开环步进控制。回零输入、正负限位、探针输入和编码器反馈当前未接线，因此回零、闭环位置反馈、真实测速、软件限位保护和力矩控制不得作为已实现功能使用。对象字典中存在的 0x6077、0x607D、0x60C2 等对象仅表示协议接口或占位，不等同于物理功能已经完成。")
    add_table(doc, ["模式", "代码", "用途", "主站责任"], [
        ["PP", "1", "单次绝对或相对定位", "触发新目标并完成握手"],
        ["PV", "3", "轮廓速度运行", "保持目标速度并按流程停止"],
        ["CSP", "8", "周期位置轨迹", "每周期发送连续位置目标"],
        ["CSV", "9", "周期速度控制", "每周期维护速度目标"],
    ], [1.0, 0.65, 2.0, 2.65])
    add_page_break(doc)

    add_heading(doc, "二 硬件接口与安装", 1)
    add_figure(doc, BOARD_IMAGE, "图 1 FCM4E1553 驱动控制板硬件总览", width=6.25)
    add_body(doc, "硬件包括电源与电机端子、数字 I O 端子、两路 EtherCAT RJ45 接口、状态指示灯、EtherCAT 从站控制器及 N32G455 主控。接口定义应以交付硬件丝印和端子图为准。")
    add_heading(doc, "2.1 主要接口", 3)
    add_table(doc, ["接口", "用途", "接线要求"], [
        ["电源端子", "DC 24 V 至 48 V 供电", "断电接线，确认极性和电源容量"],
        ["电机端子", "两相步进电机 A+ A- B+ B-", "确认相序，禁止带电插拔"],
        ["EtherCAT IN", "连接主站或上一级从站", "使用屏蔽双绞线"],
        ["EtherCAT OUT", "连接下一级从站", "末端可不连接"],
        ["数字 I O", "输入、输出和制动控制", "功能以当前硬件版本为准"],
    ], [1.25, 2.25, 2.8])
    add_page_break(doc)

    add_heading(doc, "三 安装与接线", 1)
    add_heading(doc, "3.1 安装前检查", 3)
    for text in [
        "确认驱动器型号、供电电压和电机额定电流匹配。",
        "检查端子、PCB、外壳和网口无变形、松动或污染。",
        "准备独立急停、合适熔断器或断路器，并确保保护接地可靠。",
        "电机动力线与 EtherCAT、I O 信号线分开布线。",
    ]:
        add_number(doc, text)
    add_heading(doc, "3.2 基本接线顺序", 3)
    add_code(doc, [
        "24-48 VDC 电源  ->  电源端子 V+ GND",
        "两相步进电机    ->  A+ A- B+ B-",
        "TwinCAT 主站    ->  EtherCAT IN",
        "下一级从站      ->  EtherCAT OUT",
    ])
    add_warning(doc, "首次上电前必须复核电源极性、电机相序及网线方向。主站进入 OP 并不表示电机已使能；电机使能还需要正确的 CiA402 控制字顺序。")
    add_heading(doc, "3.3 上电前检查表", 3)
    add_table(doc, ["检查项", "合格条件"], [
        ["电源", "电压在允许范围内，极性正确，无短路"],
        ["电机", "两相绕组识别正确，端子牢固"],
        ["网络", "IN 口连接主站，链路灯可正常指示"],
        ["机械", "轴可安全运动，急停和防护有效"],
        ["参数", "目标速度与位移为低风险测试值"],
    ], [2.0, 4.3])
    add_page_break(doc)

    add_heading(doc, "四 TwinCAT 工程准备", 1)
    add_heading(doc, "4.1 软件环境", 3)
    add_table(doc, ["项目", "要求"], [
        ["主站软件", "TwinCAT 3 XAE 或兼容 EtherCAT 主站"],
        ["网络适配器", "安装并绑定 TwinCAT RT Ethernet 驱动的有线网卡"],
        ["ESI 文件", "FCM4E1553_CiA402_SingleAxis_Rev03.xml"],
        ["PLC 示例", "PLC_CSP_NEW 或按本手册建立的等效控制程序"],
        ["推荐周期", "PLC Task 2 ms，DC SYNC0 2 ms"],
    ], [1.6, 4.7])
    add_heading(doc, "4.2 加载 ESI 与扫描设备", 3)
    for text in [
        "将 ESI 文件复制到 TwinCAT EtherCAT 配置目录，然后重新加载设备描述。",
        "选择已绑定 RT 驱动的有线网卡并执行 Scan Devices。",
        "确认扫描结果显示 FCM4E1553 CiA402 Single-Axis Drive。",
        "激活配置后检查从站依次进入 PREOP、SAFEOP 和 OP。",
    ]:
        add_number(doc, text)
    add_heading(doc, "4.3 设备身份", 3)
    add_table(doc, ["项目", "值"], [
        ["Vendor ID", "0x00000E66"],
        ["Product Code", "0x15530111"],
        ["Revision", "0x00000003"],
        ["设备名称", "FCM4E1553_CiA402_Drive"],
    ], [2.1, 4.2])
    add_page_break(doc)

    add_heading(doc, "五 TwinCAT 在线运行状态", 1)
    add_figure(doc, TWINCAT_SOURCE, "图 2 TwinCAT 在线变量与 EtherCAT OP 状态", width=6.25)
    add_body(doc, "图中 qControlWord 为主站输出控制字，qTargetPosition 和 qTargetVelocity 为目标量，qMode 为模式请求；iStatusWord、iActualPosition、iActualVelocity 和 iModeDisplay 为驱动器返回量。iEthercatState 等于 8 表示从站处于 OP。")
    add_heading(doc, "5.1 上线后必须确认", 3)
    add_bullet(doc, "EtherCAT 状态为 OP，WcState 无异常，主站任务保持运行。")
    add_bullet(doc, "iModeDisplay 与 qMode 一致，不能只看主站写入值。")
    add_bullet(doc, "使能前先将目标速度设为 0；CSP 目标位置先与当前内部位置对齐。")
    add_bullet(doc, "控制变量只能由一个控制源写入，避免 CoE 手动值与 PLC 周期 PDO 相互覆盖。")
    add_page_break(doc)

    add_heading(doc, "六 EtherCAT 通信配置", 1)
    add_heading(doc, "6.1 默认 PDO", 3)
    add_table(doc, ["方向", "偏移", "对象", "类型", "含义"], [
        ["RxPDO", "0-1", "6040", "U16", "控制字"],
        ["RxPDO", "2-5", "607A", "I32", "目标位置"],
        ["RxPDO", "6-9", "60FF", "I32", "目标速度"],
        ["RxPDO", "10", "6060", "I8", "模式请求"],
        ["TxPDO", "0-1", "6041", "U16", "状态字"],
        ["TxPDO", "2-5", "6064", "I32", "内部执行位置"],
        ["TxPDO", "6-9", "606C", "I32", "内部执行速度"],
        ["TxPDO", "10", "6061", "I8", "当前模式"],
    ], [0.85, 0.75, 0.8, 0.75, 3.0])
    add_heading(doc, "6.2 DC Synchron 设置", 3)
    add_number(doc, "在从站 DC 页面选择 DC-Synchron。")
    add_number(doc, "设置 Sync Unit Cycle 为 2000 us，SYNC0 选择 x1，Shift Time 先设为 0。")
    add_number(doc, "PLC Task 设置为 2.000 ms，并与 SYNC0 周期保持一致。")
    add_number(doc, "启用 SYNC0，保持 SYNC1 关闭；当前硬件没有 SYNC1 连线。")
    add_body(doc, "SYNC0 有 500 Hz 波形只表示同步脉冲存在，不能单独证明每帧 PDO 均按时处理。应同时观察 WcState、AL 状态码、主站帧诊断和 MCU PDI 诊断。")
    add_heading(doc, "6.3 通信失效原则", 3)
    add_body(doc, "当从站离开 OP、过程数据无效、PDI 故障锁存或主站控制字被清零时，驱动输出进入安全停机路径。通信恢复后软件不自动继续旧运动，主站必须重新完成状态确认和启动流程。")
    add_page_break(doc)

    add_heading(doc, "七 CiA402 状态机与使能", 1)
    add_heading(doc, "7.1 控制字顺序", 3)
    add_table(doc, ["步骤", "6040", "期望状态", "6041 掩码结果"], [
        ["关闭使能", "0000", "Switch On Disabled", "0040"],
        ["Shutdown", "0006", "Ready to Switch On", "0021"],
        ["Switch On", "0007", "Switched On", "0023"],
        ["Enable Operation", "000F", "Operation Enabled", "0027"],
        ["Fault Reset", "0080 上升沿", "退出 Fault", "重新判断"],
    ], [1.0, 1.1, 2.5, 1.7])
    add_body(doc, "判断状态时先计算 6041 AND 0x006F，不要直接比较完整状态字。bit9、bit10 和 bit12 等模式相关位可能同时存在。")
    add_heading(doc, "7.2 常用控制位", 3)
    add_table(doc, ["位", "掩码", "用途"], [
        ["bit 0-3", "000F", "CiA402 启停和快速停止"],
        ["bit 4", "0010", "PP 新目标上升沿"],
        ["bit 5", "0020", "PP 立即执行"],
        ["bit 6", "0040", "PP 相对位置选择"],
        ["bit 7", "0080", "故障复位新上升沿"],
        ["bit 8", "0100", "PP PV Halt"],
    ], [0.9, 1.0, 4.4])
    add_warning(doc, "Fault Reset 必须先观察到 bit7 为 0，再产生新的 0 到 1 上升沿。不得在每个 PLC 周期持续强制 0080。")

    # Keep the next operating-mode section flowing after the short reset note.
    # A forced break here can strand the warning on a nearly empty page when
    # Word lays out the preceding control-word tables.
    add_heading(doc, "八 PP 轮廓位置模式", 1)
    add_heading(doc, "8.1 参数与操作", 3)
    add_number(doc, "写 6060 为 1，并确认 6061 返回 1。")
    add_number(doc, "设置 6081 轮廓速度、6083 加速度和 6084 减速度。")
    add_number(doc, "写入 607A 目标位置；首次使用小距离、低速度目标。")
    add_number(doc, "绝对目标发送 002F 后发送 003F；相对目标发送 006F 后发送 007F。")
    add_number(doc, "等待 6041 bit12 确认，再将 bit4 拉低，为下一次目标准备新的上升沿。")
    add_number(doc, "根据 bit10、6064 和内部速度判断运动完成。")
    add_heading(doc, "8.2 重要说明", 3)
    add_bullet(doc, "单次有限目标应完成后保持，不应持续旋转。")
    add_bullet(doc, "保持 bit4 为 1 不会反复启动同一目标；下一目标必须重新拉低再拉高。")
    add_bullet(doc, "相对位置以接受命令时的内部当前位置为基准，不是无限累加目标。")
    add_heading(doc, "九 PV 轮廓速度模式", 1)
    add_number(doc, "写 6060 为 3，并确认 6061 为 3。")
    add_number(doc, "配置 607F 最大速度、6083 加速度和 6084 减速度。")
    add_number(doc, "写 60FF 目标速度，正负号表示方向；非零目标将持续运行。")
    add_number(doc, "停止时写 60FF 为 0，或按设计使用 Halt、Quick Stop 或撤使能流程。")
    add_body(doc, "PV 使用 607F 作为速度上限，不使用 6081 作为运行速度限制。bit8 Halt 仅适用于 PP 和 PV，不应套用于 CSP。")
    add_page_break(doc)

    add_heading(doc, "十 CSP 周期同步位置模式", 1)
    add_heading(doc, "10.1 启动步骤", 3)
    add_number(doc, "设置 DC-Synchron 和 2 ms 周期，确认任务与 SYNC0 一致。")
    add_number(doc, "写 6060 为 8，确认 6061 为 8。")
    add_number(doc, "使能前将 607A 加 60B0 对齐当前 6064，避免跳到旧目标。")
    add_number(doc, "按 0006、0007、000F 顺序使能，每一步等待状态掩码确认。")
    add_number(doc, "主站每个周期发送连续、限速、限加速度的位置目标。")
    add_heading(doc, "10.2 运行与暂停", 3)
    add_body(doc, "CSP 采用约 20 ms 的内部相位平滑，存在相应滞后。主站必须持续规划目标，不能只写一次位置后停止 PDO 更新。正常暂停应由主站规划减速到保持位置，或使用经过验证的 Quick Stop 流程。")
    add_warning(doc, "出现 WcState 异常、AL 同步错误或控制字被清零时，先保存诊断数据，不要立即强制重新使能。通信恢复后重新对齐目标并执行完整启动流程。")
    add_heading(doc, "十一 CSV 周期同步速度模式", 1)
    add_number(doc, "写 6060 为 9，确认 6061 为 9。")
    add_number(doc, "设置 607F 速度上限，并先保持 60FF 为 0。")
    add_number(doc, "完成状态机使能后，按周期更新 60FF 目标速度。")
    add_number(doc, "停止时平滑发送 0 速度，再按负载保持需求决定是否撤使能。")
    add_page_break(doc)

    add_heading(doc, "十二 对象字典与 PDO", 1)
    add_heading(doc, "12.1 常用运动对象", 3)
    add_table(doc, ["对象", "类型", "访问", "用途"], [
        ["603F", "U16", "只读", "CiA402 错误码"],
        ["6040", "U16", "读写 RxPDO", "控制字"],
        ["6041", "U16", "只读 TxPDO", "状态字"],
        ["605A", "I16", "读写", "Quick Stop 选项"],
        ["605D", "I16", "读写", "PP PV Halt 选项"],
        ["6060", "I8", "读写 RxPDO", "请求运行模式"],
        ["6061", "I8", "只读 TxPDO", "当前运行模式"],
        ["6064", "I32", "只读 TxPDO", "内部执行位置"],
        ["606C", "I32", "只读 TxPDO", "内部执行速度"],
        ["607A", "I32", "读写 RxPDO", "目标位置"],
        ["607F", "U32", "读写", "最大速度上限"],
        ["6081", "U32", "读写", "PP 轮廓速度 CSP 跟随上限"],
        ["6083", "U32", "读写", "轮廓加速度"],
        ["6084", "U32", "读写", "轮廓减速度"],
        ["6085", "U32", "读写", "Quick Stop 减速度"],
        ["60B0", "I32", "读写 可映射", "CSP 位置偏移"],
        ["60FF", "I32", "读写 RxPDO", "PV CSV 目标速度"],
        ["6502", "U32", "只读", "支持模式位图 0x185"],
    ], [0.9, 0.8, 1.35, 3.25], font_size=8.6)
    add_heading(doc, "12.2 设备与诊断对象", 3)
    add_table(doc, ["对象", "用途", "说明"], [
        ["1001", "错误寄存器", "错误分类位"],
        ["1018", "身份对象", "Vendor Product Revision Serial"],
        ["10F1", "同步错误设置", "不能用来掩盖主站调度问题"],
        ["1C12 1C13", "PDO 分配", "PREOP 配置"],
        ["1C32 1C33", "同步参数", "周期和同步错误诊断"],
        ["2000", "运动单位", "0 为 rpm  1 为 count per second"],
        ["206C", "厂商故障码", "十进制故障编号"],
    ], [1.15, 1.75, 3.6])
    add_body(doc, "写 2000 改变运动单位时必须处于 PREOP、未使能且执行链已停止。改变单位会清零速度目标并重置相关速度参数，设置不应假定掉电保存。")
    add_page_break(doc)

    add_heading(doc, "十三 故障诊断 停机与复位", 1)
    add_heading(doc, "13.1 停机优先级", 3)
    add_body(doc, "硬件故障、PDI 故障锁存、从站非 OP、过程数据无效和控制字关断条件优先于普通减速请求。故障会取消旧的普通停止状态，避免恢复中断后被旧状态覆盖。")
    add_heading(doc, "13.2 停转时保存的数据", 3)
    for text in [
        "主站 WcState、帧状态、Tx Rx 错误和丢帧计数。",
        "EtherCAT AL 状态、AL 状态码以及是否仍处于 OP。",
        "6040、6041、6061、607A、6064、60FF 和 606C。",
        "603F、1001、206C 以及 MCU 首次错误与首次禁用快照。",
        "SYNC0 和 IRQ 波形，记录相对时序及异常发生前后的变化。",
    ]:
        add_bullet(doc, text)
    add_heading(doc, "13.3 常见诊断区别", 3)
    add_table(doc, ["现象", "含义", "处理方向"], [
        ["WcState 异常", "主站周期过程数据无效", "查主站调度 网卡 任务周期和链路"],
        ["AL 0x001A", "EtherCAT 同步错误", "查 DC 周期 SYNC0 中断和处理时延"],
        ["603F 0x7500", "MCU 与 ESC PDI 故障", "查 SQI 事务和首次 PDI 错误"],
        ["6040 变为 0", "主站或 PLC 主动撤销控制", "查停止锁存和 PLC 写入逻辑"],
    ], [1.4, 2.0, 3.1])
    add_heading(doc, "13.4 故障复位", 3)
    add_number(doc, "先撤销运行请求并确认目标速度为 0。")
    add_number(doc, "保存诊断信息，排除硬件、通信和同步故障。")
    add_number(doc, "控制字 bit7 先保持 0，再发送新的 0 到 1 上升沿。")
    add_number(doc, "等待状态机退出 Fault，再按 0006、0007、000F 重新使能。")
    add_page_break(doc)

    add_heading(doc, "十四 常见问题排查", 1)
    add_table(doc, ["现象", "可能原因", "处理方法"], [
        ["无法进入 OP", "ESI 身份不匹配 网卡未绑定 同步配置错误", "核对 ESI Vendor Product Revision 和 RT 网卡"],
        ["进入 OP 但不能使能", "控制字顺序错误 状态未等待 故障未复位", "按状态掩码逐步执行 0006 0007 000F"],
        ["PP 单次目标一直转", "实际模式不是 PP 60FF 非零 PDO 覆盖手动值", "查 6061 60FF 6040 和唯一写入源"],
        ["CSP 运行后停转", "WcState 异常 AL 同步错误 主站清控制字", "保存诊断 查任务和 DC 周期 重新对齐后启动"],
        ["位置显示与机械不符", "6064 是开环内部位置 存在丢步", "降低速度加速度 检查负载 必要时增加编码器方案"],
        ["速度太小不动作", "内部速度量化后为零", "提高目标速度并检查 2000 单位"],
        ["故障复位无效", "bit7 无新上升沿 故障条件仍存在", "先清 bit7 排除故障后再次上升沿"],
        ["通信恢复后不自动继续", "保护设计阻止旧运动自动恢复", "重新确认目标 状态与启动许可"],
    ], [1.35, 2.35, 2.8], font_size=8.7)
    add_heading(doc, "十四点一 调试建议", 3)
    add_bullet(doc, "先用 PP 小位移确认相序、方向和计数分辨率，再测试连续模式。")
    add_bullet(doc, "使用 CSP 前先验证主站 RT 网卡、2 ms 任务和 DC 同步稳定性。")
    add_bullet(doc, "出现偶发停机时保留第一次异常快照，不要只看故障后的静态变量。")
    add_page_break(doc)

    add_heading(doc, "十五 规格与实现边界", 1)
    add_table(doc, ["项目", "规格或说明"], [
        ["软件平台", "N32G455 MCU 嵌入式固件"],
        ["通信", "EtherCAT CoE  CiA402"],
        ["设备身份", "Vendor 0xE66  Product 0x15530111  Revision 3"],
        ["运动模式", "PP 1  PV 3  CSP 8  CSV 9"],
        ["默认 PDO", "Rx 12 字节  Tx 12 字节"],
        ["推荐主站周期", "2 ms  DC Synchron  SYNC0 x1"],
        ["逻辑分辨率", "默认 1000 count per revolution"],
        ["速度单位", "rpm 或 count per second"],
        ["位置范围", "32 位有符号逻辑计数"],
        ["控制类型", "开环步进控制"],
        ["反馈说明", "6064 606C 为内部执行量 非编码器测量"],
        ["未实现", "回零 编码器闭环 真实力矩 软件限位 探针"],
        ["SYNC1", "当前硬件未连接 固件屏蔽"],
        ["功能安全", "无 STO 认证 需独立安全回路"],
    ], [2.05, 4.25])
    add_heading(doc, "十五点一 硬件参考参数", 3)
    add_body(doc, "现有产品资料给出的供电范围为 DC 24 V 至 48 V，外形尺寸约 134 mm × 77 mm × 34 mm。最终电流能力、I O 电气特性、端子编号和安装孔位应以量产硬件规格书与丝印为准。")
    add_page_break(doc)

    add_heading(doc, "附录 最小调试流程和地址速查", 1)
    add_heading(doc, "A 最小调试流程", 3)
    for text in [
        "安装 ESI，扫描设备，核对 Vendor Product Revision。",
        "配置 TwinCAT RT 网卡、2 ms PLC Task 和 2 ms DC SYNC0。",
        "确认从站 OP、WcState 正常，模式显示与模式请求一致。",
        "准备零速度或位置对齐目标，按 0006 0007 000F 使能。",
        "先以 PP 小位移低速运行，确认方向与急停，再使用 CSP 或速度模式。",
        "停止或故障后先保存诊断，复位并重新执行完整启动流程。",
    ]:
        add_number(doc, text)
    add_heading(doc, "B 地址速查", 3)
    add_table(doc, ["对象", "名称", "重点"], [
        ["6040", "控制字", "使能 停止 PP 握手 故障复位"],
        ["6041", "状态字", "用 006F 掩码判断状态"],
        ["6060 6061", "模式请求与显示", "必须核对二者一致"],
        ["607A 6064", "目标位置与内部位置", "CSP 启动前对齐"],
        ["60FF 606C", "目标速度与内部速度", "PV CSV 非零目标持续有效"],
        ["603F 1001 206C", "故障对象", "区分 CiA402 分类和厂商故障"],
        ["1C32 1C33", "同步参数", "周期和同步诊断"],
        ["2000", "运动单位", "更改需 PREOP 未使能"],
    ], [1.25, 2.0, 3.05])
    add_body(doc, "文档编号  FCM4E1553-UM-V1.0", align=WD_ALIGN_PARAGRAPH.CENTER)
    add_body(doc, "深圳星火自动化科技有限公司", align=WD_ALIGN_PARAGRAPH.CENTER)

    core = doc.core_properties
    core.title = "FCM4E1553 EtherCAT 单轴步进电机驱动软件使用说明书"
    core.subject = "安装配置 运行操作 对象地址 故障诊断"
    core.author = "深圳星火自动化科技有限公司"
    core.last_modified_by = ""
    core.keywords = "FCM4E1553 EtherCAT CiA402 PP PV CSP CSV N32G455"
    core.comments = ""

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    doc.save(OUTPUT)
    print(OUTPUT)


if __name__ == "__main__":
    main()
