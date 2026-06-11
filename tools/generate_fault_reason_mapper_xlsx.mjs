import fs from 'node:fs/promises';
import path from 'node:path';
import { SpreadsheetFile, Workbook } from '@oai/artifact-tool';

const cwd = process.cwd();
const srcPath = path.join(cwd, 'core/base/common/fault_reason_mapper.cpp');
const text = await fs.readFile(srcPath, 'utf8');

const sections = [
  ['BuildStandbyRuleMap', '常态故障', 1, 'standby', 0x30000],
  ['BuildStartRuleMap', '启动失败', 2, 'start', 0x10000],
  ['BuildChargingRuleMap', '充电中故障', 3, 'charging', 0x20000],
];

const headers = ['故障类型', 'FaultType', '类型值', '点位Key', '原始故障代码HEX', '原始故障代码DEC', '统一原因码HEX', '统一原因码DEC', '是否作为故障', '中文描述'];
const rows = [];

for (const [func, cname, typeVal, prefix, stage] of sections) {
  const bodyRe = new RegExp('std::map<unsigned int, FaultPointRule>\\s+' + func + '\\(\\)\\s*\\{(?<body>[\\s\\S]*?)\\n\\s*return m;\\s*\\}');
  const match = text.match(bodyRe);
  if (!match) {
    throw new Error(`missing ${func}`);
  }
  const pat = /m\[(0x[0-9A-Fa-f]+)\]\s*=\s*MakeRule\(\s*(true|false)\s*,\s*FaultTypeDef::([A-Z_]+)\s*,\s*(?:(makeReason\(\s*FaultTypeDef::([A-Z_]+)\s*,\s*(0x[0-9A-Fa-f]+)\s*\))|(0))\s*,\s*"((?:[^"\\]|\\.)*)"\s*\)\s*;/g;
  for (const item of match.groups.body.matchAll(pat)) {
    const raw = Number.parseInt(item[1], 16);
    const reason = item[7] === '0' ? 0 : (stage | (Number.parseInt(item[6], 16) & 0xffff));
    rows.push([
      cname,
      item[3],
      typeVal,
      `${prefix}_${(raw & 0xffff).toString(16).toUpperCase().padStart(4, '0')}`,
      `0x${raw.toString(16).toUpperCase().padStart(4, '0')}`,
      raw,
      reason ? `0x${reason.toString(16).toUpperCase().padStart(5, '0')}` : '0x00000',
      reason,
      item[2] === 'true' ? '是' : '否',
      item[8].replace(/\\"/g, '"'),
    ]);
  }
}

const workbook = Workbook.create();

function writeTable(sheetName, dataRows) {
  const sheet = workbook.worksheets.add(sheetName);
  sheet.showGridLines = false;
  const data = [headers, ...dataRows];
  const range = sheet.getRangeByIndexes(0, 0, data.length, headers.length);
  range.values = data;
  range.format.borders = { preset: 'all', style: 'thin', color: '#D9D9D9' };
  range.format.font = { name: 'Microsoft YaHei', size: 10, color: '#1F2937' };
  range.format.wrapText = true;
  range.format.verticalAlignment = 'top';

  const header = sheet.getRangeByIndexes(0, 0, 1, headers.length);
  header.format.fill = { color: '#1F4E79' };
  header.format.font = { name: 'Microsoft YaHei', size: 10, color: '#FFFFFF', bold: true };
  header.format.horizontalAlignment = 'center';
  sheet.freezePanes.freezeRows(1);

  [95, 110, 55, 115, 135, 110, 120, 110, 95, 520].forEach((width, index) => {
    sheet.getRangeByIndexes(0, index, data.length, 1).format.columnWidthPx = width;
  });
}

writeTable('完整映射', rows);
for (const [, cname] of sections) {
  writeTable(cname, rows.filter((row) => row[0] === cname));
}

const summary = workbook.worksheets.add('统计说明');
summary.showGridLines = false;
summary.getRange('A1:F1').values = [['故障类型', '总条数', '作为故障', '非故障', '阶段高位', '点位前缀']];
summary.getRangeByIndexes(1, 0, sections.length, 6).values = sections.map(([, cname,, prefix, stage]) => {
  const current = rows.filter((row) => row[0] === cname);
  return [
    cname,
    current.length,
    current.filter((row) => row[8] === '是').length,
    current.filter((row) => row[8] === '否').length,
    `0x${stage.toString(16).toUpperCase()}`,
    prefix,
  ];
});
summary.getRange('A1:F4').format.borders = { preset: 'all', style: 'thin', color: '#D9D9D9' };
summary.getRange('A1:F1').format.fill = { color: '#1F4E79' };
summary.getRange('A1:F1').format.font = { name: 'Microsoft YaHei', size: 10, color: '#FFFFFF', bold: true };
summary.getRange('A1:F4').format.font = { name: 'Microsoft YaHei', size: 10, color: '#1F2937' };
summary.getRange('A:F').format.columnWidthPx = 110;
summary.freezePanes.freezeRows(1);

await fs.mkdir(path.join(cwd, 'docs'), { recursive: true });
const outputPath = path.join(cwd, 'docs/fault_reason_mapper_故障映射表.xlsx');
const output = await SpreadsheetFile.exportXlsx(workbook);
await output.save(outputPath);

const inspect = await workbook.inspect({
  kind: 'table',
  range: '统计说明!A1:F4',
  include: 'values',
  tableMaxRows: 5,
  tableMaxCols: 6,
});

console.log(JSON.stringify({ outputPath, total: rows.length, inspect: inspect.ndjson }, null, 2));
