'use strict';

function validateAutomationReport(report, requiredTestPaths) {
  if (report === null || typeof report !== 'object' || Array.isArray(report)) {
    throw new TypeError('Unreal automation report must be an object.');
  }
  if (!Array.isArray(report.tests)) {
    throw new TypeError('Unreal automation report must contain a tests array.');
  }
  if (report.failed !== undefined && (typeof report.failed !== 'number' || report.failed !== 0)) {
    throw new Error('Unreal automation report has a nonzero or invalid failed-test count.');
  }
  if (!Array.isArray(requiredTestPaths) || requiredTestPaths.length === 0 ||
      requiredTestPaths.some(testPath => typeof testPath !== 'string' || testPath.length === 0)) {
    throw new TypeError('At least one valid required Unreal automation test path is required.');
  }

  const counts = new Map();
  for (const result of report.tests) {
    if (result === null || typeof result !== 'object' || Array.isArray(result)) {
      throw new TypeError('Unreal automation report contains an invalid test result.');
    }
    if (typeof result.fullTestPath !== 'string' || typeof result.state !== 'string') {
      throw new TypeError('Unreal automation results require a fullTestPath and state.');
    }
    if (result.state !== 'Success') {
      throw new Error(`Unreal automation test ${result.fullTestPath} finished with state ${result.state}.`);
    }
    counts.set(result.fullTestPath, (counts.get(result.fullTestPath) || 0) + 1);
  }

  for (const testPath of requiredTestPaths) {
    const count = counts.get(testPath) || 0;
    if (count === 0) throw new Error(`Required Unreal automation test did not run: ${testPath}.`);
    if (count !== 1) throw new Error(`Required Unreal automation test was reported more than once: ${testPath}.`);
  }

  return {
    total: report.tests.length,
    passed: report.tests.length,
    required: requiredTestPaths.length,
    tests: [...requiredTestPaths],
  };
}

module.exports = { validateAutomationReport };
