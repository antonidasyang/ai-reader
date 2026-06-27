import { Injectable, NotFoundException } from '@nestjs/common';
import { PrismaService } from '../prisma/prisma.service';
import { ProjectsService } from '../projects/projects.service';
import { ChangeNotifier } from '../events/change-notifier';
import { PushObjectDto } from './dto/push.dto';

export interface SyncObjectView {
  id: string;
  type: string;
  data: unknown;
  version: string;
  deleted: boolean;
  updatedAt: Date;
  updatedBy: string | null;
}

export interface PullResult {
  newVersion: string;
  objects: SyncObjectView[];
}

export interface PushResult {
  newVersion: string;
  applied: string[];
  conflicts: { id: string; server: SyncObjectView }[];
}

function view(o: {
  id: string;
  type: string;
  data: unknown;
  version: bigint;
  deleted: boolean;
  updatedAt: Date;
  updatedBy: string | null;
}): SyncObjectView {
  return {
    id: o.id,
    type: o.type,
    data: o.data,
    version: o.version.toString(),
    deleted: o.deleted,
    updatedAt: o.updatedAt,
    updatedBy: o.updatedBy,
  };
}

@Injectable()
export class SyncService {
  constructor(
    private readonly prisma: PrismaService,
    private readonly projects: ProjectsService,
    private readonly notifier: ChangeNotifier,
  ) {}

  /** Incremental pull: everything (including tombstones) changed after `since`. */
  async pull(
    userId: string,
    projectId: string,
    since: bigint,
  ): Promise<PullResult> {
    await this.projects.assertMember(projectId, userId);
    const project = await this.prisma.project.findUnique({
      where: { id: projectId },
    });
    if (!project) throw new NotFoundException('project not found');

    const rows = await this.prisma.syncObject.findMany({
      where: { projectId, version: { gt: since } },
      orderBy: { version: 'asc' },
    });
    return { newVersion: project.version.toString(), objects: rows.map(view) };
  }

  /**
   * Optimistic push. Each object carries the version it was based on; if the
   * server's current version differs, it is a conflict (returned with the
   * server's value for the client to merge field-level-LWW and re-push).
   * Non-conflicting objects are applied atomically under one version bump.
   */
  async push(
    userId: string,
    projectId: string,
    items: PushObjectDto[],
  ): Promise<PushResult> {
    await this.projects.assertWriter(projectId, userId);

    if (items.length === 0) {
      const p = await this.prisma.project.findUnique({
        where: { id: projectId },
      });
      if (!p) throw new NotFoundException('project not found');
      return { newVersion: p.version.toString(), applied: [], conflicts: [] };
    }

    const result = await this.prisma.$transaction(async (tx) => {
      // Serialize concurrent pushes to this project so the clock and the
      // per-object version checks can't race (lost updates).
      await tx.$executeRaw`SELECT 1 FROM projects WHERE id = ${projectId}::uuid FOR UPDATE`;

      const ids = items.map((i) => i.id);
      const existing = await tx.syncObject.findMany({
        where: { id: { in: ids }, projectId },
      });
      const byId = new Map(existing.map((o) => [o.id, o]));

      const conflicts: { id: string; server: SyncObjectView }[] = [];
      const accepted: PushObjectDto[] = [];
      for (const it of items) {
        const cur = byId.get(it.id);
        const expected = it.expectedVersion ? BigInt(it.expectedVersion) : 0n;
        if (cur && cur.version !== expected) {
          conflicts.push({ id: it.id, server: view(cur) });
        } else {
          accepted.push(it);
        }
      }

      const project = await tx.project.findUnique({ where: { id: projectId } });
      let newVersion = project!.version;

      if (accepted.length > 0) {
        const bumped = await tx.project.update({
          where: { id: projectId },
          data: { version: { increment: 1 } },
        });
        newVersion = bumped.version;
        const now = new Date();
        for (const it of accepted) {
          await tx.syncObject.upsert({
            where: { id: it.id },
            create: {
              id: it.id,
              projectId,
              type: it.type,
              data: (it.data ?? {}) as object,
              version: newVersion,
              deleted: it.deleted ?? false,
              updatedAt: now,
              updatedBy: userId,
            },
            update: {
              type: it.type,
              data: (it.data ?? {}) as object,
              version: newVersion,
              deleted: it.deleted ?? false,
              updatedAt: now,
              updatedBy: userId,
            },
          });
        }
      }

      return {
        newVersion: newVersion.toString(),
        applied: accepted.map((a) => a.id),
        conflicts,
      };
    });

    if (result.applied.length > 0) {
      this.notifier.emit(projectId, result.newVersion);
    }
    return result;
  }
}
