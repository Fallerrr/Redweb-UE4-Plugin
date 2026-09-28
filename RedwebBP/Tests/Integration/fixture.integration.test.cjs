'use strict';

const assert = require('node:assert/strict');
const { once } = require('node:events');
const { test } = require('node:test');
const WebSocket = require('ws');
const { defineApp } = require('redweb');
const { EchoRoute, VersionedEchoRoute } = require('./echo-route.cjs');

test('Redweb 0.16.5 accepts and echoes the plugin baseline raw message over real WebSocket', { timeout: 10000 }, async t => {
  const app = defineApp({ sockets: [EchoRoute, VersionedEchoRoute], port: 0, bind: '127.0.0.1', signals: false, logger: null });
  await app.run();
  t.after(() => app.shutdown());

  const { port } = app.server.address();
  const socket = new WebSocket(`ws://127.0.0.1:${port}/socket`);
  t.after(() => socket.terminate());
  await once(socket, 'open');

  const response = once(socket, 'message');
  socket.send('{"type":"echo","text":"baseline"}');
  const [frame] = await response;
  assert.deepEqual(JSON.parse(frame.toString()), { type: 'echo', text: 'baseline' });

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

  const socket = new WebSocket(`ws://127.0.0.1:${app.server.address().port}/socket`);
  t.after(() => socket.terminate());
  await once(socket, 'open');

  const text = 'x'.repeat(128 * 1024);
  const response = once(socket, 'message');
  socket.send(JSON.stringify({ type: 'echo', text }));
  const [frame, isBinary] = await response;
  assert.equal(isBinary, false);
  assert.deepEqual(JSON.parse(frame.toString()), { type: 'echo', text });
});

test('the real Redweb route can send binary frames and a reasoned close', { timeout: 10000 }, async t => {
  const app = defineApp({ sockets: [EchoRoute], port: 0, bind: '127.0.0.1', signals: false, logger: null });
  await app.run();
  t.after(() => app.shutdown());

  const socket = new WebSocket(`ws://127.0.0.1:${app.server.address().port}/socket`);
  t.after(() => socket.terminate());
  await once(socket, 'open');

  const binaryFrame = once(socket, 'message');
  socket.send('{"type":"echo","fixtureCommand":"binary"}');
  const [bytes, isBinary] = await binaryFrame;
  assert.equal(isBinary, true);
  assert.deepEqual(bytes, Buffer.from([0xde, 0xad, 0xbe, 0xef]));

  const closed = once(socket, 'close');
  socket.send('{"type":"echo","fixtureCommand":"close"}');
  const [code, reason] = await closed;
  assert.equal(code, 4001);
  assert.equal(reason.toString(), 'fixture-close');
});

test('the real Redweb route delivers empty text and exposes an abrupt peer termination', { timeout: 10000 }, async t => {
  const app = defineApp({ sockets: [EchoRoute], port: 0, bind: '127.0.0.1', signals: false, logger: null });
  await app.run();
  t.after(() => app.shutdown());

  const socket = new WebSocket(`ws://127.0.0.1:${app.server.address().port}/socket`);
  t.after(() => socket.terminate());
  await once(socket, 'open');

  const emptyFrame = once(socket, 'message');
  socket.send('{"type":"echo","fixtureCommand":"empty"}');
  const [empty, isBinary] = await emptyFrame;
  assert.equal(isBinary, false);
  assert.equal(empty.length, 0);

  const closed = once(socket, 'close');
  socket.send('{"type":"echo","fixtureCommand":"abort"}');
  const [code] = await closed;
  assert.equal(code, 1006);
});
