import { Module } from '@nestjs/common';
import { AuthModule } from '../auth/auth.module';
import { ProjectsModule } from '../projects/projects.module';
import { ChangeNotifier } from './change-notifier';
import { EventsGateway } from './events.gateway';

// The in-process change bus + the WebSocket gateway that fans changes out to
// subscribed members. ChangeNotifier is exported so SyncService can emit to it.
@Module({
  imports: [AuthModule, ProjectsModule],
  providers: [ChangeNotifier, EventsGateway],
  exports: [ChangeNotifier],
})
export class EventsModule {}
