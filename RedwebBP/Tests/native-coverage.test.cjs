'use strict';

const assert = require('node:assert/strict');
const { spawnSync } = require('node:child_process');
const { mkdtempSync, mkdirSync, rmSync, writeFileSync } = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { test } = require('node:test');
const { listProductionCppFiles, summarizeNativeCoverage, verifyNativeCoverage } = require('./lib/nativeCoverage.cjs');

function coverageClass(filename, lines) {
  const entries = lines.map(([number, hits]) => `<line number="${number}" hits="${hits}" branch="False" />`).join('');
  return `<class filename="${filename}" name="test"><methods><method name="f"><lines>${entries}</lines></method></methods><lines>${entries}</lines></class>`;
}

function makeSourceTree() {
  const root = mkdtempSync(path.join(os.tmpdir(), 'redwebbp-native-coverage-'));
  const sourceRoot = path.join(root, 'Source', 'RedwebBP');
  mkdirSync(path.join(sourceRoot, 'Private', 'Tests'), { recursive: true });
  writeFileSync(path.join(sourceRoot, 'Private', 'Runtime.cpp'), 'void Runtime() {}\n');
  writeFileSync(path.join(sourceRoot, 'Private', 'Tests', 'RuntimeTests.cpp'), 'void Tests() {}\n');
  return { root, sourceRoot };
}

test('summarizes deduplicated production lines and excludes external and test sources', () => {
  const { root, sourceRoot } = makeSourceTree();
  const runtime = path.join(sourceRoot, 'Private', 'Runtime.cpp');
  const testFile = path.join(sourceRoot, 'Private', 'Tests', 'RuntimeTests.cpp');
  const unsupportedFileType = path.join(sourceRoot, 'Private', 'Notes.txt');
  const external = 'C:\\Epic\\Engine\\Runtime\\Core\\Core.cpp';
  const xml = `<coverage>${coverageClass(runtime, [[1, 0], [2, 1]])}${coverageClass(runtime, [[1, 2]])}${coverageClass(testFile, [[1, 0]])}${coverageClass(unsupportedFileType, [[1, 0]])}${coverageClass(external, [[1, 0]])}<class name="missing-filename"><lines><line number="1" hits="0" /></lines></class></coverage>`;

  try {
    assert.deepEqual(summarizeNativeCoverage(xml, sourceRoot), {
      covered: 2,
      total: 2,
      percent: 100,
      sourceFiles: [path.win32.normalize(runtime).toLowerCase()],
      uncovered: [],
    });
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});

test('rejects empty, malformed, and out-of-scope reports', () => {
  const { root, sourceRoot } = makeSourceTree();
  try {
    assert.throws(() => summarizeNativeCoverage('', sourceRoot), /non-empty Cobertura/);
    assert.throws(() => summarizeNativeCoverage('<coverage></coverage>', sourceRoot), /no executable/);
    assert.throws(() => summarizeNativeCoverage('<class filename="x.cpp"><line number="1" hits="0" /></class>', sourceRoot), /no executable/);
    assert.throws(() => summarizeNativeCoverage('<coverage></coverage>', ''), /source root/);
    assert.throws(() => summarizeNativeCoverage(1, sourceRoot), /Cobertura XML string/);
    assert.throws(() => summarizeNativeCoverage(`<coverage>${coverageClass(path.join(sourceRoot, 'Private', 'Runtime.cpp'), [['bad', 0]])}</coverage>`, sourceRoot), /invalid line record/);
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});

test('requires all production C++ files and every measured line to be covered', () => {
  const { root, sourceRoot } = makeSourceTree();
  const runtime = path.join(sourceRoot, 'Private', 'Runtime.cpp');
  const testFile = path.join(sourceRoot, 'Private', 'Tests', 'RuntimeTests.cpp');
  try {
    assert.deepEqual(listProductionCppFiles(sourceRoot), [path.win32.normalize(runtime).toLowerCase()]);
    const complete = `<coverage>${coverageClass(runtime, [[1, 1], [2, 2]])}${coverageClass(testFile, [[1, 0]])}</coverage>`;
    assert.equal(verifyNativeCoverage(complete, sourceRoot).percent, 100);

    const uncovered = `<coverage>${coverageClass(runtime, [[1, 0]])}</coverage>`;
    assert.throws(() => verifyNativeCoverage(uncovered, sourceRoot), /Native production line coverage is 0\/1/);

    const missingFile = path.join(sourceRoot, 'Public', 'Missing.cpp');
    mkdirSync(path.dirname(missingFile), { recursive: true });
    writeFileSync(missingFile, 'void Missing() {}\n');
    assert.throws(() => verifyNativeCoverage(complete, sourceRoot), /omitted production source files/);
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});

test('the coverage report command validates real Cobertura files and reports failures', () => {
  const { root, sourceRoot } = makeSourceTree();
  const runtime = path.join(sourceRoot, 'Private', 'Runtime.cpp');
  const reportPath = path.join(root, 'coverage.xml');
  const malformedPath = path.join(root, 'malformed.xml');
  const script = path.resolve(__dirname, 'verify-native-coverage.cjs');
  writeFileSync(reportPath, `<coverage>${coverageClass(runtime, [[1, 1], [2, 4]])}</coverage>`);
  writeFileSync(malformedPath, '<coverage></coverage>');

  try {
    const valid = spawnSync(process.execPath, [script, reportPath, sourceRoot], { encoding: 'utf8' });
    assert.equal(valid.status, 0, valid.stderr);
    assert.match(valid.stdout, /2\/2 executable lines \(100\.00%\)/);

    const missingArgument = spawnSync(process.execPath, [script], { encoding: 'utf8' });
    assert.equal(missingArgument.status, 1);
    assert.match(missingArgument.stderr, /Pass the native Cobertura coverage report path/);

    const invalid = spawnSync(process.execPath, [script, malformedPath, sourceRoot], { encoding: 'utf8' });
    assert.equal(invalid.status, 1);
    assert.match(invalid.stderr, /Native coverage verification failed/);

    const defaultSourceRoot = spawnSync(process.execPath, [script, malformedPath, ''], { encoding: 'utf8' });
    assert.equal(defaultSourceRoot.status, 1);
    assert.match(defaultSourceRoot.stderr, /Native coverage verification failed/);
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});
