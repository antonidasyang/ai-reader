import {
  ConflictException,
  ForbiddenException,
  Injectable,
  NotFoundException,
} from '@nestjs/common';
import { Role } from '@prisma/client';
import { PrismaService } from '../prisma/prisma.service';
import { CreateProjectDto } from './dto/create-project.dto';
import { UpdateProjectDto } from './dto/update-project.dto';
import { AddMemberDto, MemberRole, UpdateMemberDto } from './dto/member.dto';

const RANK: Record<Role, number> = { viewer: 0, editor: 1, owner: 2 };

@Injectable()
export class ProjectsService {
  constructor(private readonly prisma: PrismaService) {}

  // ── membership / permission helpers (reused by sync, files, events) ──

  async getRole(projectId: string, userId: string): Promise<Role | null> {
    const m = await this.prisma.projectMember.findUnique({
      where: { projectId_userId: { projectId, userId } },
    });
    return m?.role ?? null;
  }

  /** Throws unless the user is a member; returns their role. */
  async assertMember(projectId: string, userId: string): Promise<Role> {
    const role = await this.getRole(projectId, userId);
    if (!role) throw new ForbiddenException('not a member of this project');
    return role;
  }

  /** Throws unless the user can write (owner or editor). */
  async assertWriter(projectId: string, userId: string): Promise<void> {
    const role = await this.assertMember(projectId, userId);
    if (RANK[role] < RANK.editor) {
      throw new ForbiddenException('read-only members cannot modify this project');
    }
  }

  /** Throws unless the user is the/an owner. */
  async assertOwner(projectId: string, userId: string): Promise<void> {
    const role = await this.assertMember(projectId, userId);
    if (role !== Role.owner) {
      throw new ForbiddenException('owner role required');
    }
  }

  // ── projects ──────────────────────────────────────────────────────

  async create(userId: string, dto: CreateProjectDto) {
    return this.prisma.$transaction(async (tx) => {
      const project = await tx.project.create({
        data: {
          name: dto.name,
          description: dto.description ?? null,
          ownerId: userId,
        },
      });
      await tx.projectMember.create({
        data: { projectId: project.id, userId, role: Role.owner },
      });
      return project;
    });
  }

  /** Projects the user participates in, with their role. */
  async listMine(userId: string) {
    const memberships = await this.prisma.projectMember.findMany({
      where: { userId },
      include: { project: true },
      orderBy: { addedAt: 'asc' },
    });
    return memberships.map((m) => ({
      id: m.project.id,
      name: m.project.name,
      description: m.project.description,
      ownerId: m.project.ownerId,
      createdAt: m.project.createdAt,
      version: m.project.version,
      role: m.role,
    }));
  }

  async get(userId: string, projectId: string) {
    await this.assertMember(projectId, userId);
    const project = await this.prisma.project.findUnique({
      where: { id: projectId },
    });
    if (!project) throw new NotFoundException('project not found');
    return project;
  }

  async update(userId: string, projectId: string, dto: UpdateProjectDto) {
    await this.assertWriter(projectId, userId);
    return this.prisma.project.update({
      where: { id: projectId },
      data: {
        ...(dto.name !== undefined ? { name: dto.name } : {}),
        ...(dto.description !== undefined ? { description: dto.description } : {}),
      },
    });
  }

  async remove(userId: string, projectId: string) {
    await this.assertOwner(projectId, userId);
    await this.prisma.project.delete({ where: { id: projectId } });
    return { deleted: true };
  }

  // ── members ───────────────────────────────────────────────────────

  async listMembers(userId: string, projectId: string) {
    await this.assertMember(projectId, userId);
    const members = await this.prisma.projectMember.findMany({
      where: { projectId },
      include: { user: { select: { id: true, email: true, displayName: true } } },
      orderBy: { addedAt: 'asc' },
    });
    return members.map((m) => ({
      userId: m.userId,
      email: m.user.email,
      displayName: m.user.displayName,
      role: m.role,
      addedAt: m.addedAt,
    }));
  }

  async addMember(userId: string, projectId: string, dto: AddMemberDto) {
    await this.assertOwner(projectId, userId);
    const invitee = await this.prisma.user.findUnique({
      where: { email: dto.email },
    });
    if (!invitee) {
      throw new NotFoundException(
        'no registered user with that email (they must sign up first)',
      );
    }
    const existing = await this.getRole(projectId, invitee.id);
    if (existing) throw new ConflictException('already a member');
    await this.prisma.projectMember.create({
      data: { projectId, userId: invitee.id, role: dto.role as Role },
    });
    return { userId: invitee.id, email: invitee.email, role: dto.role };
  }

  async updateMember(
    userId: string,
    projectId: string,
    targetUserId: string,
    dto: UpdateMemberDto,
  ) {
    await this.assertOwner(projectId, userId);
    await this.ensureNotLastOwner(projectId, targetUserId, dto.role);
    await this.prisma.projectMember.update({
      where: { projectId_userId: { projectId, userId: targetUserId } },
      data: { role: dto.role as Role },
    });
    return { userId: targetUserId, role: dto.role };
  }

  async removeMember(userId: string, projectId: string, targetUserId: string) {
    await this.assertOwner(projectId, userId);
    await this.ensureNotLastOwner(projectId, targetUserId, 'viewer');
    await this.prisma.projectMember.delete({
      where: { projectId_userId: { projectId, userId: targetUserId } },
    });
    return { removed: true };
  }

  /** Guard against demoting/removing the only owner, which would orphan the project. */
  private async ensureNotLastOwner(
    projectId: string,
    targetUserId: string,
    newRole: MemberRole,
  ) {
    if (newRole === 'owner') return;
    const current = await this.getRole(projectId, targetUserId);
    if (current !== Role.owner) return;
    const owners = await this.prisma.projectMember.count({
      where: { projectId, role: Role.owner },
    });
    if (owners <= 1) {
      throw new ConflictException('a project must keep at least one owner');
    }
  }
}
