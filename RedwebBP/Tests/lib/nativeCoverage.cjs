'use strict';

const { readdirSync } = require('node:fs');
const path = require('node:path');

function normalizeWindowsPath(value) {
  return path.win32.normalize(value.replaceAll('/', '\\')).toLowerCase();
}

function readAttribute(attributes, name) {
  const escapedName = name.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  const match = attributes.match(new RegExp(`(?:^|\\s)${escapedName}="([^"]*)"`));
  return match ? match[1] : null;
}

function summarizeNativeCoverage(report, sourceRoot) {
  if (typeof report !== 'string' || report.length === 0) {
    throw new TypeError('Native coverage report must be a non-empty Cobertura XML string.');
  }
  if (typeof sourceRoot !== 'string' || sourceRoot.length === 0) {
    throw new TypeError('Native coverage requires the RedwebBP source root.');
  }

  const normalizedRoot = normalizeWindowsPath(sourceRoot).replace(/\\+$/, '') + '\\';
  const lineHits = new Map();
  const sourceFiles = new Set();
  const classes = report.matchAll(/<class\b([^>]*)>([\s\S]*?)<\/class>/gi);

  for (const [, attributes, body] of classes) {
    const filename = readAttribute(attributes, 'filename');
    if (!filename) continue;

    const normalizedFile = normalizeWindowsPath(filename);
    if (!normalizedFile.startsWith(normalizedRoot)) continue;
    const relativeFile = normalizedFile.slice(normalizedRoot.length);
    if (/(^|\\)tests(\\|$)/i.test(relativeFile)) continue;
    if (!/\.(?:cpp|h|inl)$/i.test(relativeFile)) continue;

    sourceFiles.add(normalizedFile);
    for (const [, lineAttributes] of body.matchAll(/<line\b([^>]*)\/?\s*>/gi)) {
      const number = Number(readAttribute(lineAttributes, 'number'));
      const hits = Number(readAttribute(lineAttributes, 'hits'));
      if (!Number.isInteger(number) || number < 1 || !Number.isInteger(hits) || hits < 0) {
        throw new TypeError(`Native coverage contains an invalid line record for ${filename}.`);
      }

      const key = `${normalizedFile}:${number}`;
      lineHits.set(key, Math.max(lineHits.get(key) || 0, hits));
    }
  }

  if (lineHits.size === 0) {
    throw new Error('Native coverage report contains no executable RedwebBP production source lines.');
  }

  const covered = [...lineHits.values()].filter(hits => hits > 0).length;
  return {
    covered,
    total: lineHits.size,
    percent: (covered / lineHits.size) * 100,
    sourceFiles: [...sourceFiles].sort(),
    uncovered: [...lineHits.entries()].filter(([, hits]) => hits === 0).map(([line]) => line).sort(),
  };
}

function listProductionCppFiles(sourceRoot) {
  const root = path.resolve(sourceRoot);
  const files = [];
  const visit = directory => {
    for (const entry of readdirSync(directory, { withFileTypes: true })) {
      const fullPath = path.join(directory, entry.name);
      if (entry.isDirectory()) {
        if (!/^tests$/i.test(entry.name)) visit(fullPath);
      } else if (entry.isFile() && /\.cpp$/i.test(entry.name)) {
        files.push(normalizeWindowsPath(fullPath));
      }
    }
  };
  visit(root);
  return files.sort();
}

function verifyNativeCoverage(report, sourceRoot) {
  const summary = summarizeNativeCoverage(report, sourceRoot);
  const reportedFiles = new Set(summary.sourceFiles);
  const missingFiles = listProductionCppFiles(sourceRoot).filter(file => !reportedFiles.has(file));
  if (missingFiles.length > 0) {
    throw new Error(`Native coverage omitted production source files: ${missingFiles.join(', ')}.`);
  }
  if (summary.covered !== summary.total) {
    throw new Error(
      `Native production line coverage is ${summary.covered}/${summary.total} (${summary.percent.toFixed(2)}%); ` +
      `${summary.uncovered.length} executable lines remain uncovered.`
    );
  }
  return summary;
}

module.exports = { listProductionCppFiles, summarizeNativeCoverage, verifyNativeCoverage };
