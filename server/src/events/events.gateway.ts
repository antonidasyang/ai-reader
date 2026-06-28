import { Logger, OnModuleInit } from '@nestjs/common';
import { ConfigService } from '@nestjs/config';
import { JwtService } from '@nestjs/jwt';
import {
  ConnectedSocket,
  MessageBody,
  OnGatewayConnection,
  OnGatewayDisconnect,
  SubscribeMessage,
  WebSocketGateway,
} from '@nestjs/websockets';
import type { WebSocket } from 'ws';
import { ProjectsService } from '../projects/projects.service';
import { ChangeNotifier } from './change-notifier';

interface ClientState {
  userId: string | null;
  projects: Set<string>;
}

/**
 * Push channel for "your project changed, pull the delta".
 *
 * Protocol (JSON frames, {event,data}):
 *   -> { event: "auth",      data: { token } }            access token
 *   <- { event: "auth",      data: { ok } }
 *   -> { event: "subscribe", data: { projectId } }        membership-checked
 *   <- { event: "subscribe", data: { ok, projectId } }
 *   <- { event: "changed",   data: { projectId, version } }   on any push
 */
@WebSocketGateway()
export class EventsGateway
  implements OnGatewayConnection, OnGatewayDisconnect, OnModuleInit
{
  private readonly logger = new Logger(EventsGateway.name);
  private readonly clients = new Map<WebSocket, ClientState>();
  private readonly byProject = new Map<string, Set<WebSocket>>();

  constructor(
    private readonly jwt: JwtService,
    private readonly config: ConfigService,
    private readonly projects: ProjectsService,
    private readonly notifier: ChangeNotifier,
  ) {}

  onModuleInit(): void {
    this.notifier.changes.subscribe(({ projectId, version }) =>
      this.broadcast(projectId, version),
    );
  }

  handleConnection(client: WebSocket): void {
    this.clients.set(client, { userId: null, projects: new Set() });
  }

  handleDisconnect(client: WebSocket): void {
    const st = this.clients.get(client);
    if (st) {
      for (const pid of st.projects) {
        this.byProject.get(pid)?.delete(client);
      }
    }
    this.clients.delete(client);
  }

  @SubscribeMessage('auth')
  async auth(
    @ConnectedSocket() client: WebSocket,
    @MessageBody() body: { token?: string },
  ) {
    try {
      const payload = await this.jwt.verifyAsync(body?.token ?? '', {
        secret: this.config.getOrThrow<string>('JWT_ACCESS_SECRET'),
      });
      const st = this.clients.get(client);
      if (st) st.userId = payload.sub;
      return { event: 'auth', data: { ok: true } };
    } catch {
      return { event: 'auth', data: { ok: false } };
    }
  }

  @SubscribeMessage('subscribe')
  async subscribe(
    @ConnectedSocket() client: WebSocket,
    @MessageBody() body: { projectId?: string },
  ) {
    const st = this.clients.get(client);
    const projectId = body?.projectId;
    if (!st?.userId) {
      return { event: 'subscribe', data: { ok: false, error: 'unauthenticated' } };
    }
    if (!projectId || !(await this.projects.getRole(projectId, st.userId))) {
      return { event: 'subscribe', data: { ok: false, error: 'forbidden' } };
    }
    st.projects.add(projectId);
    if (!this.byProject.has(projectId)) this.byProject.set(projectId, new Set());
    this.byProject.get(projectId)!.add(client);
    return { event: 'subscribe', data: { ok: true, projectId } };
  }

  private broadcast(projectId: string, version: string): void {
    const set = this.byProject.get(projectId);
    if (!set || set.size === 0) return;
    const msg = JSON.stringify({ event: 'changed', data: { projectId, version } });
    for (const ws of set) {
      try {
        ws.send(msg);
      } catch (e) {
        this.logger.warn(`ws send failed: ${(e as Error).message}`);
      }
    }
  }
}
