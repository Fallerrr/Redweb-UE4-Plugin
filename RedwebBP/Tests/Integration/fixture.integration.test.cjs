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
