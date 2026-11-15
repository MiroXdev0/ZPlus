CREATE TABLE IF NOT EXISTS zpm_users (
    id uuid PRIMARY KEY,
    username text NOT NULL UNIQUE,
    email text NOT NULL,
    password_hash text NOT NULL,
    created_at timestamptz NOT NULL DEFAULT now(),
    CONSTRAINT zpm_users_username_format CHECK (username ~ '^[a-z0-9][a-z0-9_-]{2,31}$')
);

CREATE UNIQUE INDEX IF NOT EXISTS zpm_users_email_lower_unique
    ON zpm_users (lower(email));

CREATE TABLE IF NOT EXISTS zpm_sessions (
    token_hash char(64) PRIMARY KEY,
    user_id uuid NOT NULL REFERENCES zpm_users(id) ON DELETE CASCADE,
    created_at timestamptz NOT NULL DEFAULT now(),
    expires_at timestamptz NOT NULL,
    revoked_at timestamptz
);

CREATE INDEX IF NOT EXISTS zpm_sessions_user_active
    ON zpm_sessions (user_id, expires_at)
    WHERE revoked_at IS NULL;

CREATE TABLE IF NOT EXISTS zpm_rate_limits (
    key_hash char(64) PRIMARY KEY,
    window_started_at timestamptz NOT NULL,
    count integer NOT NULL CHECK (count > 0)
);

CREATE TABLE IF NOT EXISTS zpm_packages (
    name text PRIMARY KEY,
    owner_id uuid NOT NULL REFERENCES zpm_users(id),
    description text NOT NULL,
    author text NOT NULL,
    license text NOT NULL,
    repository text NOT NULL,
    homepage text NOT NULL,
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now(),
    CONSTRAINT zpm_packages_name_format
        CHECK (name ~ '^[a-z]([a-z0-9-]{0,62}[a-z0-9])?$')
);

CREATE TABLE IF NOT EXISTS zpm_package_maintainers (
    package_name text NOT NULL REFERENCES zpm_packages(name) ON DELETE CASCADE,
    user_id uuid NOT NULL REFERENCES zpm_users(id) ON DELETE CASCADE,
    added_by uuid NOT NULL REFERENCES zpm_users(id),
    created_at timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (package_name, user_id)
);

CREATE TABLE IF NOT EXISTS zpm_package_versions (
    package_name text NOT NULL REFERENCES zpm_packages(name) ON DELETE CASCADE,
    version text NOT NULL,
    compiler text NOT NULL,
    main text NOT NULL,
    dependencies jsonb NOT NULL DEFAULT '{}'::jsonb,
    manifest jsonb NOT NULL,
    readme text NOT NULL DEFAULT '',
    integrity text NOT NULL,
    blob_url text NOT NULL,
    published_by uuid NOT NULL REFERENCES zpm_users(id),
    published_at timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (package_name, version),
    CONSTRAINT zpm_package_versions_integrity_format
        CHECK (integrity ~ '^sha256:[0-9a-f]{64}$')
);

CREATE INDEX IF NOT EXISTS zpm_package_versions_published
    ON zpm_package_versions (package_name, published_at DESC);

CREATE TABLE IF NOT EXISTS zpm_registry_audit (
    id bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    actor_id uuid NOT NULL REFERENCES zpm_users(id),
    package_name text NOT NULL,
    action text NOT NULL CHECK (action IN ('publish', 'add_maintainer', 'remove_maintainer', 'transfer')),
    details jsonb NOT NULL DEFAULT '{}'::jsonb,
    created_at timestamptz NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS zpm_registry_audit_package
    ON zpm_registry_audit (package_name, created_at DESC);
