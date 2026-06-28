-- Upgrade an existing (pre-CAS) ai-reader DB to the CAS-only user schema.
-- Safe to run on an empty users table.
ALTER TABLE "users" ADD COLUMN IF NOT EXISTS "cas_username" TEXT;
ALTER TABLE "users" ALTER COLUMN "password_hash" DROP NOT NULL;
ALTER TABLE "users" ALTER COLUMN "email" DROP NOT NULL;
CREATE UNIQUE INDEX IF NOT EXISTS "users_cas_username_key" ON "users"("cas_username");
