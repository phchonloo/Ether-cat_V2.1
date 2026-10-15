#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Markdown to HTML converter for FCM4E1553 User Manual
将Markdown说明书转换为可打印的HTML格式
"""

import re
import sys

def markdown_to_html(md_file, html_file):
    """简单的Markdown到HTML转换器，支持常用语法"""
    
    with open(md_file, 'r', encoding='utf-8') as f:
        content = f.read()
    
    html_parts = []
    
    # HTML头部（带打印样式）
    html_header = '''<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>FCM4E1553 EtherCAT步进驱动器 - 产品说明书</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        
        body {
            font-family: "Microsoft YaHei", "SimSun", Arial, sans-serif;
            line-height: 1.6;
            color: #333;
            background: #fff;
            padding: 20mm;
            max-width: 210mm;
            margin: 0 auto;
        }
        
        /* 标题样式 */
        h1 {
            color: #1a5490;
            font-size: 28pt;
            margin: 20pt 0;
            padding-bottom: 10pt;
            border-bottom: 3px solid #1a5490;
            page-break-after: avoid;
        }
        
        h2 {
            color: #1a5490;
            font-size: 20pt;
            margin: 18pt 0 10pt 0;
            padding-top: 10pt;
            border-bottom: 2px solid #4a90e2;
            page-break-after: avoid;
        }
        
        h3 {
            color: #2d5da1;
            font-size: 16pt;
            margin: 14pt 0 8pt 0;
            page-break-after: avoid;
        }
        
        h4 {
            color: #4a90e2;
            font-size: 13pt;
            margin: 12pt 0 6pt 0;
            page-break-after: avoid;
        }
        
        /* 段落和列表 */
        p {
            margin: 8pt 0;
            text-align: justify;
        }
        
        ul, ol {
            margin: 8pt 0 8pt 20pt;
        }
        
        li {
            margin: 4pt 0;
        }
        
        /* 表格样式 */
        table {
            width: 100%;
            border-collapse: collapse;
            margin: 12pt 0;
            font-size: 10pt;
            page-break-inside: avoid;
        }
        
        th {
            background: #1a5490;
            color: white;
            padding: 8pt;
            text-align: left;
            font-weight: bold;
            border: 1px solid #ddd;
        }
        
        td {
            padding: 6pt 8pt;
            border: 1px solid #ddd;
        }
        
        tr:nth-child(even) {
            background: #f9f9f9;
        }
        
        /* 代码块样式 */
        code {
            background: #f4f4f4;
            padding: 2pt 4pt;
            border-radius: 3px;
            font-family: "Consolas", "Monaco", monospace;
            font-size: 9pt;
            color: #c7254e;
        }
        
        pre {
            background: #f8f8f8;
            border: 1px solid #ddd;
            border-left: 4px solid #1a5490;
            padding: 10pt;
            margin: 10pt 0;
            overflow-x: auto;
            font-family: "Consolas", "Monaco", monospace;
            font-size: 9pt;
            line-height: 1.4;
            page-break-inside: avoid;
        }
        
        pre code {
            background: none;
            padding: 0;
            color: #333;
        }
        
        /* 引用块 */
        blockquote {
            border-left: 4px solid #ffc107;
            background: #fff9e6;
            padding: 10pt 15pt;
            margin: 10pt 0;
            font-style: italic;
            page-break-inside: avoid;
        }
        
        /* 警告框 */
        .warning {
            border-left: 4px solid #ff5722;
            background: #ffebee;
            padding: 10pt 15pt;
            margin: 10pt 0;
            page-break-inside: avoid;
        }
        
        .warning::before {
            content: "⚠️ ";
            font-weight: bold;
        }
        
        /* 提示框 */
        .note {
            border-left: 4px solid #4caf50;
            background: #e8f5e9;
            padding: 10pt 15pt;
            margin: 10pt 0;
            page-break-inside: avoid;
        }
        
        .note::before {
            content: "✓ ";
            font-weight: bold;
        }
        
        /* 链接 */
        a {
            color: #1a5490;
            text-decoration: none;
        }
        
        a:hover {
            text-decoration: underline;
        }
        
        /* 水平线 */
        hr {
            border: none;
            border-top: 2px solid #ddd;
            margin: 20pt 0;
        }
        
        /* 目录样式 */
        .toc {
            background: #f8f9fa;
            border: 1px solid #ddd;
            padding: 15pt;
            margin: 20pt 0;
            page-break-inside: avoid;
        }
        
        .toc h2 {
            margin-top: 0;
            border-bottom: none;
        }
        
        .toc ul {
            list-style: none;
            margin-left: 0;
        }
        
        .toc li {
            margin: 6pt 0;
        }
        
        /* 封面样式 */
        .cover {
            text-align: center;
            padding: 50mm 0;
            page-break-after: always;
        }
        
        .cover h1 {
            font-size: 36pt;
            border: none;
            margin-bottom: 10mm;
        }
        
        .cover .subtitle {
            font-size: 24pt;
            color: #666;
            margin-bottom: 5mm;
        }
        
        .cover .version {
            font-size: 14pt;
            color: #999;
            margin-top: 20mm;
        }
        
        /* 打印样式 */
        @media print {
            body {
                padding: 15mm;
            }
            
            h1, h2, h3, h4 {
                page-break-after: avoid;
            }
            
            table, pre, blockquote, .warning, .note {
                page-break-inside: avoid;
            }
            
            a {
                color: #000;
                text-decoration: none;
            }
            
            .no-print {
                display: none;
            }
        }
        
        /* 页眉页脚（仅打印时） */
        @page {
            margin: 20mm;
            
            @top-right {
                content: "FCM4E1553 产品说明书";
                font-size: 9pt;
                color: #666;
            }
            
            @bottom-right {
                content: counter(page);
                font-size: 9pt;
            }
        }
    </style>
</head>
<body>
'''
    
    html_footer = '''
    <script>
        // 自动生成目录链接（可选）
        document.addEventListener('DOMContentLoaded', function() {
            console.log('FCM4E1553 User Manual loaded');
        });
    </script>
</body>
</html>
'''
    
    html_parts.append(html_header)
    
    # 转换内容
    lines = content.split('\n')
    in_code_block = False
    in_table = False
    table_buffer = []
    
    for i, line in enumerate(lines):
        # 代码块
        if line.startswith('```'):
            if in_code_block:
                html_parts.append('</code></pre>\n')
                in_code_block = False
            else:
                lang = line[3:].strip()
                html_parts.append(f'<pre><code class="language-{lang}">\n')
                in_code_block = True
            continue
        
        if in_code_block:
            html_parts.append(line.replace('<', '&lt;').replace('>', '&gt;') + '\n')
            continue
        
        # 表格处理
        if '|' in line and line.strip().startswith('|'):
            if not in_table:
                in_table = True
                table_buffer = []
            table_buffer.append(line)
            continue
        else:
            if in_table:
                # 处理表格
                html_parts.append(convert_table(table_buffer))
                in_table = False
                table_buffer = []
        
        # 跳过表格分隔行
        if re.match(r'^\|[\s\-:]+\|', line):
            continue
        
        # 标题
        if line.startswith('#'):
            level = len(re.match(r'^#+', line).group())
            text = line[level:].strip()
            # 移除markdown链接语法
            text = re.sub(r'\[([^\]]+)\]\([^\)]+\)', r'\1', text)
            html_parts.append(f'<h{level} id="{text.replace(" ", "-").lower()}">{text}</h{level}>\n')
            continue
        
        # 水平线
        if line.strip() == '---':
            html_parts.append('<hr>\n')
            continue
        
        # 警告/注意块
        if line.strip().startswith('⚠️') or line.strip().startswith('> **警告**'):
            html_parts.append(f'<div class="warning">{line.strip()}</div>\n')
            continue
        
        if line.strip().startswith('✓') or line.strip().startswith('> **注意**'):
            html_parts.append(f'<div class="note">{line.strip()}</div>\n')
            continue
        
        # 引用块
        if line.startswith('>'):
            text = line[1:].strip()
            html_parts.append(f'<blockquote>{text}</blockquote>\n')
            continue
        
        # 无序列表
        if re.match(r'^[\s]*[-*+]\s', line):
            indent = len(re.match(r'^[\s]*', line).group())
            text = re.sub(r'^[\s]*[-*+]\s', '', line)
            text = process_inline(text)
            if i > 0 and not re.match(r'^[\s]*[-*+]\s', lines[i-1]):
                html_parts.append('<ul>\n')
            html_parts.append(f'<li>{text}</li>\n')
            if i < len(lines)-1 and not re.match(r'^[\s]*[-*+]\s', lines[i+1]):
                html_parts.append('</ul>\n')
            continue
        
        # 有序列表
        if re.match(r'^[\s]*\d+\.\s', line):
            text = re.sub(r'^[\s]*\d+\.\s', '', line)
            text = process_inline(text)
            if i > 0 and not re.match(r'^[\s]*\d+\.\s', lines[i-1]):
                html_parts.append('<ol>\n')
            html_parts.append(f'<li>{text}</li>\n')
            if i < len(lines)-1 and not re.match(r'^[\s]*\d+\.\s', lines[i+1]):
                html_parts.append('</ol>\n')
            continue
        
        # 空行
        if not line.strip():
            html_parts.append('<br>\n')
            continue
        
        # 普通段落
        text = process_inline(line)
        html_parts.append(f'<p>{text}</p>\n')
    
    html_parts.append(html_footer)
    
    # 写入文件
    with open(html_file, 'w', encoding='utf-8') as f:
        f.write(''.join(html_parts))
    
    print(f"✓ HTML文件已生成: {html_file}")
    print(f"✓ 请用浏览器打开，按 Ctrl+P 打印为PDF")

def process_inline(text):
    """处理行内Markdown语法"""
    # 粗体
    text = re.sub(r'\*\*(.+?)\*\*', r'<strong>\1</strong>', text)
    # 斜体
    text = re.sub(r'\*(.+?)\*', r'<em>\1</em>', text)
    # 行内代码
    text = re.sub(r'`(.+?)`', r'<code>\1</code>', text)
    # 链接
    text = re.sub(r'\[([^\]]+)\]\(([^\)]+)\)', r'<a href="\2">\1</a>', text)
    return text

def convert_table(lines):
    """转换Markdown表格为HTML"""
    if not lines:
        return ''
    
    html = ['<table>']
    
    for i, line in enumerate(lines):
        # 跳过分隔行
        if re.match(r'^\|[\s\-:]+\|', line):
            continue
        
        cells = [cell.strip() for cell in line.split('|')[1:-1]]
        
        if i == 0:
            # 表头
            html.append('<thead><tr>')
            for cell in cells:
                html.append(f'<th>{process_inline(cell)}</th>')
            html.append('</tr></thead>')
            html.append('<tbody>')
        else:
            # 数据行
            html.append('<tr>')
            for cell in cells:
                html.append(f'<td>{process_inline(cell)}</td>')
            html.append('</tr>')
    
    html.append('</tbody></table>')
    return ''.join(html)

if __name__ == '__main__':
    md_file = './docs/FCM4E1553_User_Manual_V1.0.md'
    html_file = './docs/FCM4E1553_User_Manual_V1.0.html'
    
    try:
        markdown_to_html(md_file, html_file)
    except Exception as e:
        print(f"✗ 转换失败: {e}")
        sys.exit(1)
