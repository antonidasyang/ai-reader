import { Module } from '@nestjs/common';
import { ChangeNotifier } from './change-notifier';

// Provides the in-process change bus now; the WebSocket gateway is added in
// phase 2.4 and subscribes to it.
@Module({
  providers: [ChangeNotifier],
  exports: [ChangeNotifier],
})
export class EventsModule {}
