# 如何生成PDF版本的说明书

## 📄 已生成的文件

### 1. Markdown源文件
```
docs/FCM4E1553_User_Manual_V1.0.md
```
- 可编辑的Markdown格式
- 用于版本控制和内容更新

### 2. HTML网页版本
```
docs/FCM4E1553_User_Manual_V1.0.html
```
- 精美排版的网页版本
- 可直接在浏览器中查看
- 包含打印优化的CSS样式

---

## 🖨️ 方法一：浏览器打印为PDF（推荐）

### 步骤：

1. **打开HTML文件**
   - 双击 `FCM4E1553_User_Manual_V1.0.html`
   - 或用浏览器打开（Chrome/Edge推荐）

2. **打印为PDF**
   - 按 `Ctrl + P`（或菜单：文件 → 打印）
   - 目标打印机选择：**另存为PDF**
   - 设置：
     - ✓ 页边距：默认
     - ✓ 背景图形：开启（保留表格颜色）
     - ✓ 页眉和页脚：可选
   
3. **保存**
   - 点击"保存"
   - 建议保存为：`FCM4E1553_User_Manual_V1.0.pdf`

### 效果：
- ✅ 完整保留格式和颜色
- ✅ 表格自动分页
- ✅ 链接保留（可点击）
- ✅ 书签自动生成（取决于浏览器）

---

## 🔧 方法二：使用在线转换工具

如果需要更专业的排版效果：

### 推荐工具：

1. **Typora** (桌面软件)
   - 下载：https://typora.io/
   - 打开 `.md` 文件
   - 文件 → 导出 → PDF
   - 优点：Markdown原生支持，排版优美

2. **Markdown PDF** (VS Code插件)
   - 安装插件：Markdown PDF
   - 右键 `.md` 文件 → Markdown PDF: Export (pdf)
   - 优点：无需额外软件

3. **在线工具**
   - https://www.markdowntopdf.com/
   - https://md2pdf.netlify.app/
   - 上传 `.md` 文件即可
   - 优点：无需安装

---

## 📐 方法三：使用Pandoc（专业方案）

如果需要安装Pandoc：

### Windows安装：

```bash
# 使用Chocolatey
choco install pandoc

# 或下载安装包
# https://pandoc.org/installing.html
```

### 生成PDF命令：

```bash
cd /c/Users/pc/Desktop/01_FCM4E1553_SQI_82_CTT_V1.4

pandoc docs/FCM4E1553_User_Manual_V1.0.md \
  -o docs/FCM4E1553_User_Manual_V1.0.pdf \
  --pdf-engine=xelatex \
  -V CJKmainfont="Microsoft YaHei" \
  -V geometry:margin=2cm \
  --toc \
  --toc-depth=3 \
  --number-sections
```

### 参数说明：
- `--pdf-engine=xelatex`：支持中文
- `-V CJKmainfont`：中文字体
- `--toc`：生成目录
- `--number-sections`：章节自动编号

---

## 🎨 当前HTML版本的特点

### 样式优化：
- ✅ **专业配色**：蓝色主题，适合技术文档
- ✅ **表格美化**：斑马纹，便于阅读
- ✅ **代码高亮**：灰色背景，等宽字体
- ✅ **警告框**：红色/黄色/绿色边框区分
- ✅ **打印优化**：自动分页，避免表格截断

### 打印设置：
- 页面尺寸：A4 (210mm × 297mm)
- 页边距：20mm
- 自动分页：避免标题和内容分离

---

## 📝 后续优化建议

### 1. 添加封面
在HTML顶部添加：
```html
<div class="cover">
    <h1>FCM4E1553</h1>
    <div class="subtitle">EtherCAT步进驱动器产品说明书</div>
    <div class="version">版本 V1.0 | 2026-10-15</div>
</div>
```

### 2. 添加公司Logo
在CSS中添加：
```css
.cover::before {
    content: '';
    display: block;
    width: 150px;
    height: 150px;
    margin: 0 auto 20px;
    background: url('logo.png') no-repeat center;
    background-size: contain;
}
```

### 3. 生成目录书签
使用Pandoc可自动生成PDF书签：
```bash
pandoc --toc --pdf-engine=xelatex input.md -o output.pdf
```

---

## 🚀 快速开始

### 最简单的方法（3步）：

1. 打开浏览器（Chrome/Edge）
2. 把 `FCM4E1553_User_Manual_V1.0.html` 拖到浏览器窗口
3. 按 `Ctrl + P`，选择"另存为PDF"，保存

### 预期结果：
- 约30-40页的PDF文档
- 包含完整目录和所有内容
- 表格和格式完整保留

---

## ✅ 文件清单

```
├── docs/
│   ├── FCM4E1553_User_Manual_V1.0.md        ← Markdown源文件
│   ├── FCM4E1553_User_Manual_V1.0.html      ← 网页版本（已生成）
│   └── FCM4E1553_User_Manual_V1.0.pdf       ← PDF版本（待生成）
│
└── tools/
    └── md2html.py                            ← 转换脚本
```

---

## 📞 需要帮助？

如果遇到问题：
1. 检查浏览器版本（建议Chrome 90+或Edge 90+）
2. 确认HTML文件编码为UTF-8
3. 打印预览中检查内容是否完整
4. 尝试不同的浏览器

---

**提示**：使用浏览器打印PDF是最快捷的方法，效果也非常好！
