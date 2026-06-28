import { Module } from '@nestjs/common';
import { AuthModule } from '../auth/auth.module';
import { ProjectsModule } from '../projects/projects.module';
import { ExportController } from './export.controller';

@Module({
  imports: [AuthModule, ProjectsModule],
  controllers: [ExportController],
})
export class ExportModule {}
