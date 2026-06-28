import { Injectable, UnauthorizedException } from '@nestjs/common';
import { ConfigService } from '@nestjs/config';
import { JwtService } from '@nestjs/jwt';
import { PrismaService } from '../prisma/prisma.service';

export interface Tokens {
  accessToken: string;
  refreshToken: string;
}

export interface CasProfile {
  casUsername: string;
  displayName?: string;
  email?: string;
}

@Injectable()
export class AuthService {
  constructor(
    private readonly prisma: PrismaService,
    private readonly jwt: JwtService,
    private readonly config: ConfigService,
  ) {}

  /** Find-or-create the local user for a CAS principal (JIT provisioning). */
  async ensureCasUser(profile: CasProfile): Promise<string> {
    const existing = await this.prisma.user.findUnique({
      where: { casUsername: profile.casUsername },
      select: { id: true, displayName: true, email: true },
    });
    if (existing) {
      const patch: { displayName?: string; email?: string } = {};
      if (!existing.displayName && profile.displayName)
        patch.displayName = profile.displayName;
      if (!existing.email && profile.email) patch.email = profile.email;
      if (Object.keys(patch).length > 0) {
        await this.prisma.user.update({
          where: { id: existing.id },
          data: patch,
        });
      }
      return existing.id;
    }
    const created = await this.prisma.user.create({
      data: {
        casUsername: profile.casUsername,
        displayName: profile.displayName ?? null,
        email: profile.email ?? null,
      },
      select: { id: true },
    });
    return created.id;
  }

  async issueTokens(userId: string, email: string | null): Promise<Tokens> {
    const payload = { sub: userId, email: email ?? '' };
    const accessToken = await this.jwt.signAsync(payload, {
      secret: this.config.getOrThrow<string>('JWT_ACCESS_SECRET'),
      expiresIn: Number(this.config.get<string>('JWT_ACCESS_TTL', '900')),
    });
    const refreshToken = await this.jwt.signAsync(payload, {
      secret: this.config.getOrThrow<string>('JWT_REFRESH_SECRET'),
      expiresIn: Number(this.config.get<string>('JWT_REFRESH_TTL', '2592000')),
    });
    return { accessToken, refreshToken };
  }

  async refresh(
    refreshToken: string,
  ): Promise<Tokens & { user: { id: string; email: string | null; displayName: string | null } }> {
    let payload: { sub: string };
    try {
      payload = await this.jwt.verifyAsync(refreshToken, {
        secret: this.config.getOrThrow<string>('JWT_REFRESH_SECRET'),
      });
    } catch {
      throw new UnauthorizedException('invalid or expired refresh token');
    }
    const user = await this.prisma.user.findUnique({
      where: { id: payload.sub },
    });
    if (!user) throw new UnauthorizedException('user not found');
    const tokens = await this.issueTokens(user.id, user.email);
    return {
      ...tokens,
      user: { id: user.id, email: user.email, displayName: user.displayName },
    };
  }
}
