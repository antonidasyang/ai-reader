import { createParamDecorator, ExecutionContext } from '@nestjs/common';

/** The authenticated principal attached to the request by JwtAuthGuard. */
export interface AuthUser {
  userId: string;
  email: string;
}

export const CurrentUser = createParamDecorator(
  (_data: unknown, ctx: ExecutionContext): AuthUser => {
    const req = ctx.switchToHttp().getRequest();
    return req.user as AuthUser;
  },
);
