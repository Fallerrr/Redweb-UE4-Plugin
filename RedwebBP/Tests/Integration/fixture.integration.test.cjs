'use strict';

const assert = require('node:assert/strict');
const { once } = require('node:events');
const { test } = require('node:test');
const WebSocket = require('ws');
const { defineApp } = require('redweb');
const { EchoRoute, VersionedEchoRoute } = require('./echo-route.cjs');

test('Redweb 0.16.5 accepts protocol v1 and echoes the plugin-compatible envelope over real WebSocket', { timeout: 10000 }, async t => {
  const app = defineApp({ sockets: [EchoRoute, VersionedEchoRoute], port: 0, bind: '127.0.0.1', signals: false, logger: null });
  await app.run();
  t.after(() => app.shutdown());

  const { port } = app.server.address();
  const socket = new WebSocket(`ws://127.0.0.1:${port}/socket?redwebVersion=1`);
  t.after(() => socket.terminate());
  await once(socket, 'open');

  const response = once(socket, 'message');
  socket.send('{"v":"1","type":"echo","payload":{"text":"baseline"}}');
  const [frame] = await response;
  assert.deepEqual(JSON.parse(frame.toString()), { v: '1', type: 'echo', payload: { text: 'baseline' } });

  const errorResponse = once(socket, 'message');
  socket.send(JSON.stringify({ v: '1', type: 'echo', requestId: 'error-1', payload: { fixtureCommand: 'error' } }));
  const [errorFrame] = await errorResponse;
  assert.deepEqual(JSON.parse(errorFrame.toString()), {
    v: '1', type: 'error', requestId: 'error-1', error: { code: 'FIXTURE_ERROR', message: 'fixture-error' },
  });

  const unsupported = new WebSocket(`ws://127.0.0.1:${port}/current`);
  const status = await new Promise((resolve, reject) => {
    unsupported.once('unexpected-response', (_request, result) => {
      result.resume();
      resolve(result.statusCode);
    });
    unsupported.once('open', () => reject(new Error('A missing Redweb protocol version unexpectedly connected.')));
    unsupported.once('error', reject);
  });
  assert.equal(status, 426);

  const versioned = new WebSocket(`ws://127.0.0.1:${port}/current?redwebVersion=1`);
  t.after(() => versioned.terminate());
  await once(versioned, 'open');
  const currentResponse = once(versioned, 'message');
  versioned.send(JSON.stringify({ v: '1', type: 'echo', payload: { text: 'current' }, requestId: 'r1' }));
  const [currentFrame] = await currentResponse;
  assert.deepEqual(JSON.parse(currentFrame.toString()), {
    v: '1', type: 'echo', payload: { text: 'current' }, requestId: 'r1',
  });
});

test('the real Redweb route delivers a large text message without truncation', { timeout: 10000 }, async t => {
  const app = defineApp({ sockets: [EchoRoute], port: 0, bind: '127.0.0.1', signals: false, logger: null });
  await app.run();
  t.after(() => app.shutdown());

  const socket = new WebSocket(`ws://127.0.0.1:${app.server.address().port}/socket?redwebVersion=1`);
  t.after(() => socket.terminate());
  await once(socket, 'open');

  const text = 'x'.repeat(128 * 1024);
  const response = once(socket, 'message');
  socket.send(JSON.stringify({ v: '1', type: 'echo', payload: { text } }));
  const [frame, isBinary] = await response;
  assert.equal(isBinary, false);
  assert.deepEqual(JSON.parse(frame.toString()), { v: '1', type: 'echo', payload: { text } });
});

test('the real Redweb route can send binary frames and a reasoned close', { timeout: 10000 }, async t => {
  const app = defineApp({ sockets: [EchoRoute], port: 0, bind: '127.0.0.1', signals: false, logger: null });
  await app.run();
  t.after(() => app.shutdown());

  const socket = new WebSocket(`ws://127.0.0.1:${app.server.address().port}/socket?redwebVersion=1`);
  t.after(() => socket.terminate());
  await once(socket, 'open');

  const binaryFrame = once(socket, 'message');
  socket.send('{"v":"1","type":"echo","payload":{"fixtureCommand":"binary"}}');
  const [bytes, isBinary] = await binaryFrame;
  assert.equal(isBinary, true);
  assert.deepEqual(bytes, Buffer.from([0xde, 0xad, 0xbe, 0xef]));

  const closed = once(socket, 'close');
  socket.send('{"v":"1","type":"echo","payload":{"fixtureCommand":"close"}}');
  const [code, reason] = await closed;
  assert.equal(code, 4001);
  assert.equal(reason.toString(), 'fixture-close');
});

test('the real Redweb route delivers empty text and exposes an abrupt peer termination', { timeout: 10000 }, async t => {
  const app = defineApp({ sockets: [EchoRoute], port: 0, bind: '127.0.0.1', signals: false, logger: null });
  await app.run();
  t.after(() => app.shutdown());

  const socket = new WebSocket(`ws://127.0.0.1:${app.server.address().port}/socket?redwebVersion=1`);
  t.after(() => socket.terminate());
  await once(socket, 'open');

  const emptyFrame = once(socket, 'message');
  socket.send('{"v":"1","type":"echo","payload":{"fixtureCommand":"empty"}}');
  const [empty, isBinary] = await emptyFrame;
  assert.equal(isBinary, false);
  assert.equal(empty.length, 0);

  const closed = once(socket, 'close');
  socket.send('{"v":"1","type":"echo","payload":{"fixtureCommand":"abort"}}');
  const [code] = await closed;
  assert.equal(code, 1006);
});
