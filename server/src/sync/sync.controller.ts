import {
  Body,
  Controller,
  Get,
  Param,
  Post,
  Query,
  UseGuards,
} from '@nestjs/common';
import { JwtAuthGuard } from '../auth/jwt-auth.guard';
import { AuthUser, CurrentUser } from '../auth/current-user.decorator';
import { SyncService } from './sync.service';
import { PushDto } from './dto/push.dto';

function parseVersion(s: string | undefined): bigint {
  if (!s || !/^\d+$/.test(s)) return 0n;
  try {
    return BigInt(s);
  } catch {
    return 0n;
  }
}

@UseGuards(JwtAuthGuard)
@Controller('projects/:id')
export class SyncController {
  constructor(private readonly sync: SyncService) {}

  @Get('sync')
  pull(
    @CurrentUser() u: AuthUser,
    @Param('id') id: string,
    @Query('since') since?: string,
  ) {
    return this.sync.pull(u.userId, id, parseVersion(since));
  }

  @Post('push')
  push(
    @CurrentUser() u: AuthUser,
    @Param('id') id: string,
    @Body() dto: PushDto,
  ) {
    return this.sync.push(u.userId, id, dto.objects);
  }
}
