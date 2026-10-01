'use strict';

const { readFileSync } = require('node:fs');
const path = require('node:path');
const { verifyNativeCoverage } = require('./lib/nativeCoverage.cjs');

try {
  const reportPath = process.argv[2];
  if (!reportPath) throw new Error('Pass the native Cobertura coverage report path.');
  const report = readFileSync(reportPath, 'utf8');
  const sourceRoot = path.resolve(process.argv[3] || path.resolve(__dirname, '../Source/RedwebBP'));
  const summary = verifyNativeCoverage(report, sourceRoot);
  console.log(`Native RedwebBP production coverage: ${summary.covered}/${summary.total} executable lines (${summary.percent.toFixed(2)}%).`);
} catch (error) {
  console.error(`Native coverage verification failed: ${error.message}`);
  process.exitCode = 1;
}
