import { Module } from '@nestjs/common';
import { AuthModule } from '../auth/auth.module';
import { ProjectsModule } from '../projects/projects.module';
import { S3Service } from './s3.service';
import { FilesController } from './files.controller';

@Module({
  imports: [AuthModule, ProjectsModule],
  providers: [S3Service],
  controllers: [FilesController],
  exports: [S3Service],
})
export class FilesModule {}
