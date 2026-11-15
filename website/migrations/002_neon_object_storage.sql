ALTER TABLE zpm_package_versions
    ADD COLUMN IF NOT EXISTS storage_key text,
    ADD COLUMN IF NOT EXISTS archive_size bigint,
    ADD COLUMN IF NOT EXISTS publication_state text NOT NULL DEFAULT 'published';

ALTER TABLE zpm_package_versions
    ALTER COLUMN blob_url DROP NOT NULL;

UPDATE zpm_package_versions
SET archive_size = 0
WHERE archive_size IS NULL;

ALTER TABLE zpm_package_versions
    ALTER COLUMN archive_size SET DEFAULT 0,
    ALTER COLUMN archive_size SET NOT NULL;

ALTER TABLE zpm_users
    ADD COLUMN IF NOT EXISTS status text NOT NULL DEFAULT 'active';

DO $$
BEGIN
    IF NOT EXISTS (
        SELECT 1
        FROM pg_constraint
        WHERE conname = 'zpm_package_versions_archive_size_range'
          AND conrelid = 'zpm_package_versions'::regclass
    ) THEN
        ALTER TABLE zpm_package_versions
            ADD CONSTRAINT zpm_package_versions_archive_size_range
            CHECK (archive_size >= 0 AND archive_size <= 4194304);
    END IF;
    IF NOT EXISTS (
        SELECT 1
        FROM pg_constraint
        WHERE conname = 'zpm_package_versions_publication_state'
          AND conrelid = 'zpm_package_versions'::regclass
    ) THEN
        ALTER TABLE zpm_package_versions
            ADD CONSTRAINT zpm_package_versions_publication_state
            CHECK (publication_state IN ('pending', 'published'));
    END IF;
    IF NOT EXISTS (
        SELECT 1
        FROM pg_constraint
        WHERE conname = 'zpm_users_status'
          AND conrelid = 'zpm_users'::regclass
    ) THEN
        ALTER TABLE zpm_users
            ADD CONSTRAINT zpm_users_status
            CHECK (status IN ('active', 'disabled'));
    END IF;
END
$$;

CREATE UNIQUE INDEX IF NOT EXISTS zpm_package_versions_storage_key_unique
    ON zpm_package_versions (storage_key)
    WHERE storage_key IS NOT NULL;
