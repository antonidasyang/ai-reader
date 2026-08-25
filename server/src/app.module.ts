import { Module } from '@nestjs/common';
import { ConfigModule } from '@nestjs/config';
import { PrismaModule } from './prisma/prisma.module';
import { HealthController } from './health/health.controller';
import { AuthModule } from './auth/auth.module';
import { ProjectsModule } from './projects/projects.module';
import { EventsModule } from './events/events.module';
import { SyncModule } from './sync/sync.module';
import { FilesModule } from './files/files.module';
import { ExportModule } from './export/export.module';
import { UpdateModule } from './update/update.module';
import { UserPrefsModule } from './user-prefs/user-prefs.module';

@Module({
  imports: [
    ConfigModule.forRoot({ isGlobal: true }),
    PrismaModule,
    AuthModule,
    ProjectsModule,
    EventsModule,
    SyncModule,
    FilesModule,
    ExportModule,
    UpdateModule,
    UserPrefsModule,
  ],
  controllers: [HealthController],
})
export class AppModule {}
