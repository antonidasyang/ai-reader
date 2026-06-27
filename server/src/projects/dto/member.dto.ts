import { IsEmail, IsIn } from 'class-validator';

export type MemberRole = 'owner' | 'editor' | 'viewer';

export class AddMemberDto {
  @IsEmail()
  email!: string;

  @IsIn(['owner', 'editor', 'viewer'])
  role!: MemberRole;
}

export class UpdateMemberDto {
  @IsIn(['owner', 'editor', 'viewer'])
  role!: MemberRole;
}
