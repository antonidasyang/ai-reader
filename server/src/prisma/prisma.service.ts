import {
  Injectable,
  Logger,
  OnModuleInit,
  OnModuleDestroy,
} from '@nestjs/common';
import { PrismaClient } from '@prisma/client';

@Injectable()
export class PrismaService
  extends PrismaClient
  implements OnModuleInit, OnModuleDestroy
{
  private readonly logger = new Logger(PrismaService.name);

  async onModuleInit(): Promise<void> {
    try {
      await this.$connect();
    } catch (e) {
      // Boot even if the DB is unreachable (e.g. DATABASE_URL not configured
      // yet). /health reports db:down and queries fail per-request until the
      // database becomes reachable, instead of crash-looping the process.
      this.logger.error(`DB connect failed at boot: ${(e as Error).message}`);
    }
  }

  async onModuleDestroy(): Promise<void> {
    await this.$disconnect();
  }
}
