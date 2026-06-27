import { Type } from 'class-transformer';
import {
  IsArray,
  IsBoolean,
  IsObject,
  IsOptional,
  IsString,
  IsUUID,
  Matches,
  ValidateNested,
} from 'class-validator';

export class PushObjectDto {
  @IsUUID()
  id!: string;

  @IsString()
  type!: string;

  @IsOptional()
  @IsObject()
  data?: Record<string, unknown>;

  @IsOptional()
  @IsBoolean()
  deleted?: boolean;

  // The version this change was based on (string, since it is a bigint).
  // Absent / "0" means the client believes this object is new on the server.
  @IsOptional()
  @Matches(/^\d+$/)
  expectedVersion?: string;
}

export class PushDto {
  @IsArray()
  @ValidateNested({ each: true })
  @Type(() => PushObjectDto)
  objects!: PushObjectDto[];
}
