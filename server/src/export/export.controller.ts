import { Controller, Get, Param, UseGuards } from '@nestjs/common';
import { JwtAuthGuard } from '../auth/jwt-auth.guard';
import { AuthUser, CurrentUser } from '../auth/current-user.decorator';
import { ProjectsService } from '../projects/projects.service';
import { PrismaService } from '../prisma/prisma.service';

/**
 * Whole-library export for migration / no-lock-in (req §2 "数据可迁移").
 * Returns project meta + members + every live object (items with their CSL-JSON,
 * collections, tags, ai_artifacts, ...). PDFs are fetched separately via their
 * content-addressed keys, so this JSON is a complete portable dump of the data.
 */
@UseGuards(JwtAuthGuard)
@Controller('projects/:id/export')
export class ExportController {
  constructor(
    private readonly projects: ProjectsService,
    private readonly prisma: PrismaService,
  ) {}

  @Get()
  async export(@CurrentUser() u: AuthUser, @Param('id') projectId: string) {
    await this.projects.assertMember(projectId, u.userId);
    const project = await this.projects.get(u.userId, projectId);
    const members = await this.projects.listMembers(u.userId, projectId);
    const rows = await this.prisma.syncObject.findMany({
      where: { projectId, deleted: false },
      orderBy: { version: 'asc' },
    });
    return {
      exportedAt: new Date().toISOString(),
      project: {
        id: project.id,
        name: project.name,
        description: project.description,
        version: project.version.toString(),
      },
      members,
      objects: rows.map((o) => ({
        id: o.id,
        type: o.type,
        data: o.data,
        version: o.version.toString(),
        updatedAt: o.updatedAt,
        updatedBy: o.updatedBy,
      })),
    };
  }
}
