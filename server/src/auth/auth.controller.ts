import {
  BadRequestException,
  Body,
  Controller,
  Get,
  Post,
  Query,
  Res,
  UseGuards,
} from '@nestjs/common';
import type { Response } from 'express';
import { AuthService } from './auth.service';
import { CasService } from './cas.service';
import { PrismaService } from '../prisma/prisma.service';
import { RefreshDto } from './dto/refresh.dto';
import { JwtAuthGuard } from './jwt-auth.guard';
import { AuthUser, CurrentUser } from './current-user.decorator';

// Redirect target for the desktop loopback (the client's LocalHttpServer).
// Host is hardcoded to 127.0.0.1 so this can never be abused as an open redirect.
function loopback(port: number, params: Record<string, string>): string {
  const q = new URLSearchParams(params).toString();
  return `http://127.0.0.1:${port}/login/cas?${q}`;
}

function parsePort(port: string | undefined): number {
  const p = Number(port);
  if (!Number.isInteger(p) || p < 1024 || p > 65535) {
    throw new BadRequestException('invalid loopback port');
  }
  return p;
}

@Controller('auth')
export class AuthController {
  constructor(
    private readonly auth: AuthService,
    private readonly cas: CasService,
    private readonly prisma: PrismaService,
  ) {}

  // Step 1: the desktop app opens this with its loopback ?port (+ ?state nonce).
  @Get('cas/login')
  login(
    @Query('port') port: string,
    @Query('state') state: string,
    @Res() res: Response,
  ): void {
    const p = parsePort(port);
    res.redirect(this.cas.buildLoginUrl(p, state ?? ''));
  }

  // Step 2: CAS redirects here with a ticket; we validate, JIT-provision the
  // user, mint JWTs, and bounce the browser to the app's loopback with them.
  @Get('cas/callback')
  async callback(
    @Query('ticket') ticket: string,
    @Query('port') port: string,
    @Query('state') state: string,
    @Res() res: Response,
  ): Promise<void> {
    const p = parsePort(port);
    const st = state ?? '';
    if (!ticket) {
      res.redirect(loopback(p, { error: 'missing ticket', state: st }));
      return;
    }
    const r = await this.cas.validateTicket(ticket, p, st);
    if (!r.success || !r.user) {
      res.redirect(loopback(p, { error: r.error ?? 'cas failed', state: st }));
      return;
    }
    const userId = await this.auth.ensureCasUser({
      casUsername: r.user,
      displayName: r.displayName,
      email: r.email,
    });
    const tokens = await this.auth.issueTokens(userId, r.email ?? null);
    res.redirect(
      loopback(p, {
        access: tokens.accessToken,
        refresh: tokens.refreshToken,
        state: st,
      }),
    );
  }

  @Post('refresh')
  refresh(@Body() dto: RefreshDto) {
    return this.auth.refresh(dto.refreshToken);
  }

  @UseGuards(JwtAuthGuard)
  @Get('me')
  async me(@CurrentUser() user: AuthUser) {
    const u = await this.prisma.user.findUnique({
      where: { id: user.userId },
      select: { email: true, displayName: true },
    });
    return {
      userId: user.userId,
      email: u?.email ?? user.email,
      displayName: u?.displayName ?? null,
    };
  }
}
