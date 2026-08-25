import {
  ConflictException,
  Injectable,
  PayloadTooLargeException,
  UnauthorizedException,
} from '@nestjs/common';
import { ConfigService } from '@nestjs/config';
import { PrismaService } from '../prisma/prisma.service';
import { parseByteSize } from '../sync/sync.service';

export interface PrefsView {
  /** Opaque client-owned blob. `{}` for a user who has never saved. */
  data: unknown;
  /** bigint rendered as a string, exactly as the sync endpoints do. */
  version: string;
}

export interface PutPrefsResult {
  version: string;
}

/** Default ceiling on one preferences blob. Settings are kilobytes of JSON;
 *  this is generous while keeping a buggy client from parking megabytes here. */
const DEFAULT_MAX_BYTES = 256 * 1024;

@Injectable()
export class UserPrefsService {
  private readonly maxBytes: number;

  constructor(
    private readonly prisma: PrismaService,
    config: ConfigService,
  ) {
    this.maxBytes = parseByteSize(
      config.get<string>('USER_PREFS_MAX_BYTES'),
      DEFAULT_MAX_BYTES,
    );
  }

  /** A user who has never saved gets the empty blob at version 0, not a 404. */
  async get(userId: string): Promise<PrefsView> {
    const row = await this.prisma.userPref.findUnique({ where: { userId } });
    return {
      data: row?.data ?? {},
      version: (row?.version ?? 0n).toString(),
    };
  }

  /**
   * Whole-blob replace under optimistic concurrency. `expectedVersion` must
   * equal the server's current version (0 when nothing is stored); otherwise
   * this throws 409 carrying { version, data } so the client can merge and
   * retry, the same shape SyncService.push returns for a conflicting object.
   */
  async put(
    userId: string,
    data: Record<string, unknown>,
    expectedVersion: string | undefined,
  ): Promise<PutPrefsResult> {
    const bytes = Buffer.byteLength(JSON.stringify(data), 'utf8');
    if (bytes > this.maxBytes) {
      throw new PayloadTooLargeException(
        `preferences payload is ${bytes} bytes; the limit is ${this.maxBytes}`,
      );
    }

    const expected = expectedVersion ? BigInt(expectedVersion) : 0n;

    return this.prisma.$transaction(async (tx) => {
      // Serialize concurrent writes for this user the way push serializes on
      // the project row: lock the parent, then read-check-write. Without it
      // two in-flight saves could both pass the version check.
      const owner = await tx.$queryRaw<
        { id: string }[]
      >`SELECT id FROM users WHERE id = ${userId}::uuid FOR UPDATE`;
      if (owner.length === 0) {
        throw new UnauthorizedException('user no longer exists');
      }

      const cur = await tx.userPref.findUnique({ where: { userId } });
      const current = cur?.version ?? 0n;
      if (current !== expected) {
        throw new ConflictException({
          version: current.toString(),
          data: cur?.data ?? {},
        });
      }

      const next = current + 1n;
      await tx.userPref.upsert({
        where: { userId },
        create: { userId, data: data as object, version: next },
        update: { data: data as object, version: next },
      });
      return { version: next.toString() };
    });
  }
}
