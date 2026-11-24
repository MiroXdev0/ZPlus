import assert from "node:assert/strict";
import { createServer } from "node:http";
import { readFile } from "node:fs/promises";
import { after, before, beforeEach, test } from "node:test";
import { PGlite } from "@electric-sql/pglite";
import { createRegistryHandler, extractArchive } from "../lib/registry.js";
import { createVercelNodeHandler } from "../lib/vercel-adapter.js";

const publicOrigin = "https://z-plus-flax.vercel.app";
const baseUrl = `${publicOrigin}/api/registry`;
const database = new PGlite();
const pool = {
  query: (...args) => database.query(...args),
  connect: async () => ({
    query: (...args) => database.query(...args),
    release() {}
  })
};
const objects = new Map();
let corruptNextRead = false;
const storage = {
  async put(key, body) {
    objects.set(key, Buffer.from(body));
  },
  async get(key) {
    const body = objects.get(key);
    if (!body) throw new Error("object not found");
    if (corruptNextRead) {
      corruptNextRead = false;
      return Buffer.from("corrupt archive");
    }
    return Buffer.from(body);
  },
  async del(key) {
    objects.delete(key);
  }
};
const handler = createRegistryHandler({ pool, storage, publicOrigin });
let nodeOrigin;
let nodeHandler;
const nodeServer = createServer((request, response) => nodeHandler(request, response));

before(async () => {
  const firstMigration = await readFile(
    new URL("../migrations/001_zpm_registry.sql", import.meta.url),
    "utf8"
  );
  const secondMigration = await readFile(
    new URL("../migrations/002_neon_object_storage.sql", import.meta.url),
    "utf8"
  );
  await database.exec(firstMigration);
  await database.exec(secondMigration);
  nodeServer.listen(0, "127.0.0.1");
  await new Promise((resolve, reject) => {
    nodeServer.once("listening", resolve);
    nodeServer.once("error", reject);
  });
  nodeOrigin = `http://127.0.0.1:${nodeServer.address().port}`;
  nodeHandler = createVercelNodeHandler(handler, nodeOrigin);
});

beforeEach(async () => {
  objects.clear();
  corruptNextRead = false;
  await database.exec(`
    TRUNCATE zpm_registry_audit, zpm_package_versions, zpm_package_maintainers,
      zpm_packages, zpm_rate_limits, zpm_sessions, zpm_users CASCADE
  `);
});

after(async () => {
  await new Promise((resolve, reject) => {
    nodeServer.close(error => error ? reject(error) : resolve());
  });
  await database.close();
});

function u32(value) {
  const result = Buffer.alloc(4);
  result.writeUInt32LE(value);
  return result;
}

function u64(value) {
  const result = Buffer.alloc(8);
  result.writeBigUInt64LE(BigInt(value));
  return result;
}

function archiveFor(manifest, files = {}) {
  const entries = new Map([
    ["zpm.json", Buffer.from(JSON.stringify(manifest))],
    ["src/main.zh", Buffer.from("pub fn greeting(): string { return \"hello\" }")]
  ]);
  for (const [name, content] of Object.entries(files)) {
    entries.set(name, Buffer.from(content));
  }
  const chunks = [Buffer.from("ZPM1"), u32(entries.size)];
  for (const [name, contents] of entries) {
    const path = Buffer.from(name);
    chunks.push(u32(path.length), u64(contents.length), path, contents);
  }
  return Buffer.concat(chunks);
}

function manifest(name = "registry-fixture", version = "1.0.0", dependencies = {}) {
  return {
    name,
    version,
    description: "A registry integration fixture.",
    author: "Fixture Author",
    license: "MIT",
    main: "src/main.zh",
    compiler: ">=1.2.0",
    dependencies
  };
}

function request(path, { method = "GET", headers = {}, body } = {}) {
  return new Request(`${baseUrl}${path}`, {
    method,
    headers,
    body
  });
}

async function register(username, email) {
  const response = await handler(request("/auth/register", {
    method: "POST",
    headers: {
      "content-type": "application/json",
      origin: publicOrigin,
      "x-real-ip": "203.0.113.10"
    },
    body: JSON.stringify({
      username,
      email,
      password: "correct-horse-battery-staple"
    })
  }));
  return response;
}

async function cliToken(email) {
  const response = await handler(request("/auth/cli-login", {
    method: "POST",
    headers: {
      "content-type": "application/json",
      "x-real-ip": "203.0.113.11"
    },
    body: JSON.stringify({
      email,
      password: "correct-horse-battery-staple"
    })
  }));
  assert.equal(response.status, 200);
  return (await response.json()).token;
}

async function cliTokenAttempt(email) {
  return handler(request("/auth/cli-login", {
    method: "POST",
    headers: {
      "content-type": "application/json",
      "x-real-ip": "203.0.113.12"
    },
    body: JSON.stringify({
      email,
      password: "correct-horse-battery-staple"
    })
  }));
}

async function publish(token, body) {
  return handler(request("/publish", {
    method: "POST",
    headers: {
      authorization: `Bearer ${token}`,
      "content-type": "application/vnd.zpm.archive"
    },
    body
  }));
}

test("validates the shared ZPM1 archive and rejects unsafe and foreign-code entries", () => {
  const valid = archiveFor(manifest(), { "README.md": "# Fixture" });
  assert.equal(extractArchive(valid).manifest.name, "registry-fixture");

  const traversal = archiveFor(manifest(), { "../outside.zh": "bad" });
  assert.throws(() => extractArchive(traversal), /unsafe archive path/);

  const foreignCode = archiveFor(manifest(), { "src/implementation.py": "print('no')" });
  assert.throws(() => extractArchive(foreignCode), /alternative-language code/);

  const localDependency = archiveFor(manifest("registry-fixture", "1.0.0", {
    sibling: { path: "../sibling", version: "^1.0.0" }
  }));
  assert.throws(() => extractArchive(localDependency), /invalid registry dependency/);
});

test("registration stores a password hash and rejects cross-site requests", async () => {
  const rejected = await handler(request("/auth/register", {
    method: "POST",
    headers: {
      "content-type": "application/json",
      origin: "https://attacker.example",
      "x-real-ip": "203.0.113.20"
    },
    body: JSON.stringify({
      username: "attacker",
      email: "attacker@example.com",
      password: "correct-horse-battery-staple"
    })
  }));
  assert.equal(rejected.status, 403);

  const response = await register("package-owner", "owner@example.com");
  assert.equal(response.status, 201);
  assert.match(response.headers.get("set-cookie"), /HttpOnly; SameSite=Lax; Max-Age=/);
  assert.match(response.headers.get("set-cookie"), /Secure/);
  const stored = await pool.query("SELECT password_hash FROM zpm_users");
  assert.equal(stored.rows.length, 1);
  assert.match(stored.rows[0].password_hash, /^scrypt\$/);
  assert.equal(stored.rows[0].password_hash.includes("correct-horse"), false);
});

test("browser login rejects invalid credentials, authenticates valid sessions, and reports duplicates", async () => {
  const registered = await register("account-owner", "account@example.com");
  assert.equal(registered.status, 201);

  const duplicate = await register("account-owner", "another@example.com");
  assert.equal(duplicate.status, 409);
  assert.equal((await duplicate.json()).error.code, "account_exists");

  const login = (password) => handler(request("/auth/login", {
    method: "POST",
    headers: {
      "content-type": "application/json",
      origin: publicOrigin,
      "x-real-ip": "203.0.113.41"
    },
    body: JSON.stringify({ email: "account@example.com", password })
  }));
  const invalid = await login("incorrect-password");
  assert.equal(invalid.status, 401);

  const valid = await login("correct-horse-battery-staple");
  assert.equal(valid.status, 200);
  const cookie = valid.headers.get("set-cookie").split(";")[0];
  const account = await handler(request("/auth/me", { headers: { cookie } }));
  assert.equal(account.status, 200);
  assert.equal((await account.json()).user.username, "account-owner");
});

test("publishes once, exposes searchable immutable metadata, and serves the archive", async () => {
  await register("package-owner", "owner@example.com");
  const token = await cliToken("owner@example.com");
  const body = archiveFor(manifest(), { "README.md": "# Registry fixture" });

  const unauthenticated = await publish("", body);
  assert.equal(unauthenticated.status, 401);

  const created = await publish(token, body);
  assert.equal(created.status, 201);
  const published = await created.json();
  assert.match(published.integrity, /^sha256:[0-9a-f]{64}$/);
  assert.equal(objects.size, 1);

  const search = await handler(request("/search?q=fixture"));
  assert.equal(search.status, 200);
  assert.equal((await search.json()).packages[0].name, "registry-fixture");

  const info = await handler(request("/packages/registry-fixture"));
  assert.equal(info.status, 200);
  const details = await info.json();
  assert.equal(details.owner, "package-owner");
  assert.equal(details.versions[0].readme, "# Registry fixture");
  assert.equal(details.versions[0].integrity, published.integrity);

  const resolved = await handler(request(
    "/packages/registry-fixture/resolve?range=%5E1.0.0"
  ));
  assert.equal(resolved.status, 200);
  assert.deepEqual(await resolved.json(), {
    name: "registry-fixture",
    version: "1.0.0",
    integrity: published.integrity
  });

  const rewrittenResolve = await handler(request(
    "?route=packages/registry-fixture/resolve&range=%5E1.0.0"
  ));
  assert.equal(rewrittenResolve.status, 200);
  assert.deepEqual(await rewrittenResolve.json(), {
    name: "registry-fixture",
    version: "1.0.0",
    integrity: published.integrity
  });

  const download = await handler(request(
    "/packages/registry-fixture/versions/1.0.0/download"
  ));
  assert.equal(download.status, 200);
  assert.equal(download.headers.get("content-type"), "application/vnd.zpm.archive");
  assert.deepEqual(Buffer.from(await download.arrayBuffer()), body);

  const duplicate = await publish(token, body);
  assert.equal(duplicate.status, 409);
  assert.equal(objects.size, 1);
  const count = await pool.query("SELECT count(*)::int AS count FROM zpm_package_versions");
  assert.equal(count.rows[0].count, 1);
});

test("rejects corrupted stored archives and removes failed pending publications", async () => {
  await register("package-owner", "owner@example.com");
  const token = await cliToken("owner@example.com");
  const body = archiveFor(manifest());

  corruptNextRead = true;
  const rejected = await publish(token, body);
  assert.equal(rejected.status, 502);
  assert.equal(objects.size, 0);
  const pending = await pool.query(
    "SELECT count(*)::int AS count FROM zpm_package_versions WHERE publication_state = 'pending'"
  );
  assert.equal(pending.rows[0].count, 0);

  assert.equal((await publish(token, body)).status, 201);
  const version = await pool.query(
    "SELECT storage_key FROM zpm_package_versions WHERE package_name = $1",
    ["registry-fixture"]
  );
  objects.set(version.rows[0].storage_key, Buffer.from("corrupted after publication"));
  const download = await handler(request(
    "/packages/registry-fixture/versions/1.0.0/download"
  ));
  assert.equal(download.status, 502);
  assert.equal((await download.json()).error.code, "storage_integrity");
});

test("keeps pending package versions out of public registry responses", async () => {
  await register("package-owner", "owner@example.com");
  const owner = await pool.query("SELECT id FROM zpm_users WHERE username = $1", ["package-owner"]);
  await pool.query(
    `INSERT INTO zpm_packages
       (name, owner_id, description, author, license, repository, homepage)
     VALUES ($1, $2, '', '', '', '', '')`,
    ["registry-fixture", owner.rows[0].id]
  );
  await pool.query(
    `INSERT INTO zpm_package_versions
       (package_name, version, compiler, main, manifest, integrity, blob_url,
        storage_key, archive_size, publication_state, published_by)
     VALUES ($1, '1.0.0', '', 'src/main.zh', '{}'::jsonb, $2, NULL, $3, 0,
             'pending', $4)`,
    [
      "registry-fixture",
      `sha256:${"0".repeat(64)}`,
      "packages/registry-fixture/1.0.0/123e4567-e89b-12d3-a456-426614174000.zpm",
      owner.rows[0].id
    ]
  );

  const search = await handler(request("/search?q=registry-fixture"));
  assert.deepEqual((await search.json()).packages, []);
  assert.equal((await handler(request("/packages/registry-fixture"))).status, 404);
  assert.equal((await handler(request(
    "/packages/registry-fixture/resolve?range=%2A"
  ))).status, 404);
  assert.equal((await handler(request(
    "/packages/registry-fixture/versions/1.0.0/download"
  ))).status, 404);
});

test("disabled accounts cannot use existing or new sessions", async () => {
  await register("package-owner", "owner@example.com");
  const token = await cliToken("owner@example.com");
  await pool.query(
    "UPDATE zpm_users SET status = 'disabled' WHERE username = $1",
    ["package-owner"]
  );

  const currentUser = await handler(request("/auth/me", {
    headers: { authorization: `Bearer ${token}` }
  }));
  assert.equal(currentUser.status, 401);
  assert.equal((await cliTokenAttempt("owner@example.com")).status, 401);
});

test("expired sessions cannot authenticate", async () => {
  await register("package-owner", "owner@example.com");
  const token = await cliToken("owner@example.com");
  await pool.query(
    "UPDATE zpm_sessions SET expires_at = now() - interval '1 second'"
  );

  const response = await handler(request("/auth/me", {
    headers: { authorization: `Bearer ${token}` }
  }));
  assert.equal(response.status, 401);
});

test("adapts Vercel Node requests for authentication and registry routes", async () => {
  const registered = await fetch(`${nodeOrigin}/api/registry/auth/register`, {
    method: "POST",
    headers: {
      "content-type": "application/json",
      origin: publicOrigin,
      "x-real-ip": "203.0.113.31"
    },
    body: JSON.stringify({
      username: "node-runtime-user",
      email: "node-runtime@example.com",
      password: "correct-horse-battery-staple"
    })
  });
  assert.equal(registered.status, 201);

  const cookie = registered.headers.get("set-cookie").split(";")[0];
  const unauthenticated = await fetch(`${nodeOrigin}/api/registry/auth/me`);
  assert.equal(unauthenticated.status, 401);
  assert.equal((await unauthenticated.json()).error.code, "unauthorized");

  const authenticated = await fetch(`${nodeOrigin}/api/registry/auth/me`, {
    headers: { cookie }
  });
  assert.equal(authenticated.status, 200);
  assert.equal((await authenticated.json()).user.username, "node-runtime-user");

  const invalid = await fetch(`${nodeOrigin}/api/registry/auth/me`, {
    headers: { cookie: "zpm_session=invalid" }
  });
  assert.equal(invalid.status, 401);

  await pool.query("UPDATE zpm_sessions SET expires_at = now() - interval '1 second'");
  const expired = await fetch(`${nodeOrigin}/api/registry/auth/me`, {
    headers: { cookie }
  });
  assert.equal(expired.status, 401);

  const search = await fetch(`${nodeOrigin}/api/registry/search?q=node-runtime`);
  assert.equal(search.status, 200);
});

test("prevents another account from claiming the package or replacing its version", async () => {
  await register("package-owner", "owner@example.com");
  await register("other-developer", "other@example.com");
  const ownerToken = await cliToken("owner@example.com");
  const otherToken = await cliToken("other@example.com");

  assert.equal((await publish(ownerToken, archiveFor(manifest()))).status, 201);
  assert.equal((await publish(otherToken, archiveFor(
    manifest("registry-fixture", "1.1.0")
  ))).status, 403);
  assert.equal((await publish(otherToken, archiveFor(manifest()))).status, 403);
  assert.equal(objects.size, 1);
});

test("supports owner-managed maintainers and records ownership changes", async () => {
  await register("package-owner", "owner@example.com");
  await register("package-maintainer", "maintainer@example.com");
  const ownerToken = await cliToken("owner@example.com");
  const maintainerToken = await cliToken("maintainer@example.com");
  assert.equal((await publish(ownerToken, archiveFor(manifest()))).status, 201);

  const added = await handler(request("/packages/registry-fixture/maintainers", {
    method: "POST",
    headers: {
      authorization: `Bearer ${ownerToken}`,
      "content-type": "application/json"
    },
    body: JSON.stringify({ username: "package-maintainer" })
  }));
  assert.equal(added.status, 200);
  assert.equal((await publish(maintainerToken, archiveFor(
    manifest("registry-fixture", "1.1.0")
  ))).status, 201);

  const audit = await pool.query(
    "SELECT action FROM zpm_registry_audit ORDER BY id"
  );
  assert.deepEqual(audit.rows.map(row => row.action), ["publish", "add_maintainer", "publish"]);
});

test("returns real package tags and account-scoped publication history", async () => {
  await register("package-owner", "owner@example.com");
  await register("other-developer", "other@example.com");
  const ownerToken = await cliToken("owner@example.com");
  const otherToken = await cliToken("other@example.com");
  const firstManifest = { ...manifest(), tags: ["utilities", "z+"] };
  const latestManifest = {
    ...manifest("registry-fixture", "1.1.0"),
    tags: ["utilities", "collections"]
  };
  assert.equal((await publish(ownerToken, archiveFor(firstManifest))).status, 201);
  assert.equal((await publish(ownerToken, archiveFor(latestManifest))).status, 201);

  const search = await handler(request("/search?q=registry-fixture"));
  assert.deepEqual((await search.json()).packages[0].tags, ["utilities", "collections"]);

  const account = await handler(request("/me/packages", {
    headers: { authorization: `Bearer ${ownerToken}` }
  }));
  const accountResult = await account.json();
  assert.deepEqual(accountResult.packages.map(item => item.name), ["registry-fixture"]);
  assert.deepEqual(accountResult.recentPublications.map(item => item.version), ["1.1.0", "1.0.0"]);
  assert.equal(accountResult.recentPublications[0].publisher, "package-owner");

  const otherAccount = await handler(request("/me/packages", {
    headers: { authorization: `Bearer ${otherToken}` }
  }));
  assert.deepEqual((await otherAccount.json()).recentPublications, []);
});

test("returns a service error when required storage is not configured", async () => {
  const withoutStorage = createRegistryHandler({ pool, publicOrigin });
  await register("package-owner", "owner@example.com");
  const token = await cliToken("owner@example.com");
  const response = await withoutStorage(request("/publish", {
    method: "POST",
    headers: {
      authorization: `Bearer ${token}`,
      "content-type": "application/vnd.zpm.archive"
    },
    body: archiveFor(manifest())
  }));
  assert.equal(response.status, 503);
  assert.equal((await response.json()).error.code, "storage_unavailable");
});
