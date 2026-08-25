import { Transform } from 'class-transformer';
import { IsObject, IsOptional, Matches } from 'class-validator';

export class PutPrefsDto {
  /**
   * The whole preferences blob, replaced wholesale. OPAQUE: the server does
   * not read, validate or rewrite anything inside it -- same contract as
   * SyncObject.data. Only "is a JSON object" and the size ceiling are checked.
   *
   * (The global ValidationPipe runs with whitelist:true, which strips
   * undecorated properties of the DTO itself; it does not descend into a
   * plain @IsObject() field, so the blob survives byte-for-byte.)
   */
  @IsObject({ message: 'data must be a JSON object' })
  data!: Record<string, unknown>;

  /**
   * The version this edit was based on (a string, since it is a bigint).
   * Absent or "0" means "I believe the server has nothing saved yet". A
   * mismatch with the server's current version is a 409 carrying the server's
   * value, so the client can merge and retry -- the same handshake as push.
   */
  @IsOptional()
  @Transform(({ value }) => (typeof value === 'number' ? String(value) : value))
  @Matches(/^\d+$/, { message: 'expectedVersion must be a decimal string' })
  expectedVersion?: string;
}
