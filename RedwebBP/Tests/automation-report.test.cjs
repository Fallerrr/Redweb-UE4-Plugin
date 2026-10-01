'use strict';

const assert = require('node:assert/strict');
const { spawnSync } = require('node:child_process');
const { mkdtempSync, rmSync, writeFileSync } = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { test } = require('node:test');
const { validateAutomationReport } = require('./lib/automationReport.cjs');

const required = ['RedwebBP.Unit.LegacyWireCodec', 'RedwebBP.Integration.NativeTransportEcho'];
const successfulReport = () => ({
  devices: [{ deviceName: 'WindowsEditor', instance: 'local' }],
  succeeded: required.length,
  failed: 0,
  notRun: 0,
  tests: required.map(fullTestPath => ({ fullTestPath, state: 'Success' })),
});

test('accepts a report only when each required unit and real transport test passed exactly once', () => {
  assert.deepEqual(validateAutomationReport(successfulReport(), required), {
    total: 2,
    passed: 2,
    required: 2,
    tests: required,
  });
});

test('rejects malformed reports and missing required test records', () => {
  for (const report of [null, [], 'not a report', {}]) {
    assert.throws(() => validateAutomationReport(report, required));
  }
  assert.throws(() => validateAutomationReport(successfulReport(), undefined), /required Unreal automation test path/);
  assert.throws(() => validateAutomationReport(successfulReport(), []), /required Unreal automation test path/);
  assert.throws(() => validateAutomationReport(successfulReport(), ['']), /required Unreal automation test path/);
  assert.throws(() => validateAutomationReport({ tests: [null] }, required), /invalid test result/);
  assert.throws(() => validateAutomationReport({ tests: [[]] }, required), /invalid test result/);
  assert.throws(() => validateAutomationReport({ tests: [{}] }, required), /fullTestPath and state/);
  assert.throws(() => validateAutomationReport({ tests: [{ fullTestPath: required[0] }] }, required), /fullTestPath and state/);
  assert.throws(() => validateAutomationReport({ tests: [] }, required), /did not run/);
  assert.throws(() => validateAutomationReport({ ...successfulReport(), failed: 1 }, required), /failed-test count/);
  assert.throws(() => validateAutomationReport({ ...successfulReport(), failed: '0' }, required), /failed-test count/);
});

test('rejects failed, skipped, duplicate, and incomplete required tests', () => {
  for (const state of ['Fail', 'Skipped', 'NotRun', 'InProcess']) {
    assert.throws(() => validateAutomationReport({ tests: [{ fullTestPath: required[0], state }] }, [required[0]]), /finished with state/);
  }
  assert.throws(() => validateAutomationReport({
    tests: [...successfulReport().tests, successfulReport().tests[0]],
  }, required), /reported more than once/);
  assert.throws(() => validateAutomationReport({
    tests: [successfulReport().tests[0]],
  }, required), /did not run/);
});

test('the report command validates real JSON files and exits nonzero on missing or invalid input', () => {
  const root = mkdtempSync(path.join(os.tmpdir(), 'redwebbp-automation-report-'));
  const script = path.resolve(__dirname, 'verify-automation-report.cjs');
  const validPath = path.join(root, 'valid.json');
  const bomPath = path.join(root, 'bom.json');
  const malformedPath = path.join(root, 'malformed.json');
  const invalidPath = path.join(root, 'invalid.json');
  writeFileSync(validPath, JSON.stringify(successfulReport()));
  writeFileSync(bomPath, `\uFEFF${JSON.stringify(successfulReport())}`);
  writeFileSync(malformedPath, '{');
  writeFileSync(invalidPath, JSON.stringify({ tests: [] }));

  try {
    const valid = spawnSync(process.execPath, [script, validPath], { encoding: 'utf8' });
    assert.equal(valid.status, 0, valid.stderr);
    assert.match(valid.stdout, /2\/2 tests passed/);

    const bom = spawnSync(process.execPath, [script, bomPath], { encoding: 'utf8' });
    assert.equal(bom.status, 0, bom.stderr);
    assert.match(bom.stdout, /2\/2 tests passed/);

    const missingArgument = spawnSync(process.execPath, [script], { encoding: 'utf8' });
    assert.equal(missingArgument.status, 1);
    assert.match(missingArgument.stderr, /Pass the Unreal automation JSON report path/);

    for (const inputPath of [malformedPath, invalidPath, path.join(root, 'missing.json')]) {
      const invalid = spawnSync(process.execPath, [script, inputPath], { encoding: 'utf8' });
      assert.equal(invalid.status, 1);
      assert.match(invalid.stderr, /report verification failed/);
    }
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});
