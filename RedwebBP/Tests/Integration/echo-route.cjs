'use strict';

const { BaseHandler, SocketRoute } = require('redweb');
const { defineSocketContract } = require('redweb/contract');
const { z } = require('zod');

class EchoHandler extends BaseHandler {
  constructor() { super('echo'); }
  onMessage(socket, message) {
    if (message.fixtureCommand === 'binary') {
      socket.send(Buffer.from([0xde, 0xad, 0xbe, 0xef]), { binary: true });
      return;
    }
    if (message.fixtureCommand === 'close') {
      socket.close(4001, 'fixture-close');
      return;
    }
    if (message.fixtureCommand === 'empty') {
      socket.send('');
      return;
    }
    if (message.fixtureCommand === 'abort') {
      socket.terminate();
      return;
    }
    socket.sendJson({ type: 'echo', text: message.text, data: message.data });
  }
}

class EchoRoute extends SocketRoute {
  constructor() {
    super({ path: '/socket', handlers: [EchoHandler], allowDuplicateConnections: true });
  }
}

const protocol = defineSocketContract('1', {
  echo: z.object({ text: z.string() }).strict(),
});
const VersionedEcho = protocol.handler('echo', (socket, payload, message) =>
  protocol.send(socket, 'echo', payload, { requestId: message.requestId }),
);

class VersionedEchoRoute extends SocketRoute {
  constructor() {
    super({ path: '/current', handlers: [VersionedEcho], protocol: protocol.protocol });
  }
}

module.exports = { EchoRoute, VersionedEchoRoute };
