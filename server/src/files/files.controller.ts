import {
  BadRequestException,
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
import { ProjectsService } from '../projects/projects.service';
import { S3Service } from './s3.service';
import { UploadUrlDto } from './dto/upload-url.dto';

@UseGuards(JwtAuthGuard)
@Controller('projects/:id/attachments')
export class FilesController {
  constructor(
    private readonly s3: S3Service,
    private readonly projects: ProjectsService,
  ) {}

  /**
   * Ask where to upload a PDF. If a blob with this sha256 already exists,
   * returns { exists: true } and no upload is needed ("instant upload").
   */
  @Post('upload-url')
  async uploadUrl(
    @CurrentUser() u: AuthUser,
    @Param('id') projectId: string,
    @Body() dto: UploadUrlDto,
  ) {
    await this.projects.assertWriter(projectId, u.userId);
    const key = this.s3.keyForSha(dto.sha256);
    if (await this.s3.exists(key)) {
      return { key, exists: true };
    }
    const uploadUrl = await this.s3.presignUpload(key, dto.contentType);
    return { key, exists: false, uploadUrl };
  }

  /** Presigned GET url for a blob the client already knows the key of. */
  @Get('download-url')
  async downloadUrl(
    @CurrentUser() u: AuthUser,
    @Param('id') projectId: string,
    @Query('key') key: string,
  ) {
    await this.projects.assertMember(projectId, u.userId);
    if (!key || !/^blobs\/[a-f0-9]{64}$/.test(key)) {
      throw new BadRequestException('invalid key');
    }
    const downloadUrl = await this.s3.presignDownload(key);
    return { key, downloadUrl };
  }
}
