import { Module } from '@nestjs/common';
import { AuthModule } from '../auth/auth.module';
import { UserPrefsService } from './user-prefs.service';
import { UserPrefsController } from './user-prefs.controller';

@Module({
  imports: [AuthModule],
  providers: [UserPrefsService],
  controllers: [UserPrefsController],
  exports: [UserPrefsService],
})
export class UserPrefsModule {}
