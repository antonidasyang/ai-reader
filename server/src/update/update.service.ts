import { Injectable, Logger } from '@nestjs/common';
import { ConfigService } from '@nestjs/config';
import { readFile } from 'node:fs/promises';

interface PlatformBuild {
  key: string; // MinIO object key (e.g. updates/ai-reader-0.3.0-arm64.dmg)
  filename?: string; // suggested download filename
  contentType?: string;
}

interface RawManifest {
  latestVersion: string;
  releaseNotes?: string;
  releaseDate?: string;
  platforms: Record<string, PlatformBuild>;
}

// The release manifest lives on the server (a JSON file the admin edits when
// publishing a new build); the installer binaries live in MinIO. The client
// only ever sees download URLs that point back at this API, so it never needs
// to reach MinIO directly.
@Injectable()
export class UpdateService {
  private readonly logger = new Logger(UpdateService.name);

  constructor(private readonly config: ConfigService) {}

  private path(): string {
    return this.config.get<string>(
      'UPDATE_MANIFEST_PATH',
      'update-manifest.json',
    );
  }

  private async raw(): Promise<RawManifest | null> {
    try {
      return JSON.parse(await readFile(this.path(), 'utf8')) as RawManifest;
    } catch (e) {
      this.logger.warn(`update manifest unavailable: ${(e as Error).message}`);
      return null;
    }
  }

  /** Client-facing manifest: per-platform downloadUrl proxied through this API. */
  async publicManifest(): Promise<{
    latestVersion: string;
    releaseNotes: string;
    releaseDate: string;
    platforms: Record<string, { downloadUrl: string }>;
  }> {
    const m = await this.raw();
    if (!m) {
      return { latestVersion: '', releaseNotes: '', releaseDate: '', platforms: {} };
    }
    const host = this.config
      .getOrThrow<string>('PLATFORM_HOST')
      .replace(/\/+$/, '');
    const platforms: Record<string, { downloadUrl: string }> = {};
    for (const [plat, info] of Object.entries(m.platforms ?? {})) {
      if (info?.key) {
        platforms[plat] = { downloadUrl: `${host}/update/download/${plat}` };
      }
    }
    return {
      latestVersion: m.latestVersion ?? '',
      releaseNotes: m.releaseNotes ?? '',
      releaseDate: m.releaseDate ?? '',
      platforms,
    };
  }

  async build(platform: string): Promise<PlatformBuild | null> {
    const m = await this.raw();
    const info = m?.platforms?.[platform];
    return info?.key ? info : null;
  }
}
