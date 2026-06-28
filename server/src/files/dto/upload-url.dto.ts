import { IsInt, IsOptional, IsString, Matches, Min } from 'class-validator';

export class UploadUrlDto {
  // Full-file SHA-256 (lowercase hex). The storage key is derived from it, so
  // identical files dedup to a single blob across projects and users.
  @Matches(/^[a-f0-9]{64}$/)
  sha256!: string;

  @IsOptional()
  @IsString()
  contentType?: string;

  @IsOptional()
  @IsInt()
  @Min(0)
  byteSize?: number;
}
