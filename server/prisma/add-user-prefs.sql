-- Add the account-scoped preferences store (`user_prefs`) to an existing
-- ai-reader DB. Everything else in the sync stack hangs off a project; this
-- table hangs off the *user*, so portable settings (model config, prompts,
-- language, font sizes) follow the account instead of a shared library.
--
-- Idempotent: safe to run repeatedly, and on a DB that already has the table.
--
-- Apply with:
--   psql "$DATABASE_URL" -f prisma/add-user-prefs.sql

-- CreateTable
CREATE TABLE IF NOT EXISTS "user_prefs" (
    "user_id" UUID NOT NULL,
    "data" JSONB NOT NULL,
    "version" BIGINT NOT NULL DEFAULT 0,
    "updated_at" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,

    CONSTRAINT "user_prefs_pkey" PRIMARY KEY ("user_id")
);

-- AddForeignKey (ALTER TABLE ... ADD CONSTRAINT has no IF NOT EXISTS)
DO $$
BEGIN
    IF NOT EXISTS (
        SELECT 1 FROM pg_constraint WHERE conname = 'user_prefs_user_id_fkey'
    ) THEN
        ALTER TABLE "user_prefs" ADD CONSTRAINT "user_prefs_user_id_fkey"
            FOREIGN KEY ("user_id") REFERENCES "users"("id")
            ON DELETE CASCADE ON UPDATE CASCADE;
    END IF;
END
$$;
