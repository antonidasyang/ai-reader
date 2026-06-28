import { Module } from '@nestjs/common';
import { FilesModule } from '../files/files.module';
import { UpdateService } from './update.service';
import { UpdateController } from './update.controller';

// Public, unauthenticated: clients check for updates before signing in.
@Module({
  imports: [FilesModule],
  providers: [UpdateService],
  controllers: [UpdateController],
})
export class UpdateModule {}
