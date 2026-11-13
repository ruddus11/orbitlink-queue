#!/usr/bin/env node
/*
 * docs/큐파트_설명자료.md 를 같은 내용의 .docx 로 변환한다. md가 원본이고 docx는 결과물이다.
 *
 * 실행: NODE_PATH=<docx가 설치된 폴더>/node_modules node docs/tools/build_docx.js
 * 준비: 아무 빈 폴더에서 npm i docx
 *
 * 설명자료에서 쓰는 문법만 처리한다: 제목(# ~ ###), 문단, 목록(-), 표, 코드 블록,
 * 그림(![설명](파일)), 굵게(**), 인라인 코드(`), 구분용 인용문(>).
 */
const fs = require("fs");
const path = require("path");
const {
  AlignmentType, BorderStyle, Document, Footer, HeadingLevel, ImageRun, LevelFormat, Packer, PageNumber,
  Paragraph, ShadingType, Table, TableCell, TableRow, TextRun, WidthType,
} = require("docx");

const DOCS = path.resolve(__dirname, "..");
const SOURCE = path.join(DOCS, "큐파트_설명자료.md");
const OUTPUT = path.join(DOCS, "큐파트_설명자료.docx");

const FONT = { ascii: "Arial", hAnsi: "Arial", eastAsia: "Malgun Gothic", cs: "Arial" };
const MONO = { ascii: "Courier New", hAnsi: "Courier New", eastAsia: "Malgun Gothic", cs: "Courier New" };
const PAGE_WIDTH = 11906, PAGE_HEIGHT = 16838, MARGIN = 1134;
const CONTENT_WIDTH = PAGE_WIDTH - MARGIN * 2;
const BLUE = "1F5FAD", INK = "1A1A1A", MUTED = "52514E", RULE = "C9C8C2", CODE_BG = "F4F3EF", HEAD_BG = "DCEAFA";

/* **굵게** 와 `코드` 를 TextRun 배열로 바꾼다. */
function inline(text, base = {}) {
  const runs = [];
  for (const part of text.split(/(\*\*[^*]+\*\*|`[^`]+`)/).filter(Boolean)) {
    if (part.startsWith("**")) runs.push(new TextRun({ ...base, text: part.slice(2, -2), bold: true }));
    else if (part.startsWith("`")) runs.push(new TextRun({ ...base, text: part.slice(1, -1), font: MONO, size: (base.size || 21) - 2 }));
    else runs.push(new TextRun({ ...base, text: part }));
  }
  return runs;
}

/* PNG 머리에서 가로, 세로 픽셀을 읽는다. */
function pngSize(file) {
  const b = fs.readFileSync(file);
  return { width: b.readUInt32BE(16), height: b.readUInt32BE(20) };
}

function image(alt, file) {
  const full = path.join(DOCS, file);
  const { width, height } = pngSize(full);
  const w = 640; /* 본문 폭에 맞춘 표시 크기(px) */
  return [
    new Paragraph({
      alignment: AlignmentType.CENTER, spacing: { before: 120, after: 60 }, keepNext: true,
      children: [new ImageRun({ type: "png", data: fs.readFileSync(full), transformation: { width: w, height: Math.round(w * height / width) },
        altText: { title: alt, description: alt, name: file } })],
    }),
    new Paragraph({ alignment: AlignmentType.CENTER, spacing: { after: 200 }, children: [new TextRun({ text: alt, size: 18, color: MUTED })] }),
  ];
}

function codeBlock(lines) {
  return lines.map((line, i) => new Paragraph({
    shading: { type: ShadingType.CLEAR, fill: CODE_BG, color: "auto" },
    spacing: { before: i === 0 ? 100 : 0, after: i === lines.length - 1 ? 180 : 0, line: 250 },
    keepLines: true, keepNext: i < lines.length - 1, /* 코드 블록이 페이지 사이에서 잘리지 않게 한다 */
    children: [new TextRun({ text: line || " ", font: MONO, size: 17, color: INK })],
  }));
}

function table(rows) {
  const count = rows[0].length;
  /* 글자 수에 비례해 열 너비를 나누되 너무 좁아지지 않게 한다. */
  const weights = rows[0].map((_, c) => Math.max(...rows.map((r) => Math.min(r[c].replace(/[`*]/g, "").length, 34))) + 3);
  const sum = weights.reduce((a, b) => a + b, 0);
  const widths = weights.map((w) => Math.floor(CONTENT_WIDTH * w / sum));
  widths[count - 1] += CONTENT_WIDTH - widths.reduce((a, b) => a + b, 0);
  const line = { style: BorderStyle.SINGLE, size: 4, color: RULE };
  const borders = { top: line, bottom: line, left: line, right: line };
  return [
    new Table({
      width: { size: CONTENT_WIDTH, type: WidthType.DXA }, columnWidths: widths,
      rows: rows.map((cells, r) => new TableRow({
        tableHeader: r === 0, cantSplit: true,
        children: cells.map((cell, c) => new TableCell({
          width: { size: widths[c], type: WidthType.DXA }, borders,
          shading: r === 0 ? { type: ShadingType.CLEAR, fill: HEAD_BG, color: "auto" } : undefined,
          margins: { top: 50, bottom: 50, left: 80, right: 80 },
          /* 마지막 줄 전까지 keepNext를 걸어 표가 페이지 사이에서 잘리지 않게 한다. */
          children: [new Paragraph({ keepNext: r < rows.length - 1, children: inline(cell, { size: 18, bold: r === 0 }) })],
        })),
      })),
    }),
    new Paragraph({ spacing: { after: 160 }, children: [] }),
  ];
}

function convert(markdown) {
  const out = [];
  const lines = markdown.split("\n");
  let i = 0;
  const heading = { 1: HeadingLevel.HEADING_1, 2: HeadingLevel.HEADING_2, 3: HeadingLevel.HEADING_3 };

  while (i < lines.length) {
    const line = lines[i];
    if (!line.trim()) { i++; continue; }

    if (line.startsWith("```")) {
      const code = [];
      for (i++; !lines[i].startsWith("```"); i++) code.push(lines[i]);
      i++;
      out.push(...codeBlock(code));
    } else if (/^#{1,3} /.test(line)) {
      const level = line.match(/^#+/)[0].length;
      out.push(new Paragraph({ heading: heading[level], pageBreakBefore: level === 1 && /^(2|4|6|7)\./.test(line.slice(2)), children: inline(line.slice(level + 1)) }));
      i++;
    } else if (line.startsWith("![")) {
      const m = line.match(/^!\[(.*)\]\((.*)\)/);
      out.push(...image(m[1], m[2]));
      i++;
    } else if (line.startsWith("|")) {
      const rows = [];
      for (; i < lines.length && lines[i].startsWith("|"); i++) {
        if (!/^\|[\s:|-]+\|$/.test(lines[i])) rows.push(lines[i].slice(1, -1).split(/(?<!\\)\|/).map((c) => c.trim().replace(/\\\|/g, "|")));
      }
      out.push(...table(rows));
    } else if (line.startsWith("- ")) {
      out.push(new Paragraph({ numbering: { reference: "bullets", level: 0 }, spacing: { after: 60 }, children: inline(line.slice(2)) }));
      i++;
    } else if (line.startsWith("> ")) {
      out.push(new Paragraph({
        spacing: { before: 60, after: 160 }, indent: { left: 200 },
        border: { left: { style: BorderStyle.SINGLE, size: 12, color: BLUE, space: 8 } },
        children: inline(line.slice(2), { color: MUTED }),
      }));
      i++;
    } else {
      out.push(new Paragraph({ spacing: { after: 130, line: 276 }, children: inline(line) }));
      i++;
    }
  }
  return out;
}

const headingStyle = (size, before, after, color) => ({
  run: { font: FONT, size, bold: true, color }, paragraph: { spacing: { before, after }, keepNext: true },
});

const doc = new Document({
  creator: "OrbitLink Team", title: "OrbitLink 메시지 큐 파트 설명자료",
  styles: {
    default: {
      document: { run: { font: FONT, size: 21, color: INK } },
      heading1: headingStyle(32, 200, 160, BLUE),
      heading2: headingStyle(25, 280, 110, INK),
      heading3: headingStyle(22, 200, 80, INK),
    },
  },
  numbering: { config: [{ reference: "bullets", levels: [{ level: 0, format: LevelFormat.BULLET, text: "•", alignment: AlignmentType.LEFT,
    style: { paragraph: { indent: { left: 400, hanging: 240 } } } }] }] },
  sections: [{
    properties: { page: { size: { width: PAGE_WIDTH, height: PAGE_HEIGHT }, margin: { top: MARGIN, bottom: MARGIN, left: MARGIN, right: MARGIN } } },
    footers: { default: new Footer({ children: [new Paragraph({ alignment: AlignmentType.CENTER,
      children: [new TextRun({ children: [PageNumber.CURRENT], size: 17, color: MUTED })] })] }) },
    children: convert(fs.readFileSync(SOURCE, "utf8")),
  }],
});

Packer.toBuffer(doc).then((buffer) => {
  fs.writeFileSync(OUTPUT, buffer);
  console.log("저장:", OUTPUT);
});
