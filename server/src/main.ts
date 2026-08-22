import 'reflect-metadata';

// Postgres BIGINT (the per-project sync clock) comes back as a JS bigint, which
// JSON.stringify cannot serialize. Emit it as a string everywhere.
(BigInt.prototype as unknown as { toJSON: () => string }).toJSON = function (
  this: bigint,
) {
  return this.toString();
};

import { NestFactory } from '@nestjs/core';
import { ValidationPipe, Logger } from '@nestjs/common';
import { ConfigService } from '@nestjs/config';
import { NestExpressApplication } from '@nestjs/platform-express';
import { WsAdapter } from '@nestjs/platform-ws';
import { AppModule } from './app.module';

async function bootstrap() {
  const app = await NestFactory.create<NestExpressApplication>(AppModule);
  const config = app.get(ConfigService);

  // A push carries whole sync objects, and paper_data objects (a paper's
  // paragraph segmentation or its translations, deflated + base64) run to
  // hundreds of KB each. Express' 100 KB default would 413 those away. The
  // client batches its outbox well under this.
  app.useBodyParser('json', {
    limit: config.get<string>('BODY_LIMIT', '32mb'),
  });

  // Raw ws (not socket.io) for the change-notification gateway.
  app.useWebSocketAdapter(new WsAdapter(app));

  const origin = config.get<string>('CORS_ORIGIN', '*');
  app.enableCors({
    origin: origin === '*' ? true : origin.split(',').map((s) => s.trim()),
    credentials: true,
  });

  // whitelist strips unknown props; transform applies DTO types.
  app.useGlobalPipes(new ValidationPipe({ whitelist: true, transform: true }));

  const port = parseInt(config.get<string>('PORT', '3000'), 10);
  await app.listen(port);
  Logger.log(`ai-reader-server listening on :${port}`, 'Bootstrap');
}

void bootstrap();
