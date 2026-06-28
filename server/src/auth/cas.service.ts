import { Injectable, Logger } from '@nestjs/common';
import { ConfigService } from '@nestjs/config';
import { XMLParser } from 'fast-xml-parser';

export interface CasValidationResult {
  success: boolean;
  user?: string;
  displayName?: string;
  email?: string;
  error?: string;
}

// CAS 3.0 (single sign-on). Mirrors extension-packer's CasService, adapted for
// the desktop loopback flow: the service URL is this server's /auth/cas/callback
// carrying the client's loopback ?port (and a ?state nonce). The same service
// string must be byte-identical at /login and /serviceValidate, so both are
// built from serviceUrl().
@Injectable()
export class CasService {
  private readonly logger = new Logger(CasService.name);

  constructor(private readonly config: ConfigService) {}

  private base(): string {
    return this.config
      .getOrThrow<string>('CAS_BASE_URL')
      .replace(/\/+$/, '');
  }

  serviceUrl(port: number, state: string): string {
    const host = this.config
      .getOrThrow<string>('PLATFORM_HOST')
      .replace(/\/+$/, '');
    return `${host}/auth/cas/callback?port=${port}&state=${encodeURIComponent(state)}`;
  }

  buildLoginUrl(port: number, state: string): string {
    const service = this.serviceUrl(port, state);
    return `${this.base()}/login?service=${encodeURIComponent(service)}`;
  }

  /** Validate a CAS 3.0 ticket; /p3/serviceValidate returns the attribute block. */
  async validateTicket(
    ticket: string,
    port: number,
    state: string,
  ): Promise<CasValidationResult> {
    const service = this.serviceUrl(port, state);
    const url = `${this.base()}/p3/serviceValidate?service=${encodeURIComponent(
      service,
    )}&ticket=${encodeURIComponent(ticket)}`;

    const res = await fetch(url);
    if (!res.ok) {
      this.logger.warn(`CAS validate returned ${res.status}`);
      return { success: false, error: `CAS HTTP ${res.status}` };
    }
    return this.parse(await res.text());
  }

  private parse(xml: string): CasValidationResult {
    const parser = new XMLParser({
      ignoreAttributes: false,
      removeNSPrefix: true,
      parseTagValue: true,
      trimValues: true,
    });
    const parsed = parser.parse(xml) as Record<string, unknown>;
    const root = (parsed['serviceResponse'] ?? {}) as Record<string, unknown>;
    const success = root['authenticationSuccess'] as
      | Record<string, unknown>
      | undefined;

    if (success && typeof success['user'] !== 'undefined') {
      const user = String(success['user']).trim();
      if (user.length > 0) {
        const attrs = (success['attributes'] ?? {}) as Record<string, unknown>;
        return {
          success: true,
          user,
          displayName: this.pick(attrs, ['displayName', 'cn', 'name']),
          email: this.pick(attrs, ['mail', 'email']),
        };
      }
    }
    const failure = root['authenticationFailure'] as
      | Record<string, unknown>
      | string
      | undefined;
    const code =
      typeof failure === 'object' && failure !== null && '@_code' in failure
        ? String(failure['@_code'])
        : 'UNKNOWN_FAILURE';
    return { success: false, error: code };
  }

  private pick(attrs: Record<string, unknown>, keys: string[]): string | undefined {
    for (const k of keys) {
      const v = attrs[k];
      if (Array.isArray(v) && v.length > 0) {
        const s = String(v[0]).trim();
        if (s) return s;
      } else if (v !== undefined && v !== null) {
        const s = String(v).trim();
        if (s) return s;
      }
    }
    return undefined;
  }
}
