import 'reflect-metadata';
import { NestFactory } from '@nestjs/core';
import { ValidationPipe, Logger } from '@nestjs/common';
import { ConfigService } from '@nestjs/config';
import { AppModule } from './app.module';

async function bootstrap() {
  const app = await NestFactory.create(AppModule);
  const config = app.get(ConfigService);

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
