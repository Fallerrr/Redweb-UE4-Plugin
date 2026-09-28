'use strict';

const { readFileSync } = require('node:fs');
const { validateAutomationReport } = require('./lib/automationReport.cjs');

const requiredTestPaths = [
  'RedwebBP.Unit.LegacyWireCodec',
  'RedwebBP.Integration.NativeTransportEcho',
];

try {
  const reportPath = process.argv[2];
  if (!reportPath) throw new Error('Pass the Unreal automation JSON report path.');
  const source = readFileSync(reportPath, 'utf8').replace(/^\uFEFF/, '');
  const report = JSON.parse(source);
  const summary = validateAutomationReport(report, requiredTestPaths);
  console.log(`Unreal automation report verified: ${summary.passed}/${summary.total} tests passed.`);
} catch (error) {
  console.error(`Unreal automation report verification failed: ${error.message}`);
  process.exitCode = 1;
}
