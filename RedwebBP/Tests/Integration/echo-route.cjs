'use strict';

const { BaseHandler, SocketRoute } = require('redweb');
const { defineSocketContract } = require('redweb/contract');
const { z } = require('zod');

class EchoHandler extends BaseHandler {
  constructor() { super('echo'); }
  onMessage(socket, message) {
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
