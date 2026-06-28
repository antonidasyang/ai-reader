import {
  Body,
  Controller,
  Delete,
  Get,
  Param,
  Patch,
  Post,
  UseGuards,
} from '@nestjs/common';
import { JwtAuthGuard } from '../auth/jwt-auth.guard';
import { AuthUser, CurrentUser } from '../auth/current-user.decorator';
import { ProjectsService } from './projects.service';
import { CreateProjectDto } from './dto/create-project.dto';
import { UpdateProjectDto } from './dto/update-project.dto';
import { AddMemberDto, UpdateMemberDto } from './dto/member.dto';

@UseGuards(JwtAuthGuard)
@Controller('projects')
export class ProjectsController {
  constructor(private readonly projects: ProjectsService) {}

  @Post()
  create(@CurrentUser() u: AuthUser, @Body() dto: CreateProjectDto) {
    return this.projects.create(u.userId, dto);
  }

  @Get()
  listMine(@CurrentUser() u: AuthUser) {
    return this.projects.listMine(u.userId);
  }

  @Get(':id')
  get(@CurrentUser() u: AuthUser, @Param('id') id: string) {
    return this.projects.get(u.userId, id);
  }

  @Patch(':id')
  update(
    @CurrentUser() u: AuthUser,
    @Param('id') id: string,
    @Body() dto: UpdateProjectDto,
  ) {
    return this.projects.update(u.userId, id, dto);
  }

  @Delete(':id')
  remove(@CurrentUser() u: AuthUser, @Param('id') id: string) {
    return this.projects.remove(u.userId, id);
  }

  // ── members ──
  @Get(':id/members')
  listMembers(@CurrentUser() u: AuthUser, @Param('id') id: string) {
    return this.projects.listMembers(u.userId, id);
  }

  @Post(':id/members')
  addMember(
    @CurrentUser() u: AuthUser,
    @Param('id') id: string,
    @Body() dto: AddMemberDto,
  ) {
    return this.projects.addMember(u.userId, id, dto);
  }

  @Patch(':id/members/:userId')
  updateMember(
    @CurrentUser() u: AuthUser,
    @Param('id') id: string,
    @Param('userId') targetUserId: string,
    @Body() dto: UpdateMemberDto,
  ) {
    return this.projects.updateMember(u.userId, id, targetUserId, dto);
  }

  @Delete(':id/members/:userId')
  removeMember(
    @CurrentUser() u: AuthUser,
    @Param('id') id: string,
    @Param('userId') targetUserId: string,
  ) {
    return this.projects.removeMember(u.userId, id, targetUserId);
  }
}
