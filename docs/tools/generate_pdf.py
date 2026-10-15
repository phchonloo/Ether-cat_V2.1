#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
自动使用Chrome生成PDF
"""

import subprocess
import os
import time
import sys

def generate_pdf():
    # 文件路径
    html_file = r"C:\Users\pc\Desktop\01_FCM4E1553_SQI_82_CTT_V1.4\docs\FCM4E1553_User_Manual_V1.0.html"
    pdf_file = r"C:\Users\pc\Desktop\01_FCM4E1553_SQI_82_CTT_V1.4\docs\FCM4E1553_User_Manual_V1.0.pdf"
    
    # Chrome路径
    chrome_path = r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe"
    
    # 检查HTML文件是否存在
    if not os.path.exists(html_file):
        print(f"✗ HTML文件不存在: {html_file}")
        return False
    
    # 检查Chrome是否存在
    if not os.path.exists(chrome_path):
        print(f"✗ Chrome浏览器不存在: {chrome_path}")
        return False
    
    print(f"✓ HTML文件: {html_file}")
    print(f"✓ Chrome浏览器: {chrome_path}")
    print(f"✓ 输出PDF: {pdf_file}")
    print()
    print("正在生成PDF，请稍候...")
    
    # 构建命令
    # Chrome headless模式打印PDF
    cmd = [
        chrome_path,
        '--headless',
        '--disable-gpu',
        '--run-all-compositor-stages-before-draw',
        '--print-to-pdf=' + pdf_file,
        '--print-to-pdf-no-header',
        '--no-margins',
        html_file
    ]
    
    try:
        # 执行命令
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=30
        )
        
        # 等待文件生成
        time.sleep(2)
        
        # 检查PDF是否生成
        if os.path.exists(pdf_file):
            file_size = os.path.getsize(pdf_file)
            print()
            print("=" * 60)
            print("✓ PDF生成成功！")
            print("=" * 60)
            print(f"文件位置: {pdf_file}")
            print(f"文件大小: {file_size / 1024:.1f} KB")
            print()
            print("提示：")
            print("- 可以直接打开查看")
            print("- 如需调整边距，请用浏览器手动打印")
            print("=" * 60)
            return True
        else:
            print("✗ PDF文件未生成")
            if result.stderr:
                print(f"错误信息: {result.stderr}")
            return False
            
    except subprocess.TimeoutExpired:
        print("✗ 生成超时（30秒）")
        return False
    except Exception as e:
        print(f"✗ 生成失败: {e}")
        return False

if __name__ == '__main__':
    success = generate_pdf()
    sys.exit(0 if success else 1)
