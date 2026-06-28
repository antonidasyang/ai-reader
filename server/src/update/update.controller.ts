import {
  Controller,
  Get,
  NotFoundException,
  Param,
  Res,
} from '@nestjs/common';
import type { Response } from 'express';
import { UpdateService } from './update.service';
import { S3Service } from '../files/s3.service';

@Controller('update')
export class UpdateController {
  constructor(
    private readonly update: UpdateService,
    private readonly s3: S3Service,
  ) {}

  // Version info (consumed by the client's UpdateChecker).
  @Get('manifest')
  manifest() {
    return this.update.publicManifest();
  }

  // Streams the installer for a platform from MinIO through the API, so the
  // client only needs to reach this domain (not MinIO).
  @Get('download/:platform')
  async download(
    @Param('platform') platform: string,
    @Res() res: Response,
  ): Promise<void> {
    const info = await this.update.build(platform);
    if (!info) {
      throw new NotFoundException('no build for this platform');
    }
    const obj = await this.s3.getObject(info.key);
    const filename =
      info.filename ?? info.key.split('/').pop() ?? 'ai-reader-update';
    res.setHeader(
      'Content-Type',
      info.contentType ?? obj.contentType ?? 'application/octet-stream',
    );
    if (obj.contentLength) {
      res.setHeader('Content-Length', String(obj.contentLength));
    }
    res.setHeader('Content-Disposition', `attachment; filename="${filename}"`);
    obj.body.pipe(res);
  }
}
