# ai-reader-server

Cloud sync + collaboration backend for ai-reader's literature-management
features. Self-hosted, no third-party BaaS.

- **Stack**: NestJS (TypeScript) + Prisma + PostgreSQL; MinIO/S3 for PDF blobs;
  WebSocket for change notifications.
- **Model**: one library per research *project*; members with roles
  (owner/editor/viewer); version-based optimistic sync with field-level
  last-write-wins; offline-first client (local SQLite mirror + outbox).

See `../docs/工作清单-文献管理能力.md` for the full plan.

## Quick start (local dev)

```bash
cd server
cp .env.example .env            # edit secrets

# bring up Postgres + MinIO (needs Docker)
docker compose up -d postgres minio

npm install
npm run prisma:generate
npm run prisma:migrate          # create/apply the schema
npm run start:dev               # http://localhost:3000/health
```

## Production

```bash
docker compose up -d --build    # postgres + minio + server (runs migrate deploy)
```

## Layout

```
src/
  main.ts            bootstrap (CORS, validation)
  app.module.ts      root module
  prisma/            PrismaService (global)
  health/            GET /health
  auth/              register / login / refresh (JWT, argon2)   [phase 0.2]
  projects/          projects + members + roles                 [phase 1]
  sync/              GET /projects/:id/sync, POST .../push       [phase 2]
  files/             S3 presigned upload/download                [phase 2.3]
  events/            WebSocket change notifications              [phase 2.4]
prisma/schema.prisma data model (source of truth)
```
