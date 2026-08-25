import { Body, Controller, Get, Put, UseGuards } from '@nestjs/common';
import { JwtAuthGuard } from '../auth/jwt-auth.guard';
import { AuthUser, CurrentUser } from '../auth/current-user.decorator';
import { UserPrefsService } from './user-prefs.service';
import { PutPrefsDto } from './dto/put-prefs.dto';

/**
 * GET/PUT /me/prefs -- the signed-in user's portable settings.
 *
 * The user always comes from the verified access token via @CurrentUser();
 * there is deliberately no user id in the path, the query or the body, so
 * one account can never read or write another's preferences.
 */
@UseGuards(JwtAuthGuard)
@Controller('me')
export class UserPrefsController {
  constructor(private readonly prefs: UserPrefsService) {}

  @Get('prefs')
  get(@CurrentUser() u: AuthUser) {
    return this.prefs.get(u.userId);
  }

  @Put('prefs')
  put(@CurrentUser() u: AuthUser, @Body() dto: PutPrefsDto) {
    return this.prefs.put(u.userId, dto.data, dto.expectedVersion);
  }
}
