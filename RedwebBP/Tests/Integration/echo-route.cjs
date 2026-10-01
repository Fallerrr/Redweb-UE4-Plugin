'use strict';

const { SocketRoute } = require('redweb');
const { defineSocketContract } = require('redweb/contract');
const { z } = require('zod');

const protocol = defineSocketContract('1', {
  echo: z.object({
    text: z.string().optional(),
    data: z.string().optional(),
    fixtureCommand: z.string().optional(),
  }).strict(),
});

const EchoHandler = protocol.handler('echo', (socket, payload, message) => {
    if (payload.fixtureCommand === 'binary') {
      socket.send(Buffer.from([0xde, 0xad, 0xbe, 0xef]), { binary: true });
      return;
    }
    if (payload.fixtureCommand === 'close') {
      socket.close(4001, 'fixture-close');
      return;
    }
    if (payload.fixtureCommand === 'empty') {
      socket.send('');
      return;
    }
    if (payload.fixtureCommand === 'abort') {
      socket.terminate();
      return;
    }
    if (payload.fixtureCommand === 'error') {
      socket.sendProtocolError('FIXTURE_ERROR', 'fixture-error', { requestId: message.requestId });
      return;
    }
    return protocol.send(socket, 'echo', { text: payload.text, data: payload.data }, { requestId: message.requestId });
});

class EchoRoute extends SocketRoute {
  constructor() {
    super({ path: '/socket', handlers: [EchoHandler], protocol: protocol.protocol, allowDuplicateConnections: true });
  }
}

class VersionedEchoRoute extends SocketRoute {
  constructor() {
    super({ path: '/current', handlers: [EchoHandler], protocol: protocol.protocol });
  }
}

module.exports = { EchoRoute, VersionedEchoRoute };
