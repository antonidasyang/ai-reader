import { Module } from '@nestjs/common';
import { AuthModule } from '../auth/auth.module';
import { ProjectsService } from './projects.service';
import { ProjectsController } from './projects.controller';

@Module({
  imports: [AuthModule], // JwtAuthGuard
  providers: [ProjectsService],
  controllers: [ProjectsController],
  exports: [ProjectsService], // sync / files / events reuse the permission helpers
})
export class ProjectsModule {}
