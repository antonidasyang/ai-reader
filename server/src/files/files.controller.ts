import {
  BadRequestException,
  Body,
  Controller,
  Get,
  Header,
  HttpCode,
  Param,
  PayloadTooLargeException,
  Post,
  Put,
  Query,
  Req,
  Res,
  UseGuards,
} from '@nestjs/common';
import type { Request, Response } from 'express';
import { createHash } from 'node:crypto';
import { Transform } from 'node:stream';
import { JwtAuthGuard } from '../auth/jwt-auth.guard';
import { AuthUser, CurrentUser } from '../auth/current-user.decorator';
import { ProjectsService } from '../projects/projects.service';
import { S3Service } from './s3.service';
import { UploadUrlDto } from './dto/upload-url.dto';

/** Matches the only key shape we ever mint: blobs/<sha256 hex>. */
const KEY_RE = /^blobs\/[a-f0-9]{64}$/;
const SHA_RE = /^[a-f0-9]{64}$/;
// Mirrors client_max_body_size in the front nginx. Rejecting on
// Content-Length costs nothing; letting nginx cut the stream mid-upload
// would leave a truncated blob under a hash that doesn't match it.
const MAX_BLOB_BYTES = 200 * 1024 * 1024;

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
    if (!key || !KEY_RE.test(key)) {
      throw new BadRequestException('invalid key');
    }
    const downloadUrl = await this.s3.presignDownload(key);
    return { key, downloadUrl };
  }

  // ── Proxied transfer ────────────────────────────────────────────
  // The routes above hand the client a presigned URL pointing at the
  // storage endpoint, which only works where that endpoint is
  // reachable — on our deployment it is a private LAN address, so
  // attachments silently failed outside the office. These routes move
  // the bytes through the API instead, the same way update downloads
  // already work: one public host, one auth mechanism, storage stays
  // internal. The presign routes stay for clients older than 1.1.12.

  /** Is this content already stored? Lets the client skip the upload. */
  @Get('blob-status')
  async blobStatus(
    @CurrentUser() u: AuthUser,
    @Param('id') projectId: string,
    @Query('sha256') sha256: string,
  ) {
    await this.projects.assertMember(projectId, u.userId);
    if (!sha256 || !SHA_RE.test(sha256)) {
      throw new BadRequestException('invalid sha256');
    }
    const key = this.s3.keyForSha(sha256);
    return { key, exists: await this.s3.exists(key) };
  }

  /**
   * Upload a blob's bytes. The body is streamed to storage (never
   * buffered here) while its sha256 is computed; a body that doesn't
   * hash to the key it claims is deleted again rather than left to
   * poison a content-addressed key forever.
   */
  @Put('blob')
  @HttpCode(200)
  async putBlob(
    @CurrentUser() u: AuthUser,
    @Param('id') projectId: string,
    @Query('sha256') sha256: string,
    @Req() req: Request,
  ) {
    await this.projects.assertWriter(projectId, u.userId);
    if (!sha256 || !SHA_RE.test(sha256)) {
      throw new BadRequestException('invalid sha256');
    }
    const declared = Number(req.headers['content-length']);
    if (!Number.isFinite(declared) || declared <= 0) {
      // Streaming to S3 needs a known length up front; the alternative
      // is buffering the whole PDF in memory.
      throw new BadRequestException('Content-Length required');
    }
    if (declared > MAX_BLOB_BYTES) {
      throw new PayloadTooLargeException('blob too large');
    }

    const key = this.s3.keyForSha(sha256);
    if (await this.s3.exists(key)) {
      req.resume(); // drain, otherwise the connection stalls
      return { key, deduped: true, byteSize: declared };
    }

    const hash = createHash('sha256');
    let bytes = 0;
    const hashing = new Transform({
      transform(chunk: Buffer, _enc, cb) {
        hash.update(chunk);
        bytes += chunk.length;
        cb(null, chunk);
      },
    });

    await this.s3.putObject(
      key,
      req.pipe(hashing),
      declared,
      (req.headers['content-type'] as string) ?? 'application/pdf',
    );

    const digest = hash.digest('hex');
    if (digest !== sha256 || bytes !== declared) {
      await this.s3.deleteObject(key).catch(() => undefined);
      throw new BadRequestException('content does not match sha256');
    }
    return { key, deduped: false, byteSize: bytes };
  }

  /** Stream a blob back through the API (no storage access needed). */
  @Get('blob')
  @Header('Cache-Control', 'private, max-age=31536000, immutable')
  async getBlob(
    @CurrentUser() u: AuthUser,
    @Param('id') projectId: string,
    @Query('key') key: string,
    @Res() res: Response,
  ) {
    await this.projects.assertMember(projectId, u.userId);
    if (!key || !KEY_RE.test(key)) {
      throw new BadRequestException('invalid key');
    }
    const obj = await this.s3.getObject(key);
    res.setHeader(
      'Content-Type',
      obj.contentType ?? 'application/octet-stream',
    );
    if (obj.contentLength) {
      res.setHeader('Content-Length', String(obj.contentLength));
    }
    obj.body.pipe(res);
  }
}
