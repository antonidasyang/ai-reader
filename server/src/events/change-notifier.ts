import { Injectable } from '@nestjs/common';
import { Observable, Subject } from 'rxjs';

export interface ProjectChange {
  projectId: string;
  version: string;
}

/**
 * In-process pub/sub bridge: SyncService emits a project change after a push;
 * the WebSocket gateway (phase 2.4) subscribes and fans it out to connected
 * members so their clients pull the delta.
 */
@Injectable()
export class ChangeNotifier {
  private readonly subject = new Subject<ProjectChange>();
  readonly changes: Observable<ProjectChange> = this.subject.asObservable();

  emit(projectId: string, version: string): void {
    this.subject.next({ projectId, version });
  }
}
