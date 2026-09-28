'use strict';

const { defineApp } = require('redweb');
const { EchoRoute, VersionedEchoRoute } = require('./echo-route.cjs');

const port = Number(process.env.REDWEBBP_TEST_PORT || 18182);
const app = defineApp({ sockets: [EchoRoute, VersionedEchoRoute], port, bind: '127.0.0.1', signals: false, logger: null });

void app.run().then(() => {
  console.log(`REDBP_FIXTURE_READY ${port}`);
}).catch(error => {
  console.error(error);
  process.exitCode = 1;
});
