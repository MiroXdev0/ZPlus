import {
  createHash,
  randomBytes,
  randomUUID,
  scrypt,
  timingSafeEqual
} from "node:crypto";

const API_BASE = "/api/registry";
const MAX_ARCHIVE_BYTES = 4 * 1024 * 1024;
const MAX_ARCHIVE_ENTRIES = 10000;
const MAX_MANIFEST_BYTES = 1024 * 1024;
const MAX_JSON_BYTES = 16 * 1024;
const MAX_README_BYTES = 64 * 1024;
const SESSION_LIFETIME_MS = 30 * 24 * 60 * 60 * 1000;
const PENDING_PUBLICATION_TIMEOUT_MS = 30 * 60 * 1000;
const PASSWORD_SCRYPT = { N: 32768, r: 8, p: 1, maxmem: 64 * 1024 * 1024 };
const foreignCodeExtensions = new Set([
  ".a", ".bat", ".c", ".cc", ".cmd", ".cpp", ".cs", ".cxx", ".dll",
  ".dylib", ".exe", ".go", ".h", ".hh", ".hpp", ".hxx", ".class",
  ".jar", ".java", ".js", ".jsx", ".kt", ".lua", ".mjs", ".node",
  ".o", ".obj", ".php", ".pl", ".ps1", ".py", ".rb", ".rs", ".scala",
  ".sh", ".so", ".swift", ".ts", ".tsx", ".vb", ".wasm"
]);

const scryptAsync = (password, salt, length, options) => new Promise((resolve, reject) => {
  scrypt(password, salt, length, options, (error, derivedKey) => {
    if (error) reject(error);
    else resolve(derivedKey);
  });
});

class RegistryError extends Error {
  constructor(status, code, message, headers = {}) {
    super(message);
    this.status = status;
    this.code = code;
    this.headers = headers;
  }
}

function jsonResponse(status, value, headers = {}) {
  const responseHeaders = new Headers(headers);
  responseHeaders.set("Content-Type", "application/json; charset=utf-8");
  responseHeaders.set("X-Content-Type-Options", "nosniff");
  if (!responseHeaders.has("Cache-Control")) {
    responseHeaders.set("Cache-Control", "no-store");
  }
  return new Response(JSON.stringify(value), { status, headers: responseHeaders });
}

function fail(status, code, message, headers) {
  throw new RegistryError(status, code, message, headers);
}

function sha256(value) {
  return createHash("sha256").update(value).digest("hex");
}

function validatePackageName(name) {
  return typeof name === "string" &&
    name.length <= 64 &&
    /^[a-z](?:[a-z0-9-]{0,62}[a-z0-9])?$/.test(name);
}

function parseVersion(value) {
  if (typeof value !== "string" || value.length > 128) return null;
  const match = /^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?(?:\+([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?$/.exec(value);
  if (!match) return null;
  const prerelease = match[4] ? match[4].split(".") : [];
  if (prerelease.some(item => /^\d+$/.test(item) && item.length > 1 && item[0] === "0")) {
    return null;
  }
  return {
    major: BigInt(match[1]),
    minor: BigInt(match[2]),
    patch: BigInt(match[3]),
    prerelease
  };
}

function compareVersions(leftValue, rightValue) {
  const left = parseVersion(leftValue);
  const right = parseVersion(rightValue);
  if (!left || !right) return 0;
  for (const component of ["major", "minor", "patch"]) {
    if (left[component] !== right[component]) {
      return left[component] < right[component] ? -1 : 1;
    }
  }
  if (!left.prerelease.length || !right.prerelease.length) {
    return left.prerelease.length === right.prerelease.length
      ? 0
      : left.prerelease.length ? -1 : 1;
  }
  for (let index = 0; index < Math.min(left.prerelease.length, right.prerelease.length); index++) {
    const a = left.prerelease[index];
    const b = right.prerelease[index];
    if (a === b) continue;
    const aNumeric = /^\d+$/.test(a);
    const bNumeric = /^\d+$/.test(b);
    if (aNumeric && bNumeric) return BigInt(a) < BigInt(b) ? -1 : 1;
    if (aNumeric !== bNumeric) return aNumeric ? -1 : 1;
    return a < b ? -1 : 1;
  }
  return left.prerelease.length === right.prerelease.length
    ? 0
    : left.prerelease.length < right.prerelease.length ? -1 : 1;
}

function satisfiesVersion(version, constraint) {
  const candidate = parseVersion(version);
  if (!candidate) return false;
  if (constraint === "" || constraint === "*") return candidate.prerelease.length === 0;
  const terms = constraint.trim().split(/\s+/);
  if (!terms.length || terms.some(term => !term)) return false;
  return terms.every(term => {
    const match = /^(>=|<=|>|<|=|\^|~)?(.+)$/.exec(term);
    if (!match) return false;
    const required = parseVersion(match[2]);
    if (!required) return false;
    const operation = match[1] || "=";
    const comparison = compareVersions(version, match[2]);
    if (operation === "=") return comparison === 0;
    if (operation === ">") return comparison > 0;
    if (operation === ">=") return comparison >= 0;
    if (operation === "<") return comparison < 0;
    if (operation === "<=") return comparison <= 0;
    if (operation === "^") {
      if (comparison < 0) return false;
      if (required.major !== 0n) return candidate.major === required.major;
      if (required.minor !== 0n) {
        return candidate.major === 0n && candidate.minor === required.minor;
      }
      return candidate.major === 0n && candidate.minor === 0n &&
        candidate.patch === required.patch;
    }
    return comparison >= 0 && candidate.major === required.major &&
      candidate.minor === required.minor;
  });
}

function validConstraint(value) {
  if (typeof value !== "string" || value.length > 512 || !value.trim()) return false;
  if (value.trim() === "*") return true;
  return value.trim().split(/\s+/).every(term => {
    const match = /^(>=|<=|>|<|=|\^|~)?(.+)$/.exec(term);
    return !!match && !!parseVersion(match[2]);
  });
}

function validRelativePath(path) {
  if (typeof path !== "string" || !path || path.length > 4096 ||
      path.startsWith("/") || path.includes("\\") || path.includes(":") ||
      /[\u0000-\u001f\u007f]/.test(path)) {
    return false;
  }
  const parts = path.split("/");
  if (parts.some(part => !part || part === "." || part === ".." ||
      part.endsWith(".") || part.endsWith(" "))) {
    return false;
  }
  return parts.every(part => !/[<>:"|?*]/.test(part) &&
    !/^(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\..*)?$/i.test(part));
}

function readU32(bytes, offset) {
  if (offset + 4 > bytes.length) throw new Error("truncated archive");
  return bytes.readUInt32LE(offset);
}

function readU64(bytes, offset) {
  if (offset + 8 > bytes.length) throw new Error("truncated archive");
  const value = bytes.readBigUInt64LE(offset);
  if (value > BigInt(MAX_ARCHIVE_BYTES)) throw new Error("archive entry exceeds size limit");
  return Number(value);
}

function decodeUtf8(bytes, label) {
  try {
    return new TextDecoder("utf-8", { fatal: true }).decode(bytes);
  } catch {
    throw new Error(`${label} is not valid UTF-8`);
  }
}

function extractArchive(buffer) {
  if (!Buffer.isBuffer(buffer) || buffer.length < 8 ||
      buffer.length > MAX_ARCHIVE_BYTES || buffer.toString("ascii", 0, 4) !== "ZPM1") {
    throw new Error("unsupported or oversized ZPM archive");
  }
  const count = readU32(buffer, 4);
  if (!count || count > MAX_ARCHIVE_ENTRIES) throw new Error("invalid archive entry count");
  const files = new Map();
  const paths = new Set();
  let offset = 8;
  let total = 0;
  for (let index = 0; index < count; index++) {
    const pathLength = readU32(buffer, offset);
    offset += 4;
    const size = readU64(buffer, offset);
    offset += 8;
    if (!pathLength || pathLength > 4096 || offset + pathLength + size > buffer.length) {
      throw new Error("invalid or truncated archive entry");
    }
    const path = decodeUtf8(buffer.subarray(offset, offset + pathLength), "archive path");
    offset += pathLength;
    if (!validRelativePath(path)) throw new Error(`unsafe archive path '${path}'`);
    const key = path.toLowerCase();
    if (paths.has(key)) throw new Error(`duplicate archive path '${path}'`);
    paths.add(key);
    const components = path.split("/");
    if (components.some(component => [".git", ".hg", ".svn", "build", "z+_packages"].includes(component))) {
      throw new Error(`archive contains excluded package data '${path}'`);
    }
    const baseName = components.at(-1);
    if (baseName === ".zpm-installed.json" || baseName === ".zpm-cache.json") {
      throw new Error(`archive contains reserved ZPM metadata '${path}'`);
    }
    const extension = path.slice(path.lastIndexOf(".")).toLowerCase();
    if (foreignCodeExtensions.has(extension)) {
      throw new Error(`Z+ packages cannot contain alternative-language code '${path}'`);
    }
    total += size;
    if (total > MAX_ARCHIVE_BYTES) throw new Error("package contents exceed size limit");
    const contents = buffer.subarray(offset, offset + size);
    offset += size;
    files.set(path, contents);
  }
  if (offset !== buffer.length) throw new Error("archive contains trailing data");
  const manifestBytes = files.get("zpm.json");
  if (!manifestBytes || manifestBytes.length > MAX_MANIFEST_BYTES) {
    throw new Error("package archive requires a bounded zpm.json manifest");
  }
  let manifest;
  try {
    manifest = JSON.parse(decodeUtf8(manifestBytes, "package manifest"));
  } catch {
    throw new Error("package manifest is not valid UTF-8 JSON");
  }
  if (!manifest || typeof manifest !== "object" || Array.isArray(manifest)) {
    throw new Error("package manifest must be a JSON object");
  }
  if (!validatePackageName(manifest.name)) throw new Error("invalid package name");
  if (!parseVersion(manifest.version)) throw new Error("invalid package semantic version");
  if (!validRelativePath(manifest.main) || !manifest.main.endsWith(".zh") ||
      !files.has(manifest.main)) {
    throw new Error("package main must name an existing .zh Z+ module");
  }
  const sourceFiles = [...files.keys()].filter(path => /\.(zh|zp)$/i.test(path));
  if (!sourceFiles.length) throw new Error("package must contain Z+ source files");
  for (const field of ["description", "author", "license", "repository", "homepage", "compiler"]) {
    if (manifest[field] !== undefined &&
        (typeof manifest[field] !== "string" || Buffer.byteLength(manifest[field]) > 4096)) {
      throw new Error(`package manifest field '${field}' must be a short string`);
    }
  }
  if (manifest.compiler && !validConstraint(manifest.compiler)) {
    throw new Error("package compiler requirement is not a valid version constraint");
  }
  if (manifest.devDependencies !== undefined) {
    throw new Error("development dependencies are not supported by this ZPM release");
  }
  const dependencies = manifest.dependencies ?? {};
  if (!dependencies || typeof dependencies !== "object" || Array.isArray(dependencies)) {
    throw new Error("package dependencies must be an object");
  }
  for (const [name, constraint] of Object.entries(dependencies)) {
    if (!validatePackageName(name) || !validConstraint(constraint)) {
      throw new Error(`invalid registry dependency '${name}'`);
    }
  }
  const readmeBytes = files.get("README.md") ?? files.get("readme.md");
  const readme = readmeBytes && readmeBytes.length <= MAX_README_BYTES
    ? decodeUtf8(readmeBytes, "package README")
    : "";
  return { manifest, dependencies, readme };
}

async function readLimited(request, limit) {
  const declaredLength = Number(request.headers.get("content-length"));
  if (Number.isFinite(declaredLength) && declaredLength > limit) {
    fail(413, "payload_too_large", "Request body exceeds the allowed size.");
  }
  if (!request.body) return Buffer.alloc(0);
  const reader = request.body.getReader();
  const chunks = [];
  let total = 0;
  try {
    while (true) {
      const { done, value } = await reader.read();
      if (done) break;
      total += value.byteLength;
      if (total > limit) {
        await reader.cancel();
        fail(413, "payload_too_large", "Request body exceeds the allowed size.");
      }
      chunks.push(Buffer.from(value));
    }
  } finally {
    reader.releaseLock();
  }
  return Buffer.concat(chunks, total);
}

async function readJson(request) {
  const contentType = request.headers.get("content-type") || "";
  if (!contentType.toLowerCase().startsWith("application/json")) {
    fail(415, "unsupported_media_type", "Send a JSON request body.");
  }
  const bytes = await readLimited(request, MAX_JSON_BYTES);
  let value;
  try {
    value = JSON.parse(decodeUtf8(bytes, "request body"));
  } catch {
    fail(400, "invalid_json", "Request body is not valid JSON.");
  }
  if (!value || typeof value !== "object" || Array.isArray(value)) {
    fail(400, "invalid_json", "Request body must be a JSON object.");
  }
  return value;
}

function normalizeEmail(value) {
  if (typeof value !== "string") return "";
  const email = value.trim().toLowerCase();
  return email.length <= 254 &&
    /^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email) ? email : "";
}

function normalizeUsername(value) {
  if (typeof value !== "string") return "";
  const username = value.trim().toLowerCase();
  return /^[a-z0-9][a-z0-9_-]{2,31}$/.test(username) ? username : "";
}

function validPassword(value) {
  return typeof value === "string" &&
    Buffer.byteLength(value, "utf8") >= 12 &&
    Buffer.byteLength(value, "utf8") <= 1024;
}

async function hashPassword(password) {
  const salt = randomBytes(16);
  const hash = await scryptAsync(password, salt, 64, PASSWORD_SCRYPT);
  return `scrypt$${PASSWORD_SCRYPT.N}$${PASSWORD_SCRYPT.r}$${PASSWORD_SCRYPT.p}$${salt.toString("base64url")}$${hash.toString("base64url")}`;
}

async function verifyPassword(password, encoded) {
  const parts = typeof encoded === "string" ? encoded.split("$") : [];
  if (parts.length !== 6 || parts[0] !== "scrypt" ||
      parts[1] !== String(PASSWORD_SCRYPT.N) ||
      parts[2] !== String(PASSWORD_SCRYPT.r) ||
      parts[3] !== String(PASSWORD_SCRYPT.p)) {
    await scryptAsync(password, Buffer.alloc(16), 64, PASSWORD_SCRYPT);
    return false;
  }
  try {
    const salt = Buffer.from(parts[4], "base64url");
    const expected = Buffer.from(parts[5], "base64url");
    if (salt.length !== 16 || expected.length !== 64) return false;
    const actual = await scryptAsync(password, salt, 64, PASSWORD_SCRYPT);
    return timingSafeEqual(actual, expected);
  } catch {
    return false;
  }
}

function requestIp(request) {
  const value = request.headers.get("x-real-ip") ||
    request.headers.get("x-forwarded-for")?.split(",")[0] || "unknown";
  return value.trim().slice(0, 128) || "unknown";
}

async function enforceRateLimit(pool, key, maxCount, seconds) {
  const keyHash = sha256(key);
  const result = await pool.query(
    `INSERT INTO zpm_rate_limits (key_hash, window_started_at, count)
     VALUES ($1, now(), 1)
     ON CONFLICT (key_hash) DO UPDATE SET
       count = CASE
         WHEN zpm_rate_limits.window_started_at <= now() - ($2::double precision * interval '1 second') THEN 1
         ELSE zpm_rate_limits.count + 1
       END,
       window_started_at = CASE
         WHEN zpm_rate_limits.window_started_at <= now() - ($2::double precision * interval '1 second') THEN now()
         ELSE zpm_rate_limits.window_started_at
       END
     RETURNING count`,
    [keyHash, seconds]
  );
  if (Number(result.rows[0].count) > maxCount) {
    fail(429, "rate_limited", "Too many requests. Try again later.", {
      "Retry-After": String(seconds)
    });
  }
}

function cookieName(publicOrigin) {
  return publicOrigin.startsWith("https:")
    ? "__Host-zpm_session"
    : "zpm_session";
}

function cookieValue(request, name) {
  const cookie = request.headers.get("cookie") || "";
  for (const item of cookie.split(";")) {
    const separator = item.indexOf("=");
    if (separator < 0 || item.slice(0, separator).trim() !== name) continue;
    const value = item.slice(separator + 1).trim();
    return /^[A-Za-z0-9_-]{32,128}$/.test(value) ? value : "";
  }
  return "";
}

function requestToken(request, publicOrigin) {
  const authorization = request.headers.get("authorization") || "";
  const bearer = /^Bearer ([A-Za-z0-9_-]{32,128})$/.exec(authorization);
  if (bearer) return bearer[1];
  return cookieValue(request, cookieName(publicOrigin));
}

function sessionCookie(token, publicOrigin, maxAge) {
  const secure = publicOrigin.startsWith("https:") ? "; Secure" : "";
  return `${cookieName(publicOrigin)}=${encodeURIComponent(token)}; Path=/; HttpOnly; SameSite=Lax; Max-Age=${maxAge}${secure}`;
}

async function newSession(pool, userId) {
  const token = randomBytes(32).toString("base64url");
  const expiresAt = new Date(Date.now() + SESSION_LIFETIME_MS);
  await pool.query(
    "INSERT INTO zpm_sessions (token_hash, user_id, expires_at) VALUES ($1, $2, $3)",
    [sha256(token), userId, expiresAt]
  );
  return { token, expiresAt };
}

async function currentUser(pool, request, publicOrigin) {
  const token = requestToken(request, publicOrigin);
  if (!token) return null;
  const result = await pool.query(
    `SELECT u.id, u.username, u.email
     FROM zpm_sessions s
     JOIN zpm_users u ON u.id = s.user_id
     WHERE s.token_hash = $1 AND s.revoked_at IS NULL AND s.expires_at > now()
       AND u.status = 'active'`,
    [sha256(token)]
  );
  return result.rows[0] || null;
}

async function requireUser(pool, request, publicOrigin) {
  const user = await currentUser(pool, request, publicOrigin);
  if (!user) fail(401, "unauthorized", "Sign in to perform this operation.");
  return user;
}

function requireBrowserOrigin(request, publicOrigin) {
  if (request.headers.get("origin") !== publicOrigin) {
    fail(403, "invalid_origin", "This browser request did not come from the official website.");
  }
}

async function requireOwnerOrMaintainer(pool, packageName, userId) {
  const result = await pool.query(
    `SELECT p.owner_id, EXISTS (
       SELECT 1 FROM zpm_package_maintainers m
       WHERE m.package_name = p.name AND m.user_id = $2
     ) AS is_maintainer
     FROM zpm_packages p
     WHERE p.name = $1`,
    [packageName, userId]
  );
  const row = result.rows[0];
  if (!row) fail(404, "package_not_found", "Package was not found.");
  if (row.owner_id !== userId && !row.is_maintainer) {
    fail(403, "forbidden", "You are not authorized to manage this package.");
  }
  return row;
}

async function withTransaction(pool, callback) {
  const client = await pool.connect();
  try {
    await client.query("BEGIN");
    const result = await callback(client);
    await client.query("COMMIT");
    return result;
  } catch (error) {
    await client.query("ROLLBACK");
    throw error;
  } finally {
    client.release();
  }
}

async function register(pool, request, publicOrigin) {
  requireBrowserOrigin(request, publicOrigin);
  const body = await readJson(request);
  const username = normalizeUsername(body.username);
  const email = normalizeEmail(body.email);
  if (!username || !email || !validPassword(body.password)) {
    fail(400, "invalid_registration", "Enter a valid username and email, and a password of 12 to 1024 UTF-8 bytes.");
  }
  const ip = requestIp(request);
  await enforceRateLimit(pool, `register-ip:${ip}`, 5, 3600);
  await enforceRateLimit(pool, `register-email:${email}`, 5, 3600);
  const user = {
    id: randomUUID(),
    username,
    email
  };
  try {
    await pool.query(
      "INSERT INTO zpm_users (id, username, email, password_hash) VALUES ($1, $2, $3, $4)",
      [user.id, user.username, user.email, await hashPassword(body.password)]
    );
  } catch (error) {
    if (error.code === "23505") {
      fail(409, "account_exists", "That username or email is already registered.");
    }
    throw error;
  }
  const session = await newSession(pool, user.id);
  return jsonResponse(201, { user }, {
    "Set-Cookie": sessionCookie(session.token, publicOrigin, 30 * 24 * 60 * 60)
  });
}

async function login(pool, request, publicOrigin, cli) {
  if (!cli) requireBrowserOrigin(request, publicOrigin);
  const body = await readJson(request);
  const email = normalizeEmail(body.email);
  if (!email || typeof body.password !== "string" ||
      Buffer.byteLength(body.password, "utf8") > 1024) {
    fail(400, "invalid_credentials", "Enter a valid email and password.");
  }
  const ip = requestIp(request);
  await enforceRateLimit(pool, `login-ip:${ip}`, 10, 60);
  await enforceRateLimit(pool, `login-email:${email}`, 10, 3600);
  const result = await pool.query(
    `SELECT id, username, email, password_hash, status
     FROM zpm_users WHERE lower(email) = $1`,
    [email]
  );
  const user = result.rows[0];
  const passwordMatches = user
    ? await verifyPassword(body.password, user.password_hash)
    : false;
  if (!user || user.status !== "active" || !passwordMatches) {
    fail(401, "invalid_credentials", "Email or password is incorrect.");
  }
  const session = await newSession(pool, user.id);
  if (cli) {
    return jsonResponse(200, {
      token: session.token,
      expiresAt: session.expiresAt.toISOString(),
      user: { id: user.id, username: user.username, email: user.email }
    });
  }
  return jsonResponse(200, {
    user: { id: user.id, username: user.username, email: user.email }
  }, {
    "Set-Cookie": sessionCookie(session.token, publicOrigin, 30 * 24 * 60 * 60)
  });
}

function validateStringField(manifest, key, maxLength, fallback = "") {
  const value = manifest[key] ?? fallback;
  if (typeof value !== "string" || Buffer.byteLength(value, "utf8") > maxLength) {
    throw new Error(`package manifest field '${key}' must be a string of at most ${maxLength} bytes`);
  }
  return value;
}

async function publish(pool, storage, request, publicOrigin) {
  if (request.headers.get("cookie")) requireBrowserOrigin(request, publicOrigin);
  const user = await requireUser(pool, request, publicOrigin);
  if (!storage || typeof storage.put !== "function" ||
      typeof storage.get !== "function" || typeof storage.del !== "function") {
    fail(503, "storage_unavailable", "Package storage is not connected to this Vercel project.");
  }
  await enforceRateLimit(pool, `publish-user:${user.id}`, 20, 3600);
  const archive = await readLimited(request, MAX_ARCHIVE_BYTES);
  let packageData;
  try {
    packageData = extractArchive(archive);
  } catch (error) {
    fail(400, "invalid_package", error.message);
  }
  const { manifest, dependencies, readme } = packageData;
  const description = validateStringField(manifest, "description", 500);
  const author = validateStringField(manifest, "author", 128, user.username);
  const license = validateStringField(manifest, "license", 128);
  const repository = validateStringField(manifest, "repository", 2048);
  const homepage = validateStringField(manifest, "homepage", 2048);
  const compiler = validateStringField(manifest, "compiler", 512);
  const archiveIntegrity = `sha256:${sha256(archive)}`;
  let storageKey = `packages/${manifest.name}/${manifest.version}/${randomUUID()}.zpm`;
  await withTransaction(pool, async client => {
    await client.query(
      `INSERT INTO zpm_packages
         (name, owner_id, description, author, license, repository, homepage)
       VALUES ($1, $2, $3, $4, $5, $6, $7)
       ON CONFLICT (name) DO NOTHING`,
      [manifest.name, user.id, description, author, license, repository, homepage]
    );
    const packageResult = await client.query(
      "SELECT owner_id FROM zpm_packages WHERE name = $1 FOR UPDATE",
      [manifest.name]
    );
    const packageRow = packageResult.rows[0];
    const maintainers = await client.query(
      "SELECT 1 FROM zpm_package_maintainers WHERE package_name = $1 AND user_id = $2",
      [manifest.name, user.id]
    );
    if (packageRow.owner_id !== user.id && !maintainers.rowCount) {
      fail(403, "package_owned", "This package name is owned by another developer.");
    }

    const existing = await client.query(
      `SELECT publication_state, published_by, integrity, archive_size,
              storage_key, published_at
       FROM zpm_package_versions
       WHERE package_name = $1 AND version = $2
       FOR UPDATE`,
      [manifest.name, manifest.version]
    );
    if (existing.rowCount) {
      const version = existing.rows[0];
      if (version.publication_state !== "pending" ||
          version.published_by !== user.id ||
          version.integrity !== archiveIntegrity ||
          Number(version.archive_size) !== archive.length ||
          !version.storage_key) {
        fail(409, "version_exists", "This package version has already been published.");
      }
      if (Date.now() - new Date(version.published_at).getTime() <
          PENDING_PUBLICATION_TIMEOUT_MS) {
        fail(409, "publication_in_progress", "A package publication is already in progress.");
      }
      storageKey = version.storage_key;
      await client.query(
        `UPDATE zpm_package_versions
         SET published_at = now()
         WHERE package_name = $1 AND version = $2
           AND publication_state = 'pending' AND storage_key = $3`,
        [manifest.name, manifest.version, storageKey]
      );
      return;
    }
    await client.query(
      `INSERT INTO zpm_package_versions
         (package_name, version, compiler, main, dependencies, manifest, readme,
          integrity, blob_url, storage_key, archive_size, publication_state,
          published_by)
       VALUES ($1, $2, $3, $4, $5::jsonb, $6::jsonb, $7, $8, NULL, $9, $10,
               'pending', $11)`,
      [
        manifest.name,
        manifest.version,
        compiler,
        manifest.main,
        JSON.stringify(dependencies),
        JSON.stringify(manifest),
        readme,
        archiveIntegrity,
        storageKey,
        archive.length,
        user.id
      ]
    );
  });

  let storageMayExist = false;
  let finalizationStarted = false;
  try {
    storageMayExist = true;
    await storage.put(storageKey, archive, {
      contentType: "application/vnd.zpm.archive"
    });
    const storedArchive = Buffer.from(await storage.get(storageKey));
    if (storedArchive.length !== archive.length ||
        `sha256:${sha256(storedArchive)}` !== archiveIntegrity) {
      fail(502, "storage_integrity", "The stored package archive failed its integrity check.");
    }

    finalizationStarted = true;
    const published = await withTransaction(pool, async client => {
      const versionResult = await client.query(
        `UPDATE zpm_package_versions
         SET publication_state = 'published', published_at = now()
         WHERE package_name = $1 AND version = $2
           AND publication_state = 'pending' AND storage_key = $3
           AND published_by = $4
         RETURNING version, published_at`,
        [manifest.name, manifest.version, storageKey, user.id]
      );
      if (!versionResult.rowCount) {
        fail(409, "publication_conflict", "This package publication could not be finalized.");
      }
      await client.query(
        `UPDATE zpm_packages
         SET description = $2, author = $3, license = $4, repository = $5,
             homepage = $6, updated_at = now()
         WHERE name = $1`,
        [manifest.name, description, author, license, repository, homepage]
      );
      await client.query(
        `INSERT INTO zpm_registry_audit (actor_id, package_name, action, details)
         VALUES ($1, $2, 'publish', $3::jsonb)`,
        [user.id, manifest.name, JSON.stringify({
          version: manifest.version,
          integrity: archiveIntegrity
        })]
      );
      return versionResult.rows[0];
    });
    return jsonResponse(201, {
      name: manifest.name,
      version: manifest.version,
      integrity: archiveIntegrity,
      publishedAt: published.published_at,
      downloadUrl: `${API_BASE}/packages/${encodeURIComponent(manifest.name)}/versions/${encodeURIComponent(manifest.version)}/download`
    });
  } catch (error) {
    if (storageMayExist && !finalizationStarted) {
      let storageRemoved = false;
      try {
        await storage.del(storageKey);
        storageRemoved = true;
      } catch (cleanupError) {
        console.error("Unable to remove rejected ZPM archive from Neon Object Storage.", cleanupError);
      }
      if (storageRemoved) {
        try {
          await pool.query(
            `DELETE FROM zpm_package_versions
             WHERE package_name = $1 AND version = $2
               AND publication_state = 'pending' AND storage_key = $3
               AND published_by = $4`,
            [manifest.name, manifest.version, storageKey, user.id]
          );
        } catch (cleanupError) {
          console.error("Unable to remove the pending ZPM version record.", cleanupError);
        }
      }
    }
    throw error;
  }
}

function packageTags(manifest) {
  const values = manifest?.tags ?? manifest?.keywords ?? manifest?.categories;
  if (!Array.isArray(values)) return [];
  return [...new Set(values
    .filter(value => typeof value === "string" && value.trim())
    .map(value => value.trim().slice(0, 40)))]
    .slice(0, 8);
}

async function searchPackages(pool, url) {
  const query = (url.searchParams.get("q") || "").trim();
  if (query.length > 100) fail(400, "invalid_query", "Search query is too long.");
  const limitValue = Number(url.searchParams.get("limit") || 30);
  const limit = Number.isInteger(limitValue) ? Math.min(Math.max(limitValue, 1), 50) : 30;
  const pattern = query ? `%${query.replace(/[\\%_]/g, "\\$&")}%` : "%";
  const result = await pool.query(
    `SELECT p.name, p.description, p.author, p.license, p.repository,
            p.homepage, p.updated_at, u.username AS owner
     FROM zpm_packages p
     JOIN zpm_users u ON u.id = p.owner_id
     WHERE ($1 = '%' OR p.name ILIKE $1 ESCAPE '\\' OR p.description ILIKE $1 ESCAPE '\\')
       AND EXISTS (
         SELECT 1 FROM zpm_package_versions v
         WHERE v.package_name = p.name AND v.publication_state = 'published'
           AND v.version !~ '-'
       )
     ORDER BY p.name
     LIMIT $2`,
    [pattern, limit]
  );
  const names = result.rows.map(row => row.name);
  const versions = names.length
    ? await pool.query(
      `SELECT package_name, version
       FROM zpm_package_versions
       WHERE package_name = ANY($1::text[])
         AND publication_state = 'published' AND version !~ '-'`,
      [names]
    )
    : { rows: [] };
  const latest = new Map();
  for (const row of versions.rows) {
    const current = latest.get(row.package_name);
    if (!current || compareVersions(row.version, current.version) > 0) {
      latest.set(row.package_name, { version: row.version });
    }
  }
  const latestPackages = [...latest.entries()];
  const latestManifests = latestPackages.length
    ? await pool.query(
      `SELECT package_name, manifest
       FROM zpm_package_versions
       WHERE publication_state = 'published' AND version !~ '-'
         AND (${latestPackages.map((_, index) =>
    `(package_name = $${index * 2 + 1} AND version = $${index * 2 + 2})`
  ).join(" OR ")})`,
      latestPackages.flatMap(([name, version]) => [name, version.version])
    )
    : { rows: [] };
  const manifestsByName = new Map(
    latestManifests.rows.map(row => [row.package_name, row.manifest])
  );
  return jsonResponse(200, {
    packages: result.rows.map(row => ({
      name: row.name,
      version: latest.get(row.name)?.version,
      description: row.description,
      author: row.author,
      owner: row.owner,
      license: row.license,
      repository: row.repository,
      homepage: row.homepage,
      updatedAt: row.updated_at,
      tags: packageTags(manifestsByName.get(row.name))
    }))
  }, { "Cache-Control": "public, max-age=60, stale-while-revalidate=300" });
}

async function packageInfo(pool, packageName) {
  if (!validatePackageName(packageName)) fail(400, "invalid_name", "Package name is invalid.");
  const result = await pool.query(
    `SELECT p.name, p.description, p.author, p.license, p.repository,
            p.homepage, p.created_at, p.updated_at, u.username AS owner
     FROM zpm_packages p
     JOIN zpm_users u ON u.id = p.owner_id
     WHERE p.name = $1
       AND EXISTS (
         SELECT 1 FROM zpm_package_versions v
         WHERE v.package_name = p.name AND v.publication_state = 'published'
       )`,
    [packageName]
  );
  const packageRow = result.rows[0];
  if (!packageRow) fail(404, "package_not_found", "Package was not found.");
  const versions = await pool.query(
    `SELECT v.version, v.compiler, v.main, v.dependencies, v.manifest,
            v.readme, v.integrity, v.published_at, u.username AS publisher
     FROM zpm_package_versions v
     JOIN zpm_users u ON u.id = v.published_by
     WHERE v.package_name = $1 AND v.publication_state = 'published'`,
    [packageName]
  );
  versions.rows.sort((left, right) => compareVersions(right.version, left.version));
  const maintainers = await pool.query(
    `SELECT u.username
     FROM zpm_package_maintainers m
     JOIN zpm_users u ON u.id = m.user_id
     WHERE m.package_name = $1
     ORDER BY u.username`,
    [packageName]
  );
  return {
    ...packageRow,
    maintainers: maintainers.rows.map(row => row.username),
    versions: versions.rows.map(version => ({
      version: version.version,
      compiler: version.compiler,
      main: version.main,
      dependencies: version.dependencies,
      manifest: version.manifest,
      readme: version.readme,
      integrity: version.integrity,
      publishedAt: version.published_at,
      publisher: version.publisher,
      downloadUrl: `${API_BASE}/packages/${encodeURIComponent(packageName)}/versions/${encodeURIComponent(version.version)}/download`
    }))
  };
}

async function packageVersion(pool, packageName, version) {
  if (!validatePackageName(packageName) || !parseVersion(version)) {
    fail(400, "invalid_package_reference", "Package name or version is invalid.");
  }
  const result = await pool.query(
    `SELECT v.package_name, v.version, v.compiler, v.main, v.dependencies,
            v.manifest, v.readme, v.integrity, v.blob_url, v.storage_key,
            v.archive_size, v.published_at,
            u.username AS publisher
     FROM zpm_package_versions v
     JOIN zpm_users u ON u.id = v.published_by
     WHERE v.package_name = $1 AND v.version = $2
       AND v.publication_state = 'published'`,
    [packageName, version]
  );
  const row = result.rows[0];
  if (!row) fail(404, "version_not_found", "Package version was not found.");
  return row;
}

async function manageMaintainer(pool, user, packageName, targetName, request, remove) {
  await requireOwnerOrMaintainer(pool, packageName, user.id);
  const body = remove ? null : await readJson(request);
  const username = normalizeUsername(remove ? targetName : body.username);
  if (!username) fail(400, "invalid_username", "Maintainer username is invalid.");
  await withTransaction(pool, async client => {
    const packageRow = await client.query(
      "SELECT owner_id FROM zpm_packages WHERE name = $1 FOR UPDATE",
      [packageName]
    );
    if (packageRow.rows[0].owner_id !== user.id) {
      fail(403, "owner_required", "Only the package owner can manage maintainers.");
    }
    const target = await client.query(
      "SELECT id FROM zpm_users WHERE username = $1",
      [username]
    );
    if (!target.rowCount) fail(404, "account_not_found", "Developer account was not found.");
    if (target.rows[0].id === user.id) {
      fail(400, "invalid_maintainer", "The package owner is already a maintainer.");
    }
    if (remove) {
      const result = await client.query(
        "DELETE FROM zpm_package_maintainers WHERE package_name = $1 AND user_id = $2",
        [packageName, target.rows[0].id]
      );
      if (!result.rowCount) fail(404, "maintainer_not_found", "Maintainer was not found.");
    } else {
      await client.query(
        `INSERT INTO zpm_package_maintainers (package_name, user_id, added_by)
         VALUES ($1, $2, $3)
         ON CONFLICT (package_name, user_id) DO NOTHING`,
        [packageName, target.rows[0].id, user.id]
      );
    }
    await client.query(
      `INSERT INTO zpm_registry_audit (actor_id, package_name, action, details)
       VALUES ($1, $2, $3, $4::jsonb)`,
      [user.id, packageName, remove ? "remove_maintainer" : "add_maintainer",
        JSON.stringify({ username })]
    );
  });
  return jsonResponse(200, { package: packageName, maintainer: username, removed: remove });
}

function publicOriginValue(value) {
  const configured = value || "https://z-plus-flax.vercel.app";
  try {
    const parsed = new URL(configured);
    if ((parsed.protocol !== "https:" && parsed.hostname !== "localhost" &&
         parsed.hostname !== "127.0.0.1") || parsed.username || parsed.password ||
        parsed.pathname !== "/" || parsed.search || parsed.hash) {
      throw new Error("invalid origin");
    }
    return parsed.origin;
  } catch {
    throw new Error("ZPM_PUBLIC_ORIGIN must be an HTTPS origin.");
  }
}

export function createRegistryHandler({ pool, storage, publicOrigin } = {}) {
  const origin = publicOriginValue(publicOrigin || process.env.ZPM_PUBLIC_ORIGIN);
  return async request => {
    const headers = new Headers({
      "X-Content-Type-Options": "nosniff",
      "Referrer-Policy": "same-origin"
    });
    const requestOrigin = request.headers.get("origin");
    if (requestOrigin === origin) {
      headers.set("Access-Control-Allow-Origin", origin);
      headers.set("Access-Control-Allow-Credentials", "true");
      headers.set("Vary", "Origin");
    }
    if (request.method === "OPTIONS") {
      if (requestOrigin && requestOrigin !== origin) {
        return jsonResponse(403, { error: { code: "invalid_origin", message: "Origin is not allowed." } });
      }
      headers.set("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
      headers.set("Access-Control-Allow-Headers", "Authorization, Content-Type");
      headers.set("Access-Control-Max-Age", "600");
      return new Response(null, { status: 204, headers });
    }
    if (!pool) {
      return jsonResponse(503, {
        error: {
          code: "database_unavailable",
          message: "Registry database is not connected to this Vercel project."
        }
      });
    }
    try {
      if (requestOrigin && requestOrigin !== origin) {
        fail(403, "invalid_origin", "Origin is not allowed.");
      }
      const url = new URL(request.url);
      const rewrittenPath = url.searchParams.has("route");
      const rawPath = rewrittenPath
        ? url.searchParams.get("route") || ""
        : url.pathname === API_BASE
          ? ""
          : url.pathname.startsWith(`${API_BASE}/`)
            ? url.pathname.slice(API_BASE.length + 1)
            : "";
      let segments;
      try {
        segments = rawPath
          ? rawPath.split("/").map(segment =>
            rewrittenPath ? segment : decodeURIComponent(segment))
          : [];
      } catch {
        fail(400, "invalid_path", "Request path encoding is invalid.");
      }
      const method = request.method.toUpperCase();
      if (!["GET", "POST", "DELETE"].includes(method)) {
        return jsonResponse(405, {
          error: { code: "method_not_allowed", message: "Method is not allowed." }
        }, { Allow: "GET, POST, DELETE, OPTIONS" });
      }
      if (segments.length === 0 && method === "GET") {
        return jsonResponse(200, {
          service: "Z+ Package Registry",
          status: "available"
        }, { "Cache-Control": "no-store" });
      }
      if (segments[0] === "auth" && segments[1] === "register" &&
          segments.length === 2 && method === "POST") {
        return await register(pool, request, origin);
      }
      if (segments[0] === "auth" && segments[1] === "login" &&
          segments.length === 2 && method === "POST") {
        return await login(pool, request, origin, false);
      }
      if (segments[0] === "auth" && segments[1] === "cli-login" &&
          segments.length === 2 && method === "POST") {
        return await login(pool, request, origin, true);
      }
      if (segments[0] === "auth" && segments[1] === "logout" &&
          segments.length === 2 && method === "POST") {
        const token = requestToken(request, origin);
        if (token) {
          await pool.query(
            "UPDATE zpm_sessions SET revoked_at = now() WHERE token_hash = $1 AND revoked_at IS NULL",
            [sha256(token)]
          );
        }
        return jsonResponse(200, { loggedOut: true }, {
          "Set-Cookie": sessionCookie("", origin, 0)
        });
      }
      if (segments[0] === "auth" && segments[1] === "me" &&
          segments.length === 2 && method === "GET") {
        const user = await requireUser(pool, request, origin);
        return jsonResponse(200, { user });
      }
      if (segments[0] === "search" && segments.length === 1 && method === "GET") {
        return await searchPackages(pool, url);
      }
      if (segments[0] === "me" && segments[1] === "packages" &&
          segments.length === 2 && method === "GET") {
        const user = await requireUser(pool, request, origin);
        const packages = await pool.query(
          `SELECT p.name, p.description, p.updated_at,
                  (p.owner_id = $1) AS is_owner
           FROM zpm_packages p
           WHERE p.owner_id = $1 OR EXISTS (
             SELECT 1 FROM zpm_package_maintainers m
             WHERE m.package_name = p.name AND m.user_id = $1
           )
           ORDER BY p.name`,
          [user.id]
        );
        const recent = await pool.query(
          `SELECT v.package_name, v.version, v.published_at, u.username AS publisher
           FROM zpm_package_versions v
           JOIN zpm_packages p ON p.name = v.package_name
           JOIN zpm_users u ON u.id = v.published_by
           WHERE v.publication_state = 'published'
             AND (p.owner_id = $1 OR EXISTS (
               SELECT 1 FROM zpm_package_maintainers m
               WHERE m.package_name = p.name AND m.user_id = $1
             ))
           ORDER BY v.published_at DESC, v.package_name, v.version DESC
           LIMIT 20`,
          [user.id]
        );
        return jsonResponse(200, {
          packages: packages.rows,
          recentPublications: recent.rows.map(row => ({
            name: row.package_name,
            version: row.version,
            publishedAt: row.published_at,
            publisher: row.publisher
          }))
        });
      }
      if (segments[0] === "publish" && segments.length === 1 && method === "POST") {
        return await publish(pool, storage, request, origin);
      }
      if (segments[0] === "packages" && segments.length === 2 &&
          method === "GET") {
        return jsonResponse(200, await packageInfo(pool, segments[1]), {
          "Cache-Control": "public, max-age=60, stale-while-revalidate=300"
        });
      }
      if (segments[0] === "packages" && segments.length === 3 &&
          segments[2] === "resolve" && method === "GET") {
        if (!validatePackageName(segments[1])) {
          fail(400, "invalid_name", "Package name is invalid.");
        }
        const range = url.searchParams.get("range") || "*";
        if (!validConstraint(range)) {
          fail(400, "invalid_range", "Package version constraint is invalid.");
        }
        const candidates = await pool.query(
          `SELECT version, integrity
           FROM zpm_package_versions
           WHERE package_name = $1 AND publication_state = 'published'`,
          [segments[1]]
        );
        const selected = candidates.rows
          .filter(candidate => satisfiesVersion(candidate.version, range))
          .sort((left, right) => compareVersions(right.version, left.version))[0];
        if (!selected) {
          fail(404, "version_not_found", "No published version satisfies that constraint.");
        }
        return jsonResponse(200, {
          name: segments[1],
          version: selected.version,
          integrity: selected.integrity
        }, { "Cache-Control": "public, max-age=60" });
      }
      if (segments[0] === "packages" && segments.length === 5 &&
          segments[2] === "versions" && segments[4] === "download" &&
          method === "GET") {
        const version = await packageVersion(pool, segments[1], segments[3]);
        if (version.storage_key) {
          if (!storage || typeof storage.get !== "function") {
            fail(503, "storage_unavailable", "Package storage is not connected to this Vercel project.");
          }
          let archive;
          try {
            archive = Buffer.from(await storage.get(version.storage_key));
          } catch (error) {
            console.error("Unable to read ZPM archive from Neon Object Storage.", error);
            fail(502, "storage_read_failed", "The package archive could not be retrieved.");
          }
          if (archive.length !== Number(version.archive_size) ||
              `sha256:${sha256(archive)}` !== version.integrity) {
            fail(502, "storage_integrity", "The package archive failed its integrity check.");
          }
          return new Response(archive, {
            status: 200,
            headers: {
              "Cache-Control": "public, max-age=3600, immutable",
              "Content-Disposition": `attachment; filename="${version.package_name}-${version.version}.zpm"`,
              "Content-Length": String(archive.length),
              "Content-Type": "application/vnd.zpm.archive",
              "X-Content-Type-Options": "nosniff"
            }
          });
        }
        if (!version.blob_url) {
          fail(502, "storage_unavailable", "The package archive has no configured storage object.");
        }
        return Response.redirect(version.blob_url, 302);
      }
      if (segments[0] === "packages" && segments.length === 4 &&
          segments[2] === "versions" && method === "GET") {
        const version = await packageVersion(pool, segments[1], segments[3]);
        return jsonResponse(200, {
          name: version.package_name,
          version: version.version,
          compiler: version.compiler,
          main: version.main,
          dependencies: version.dependencies,
          manifest: version.manifest,
          readme: version.readme,
          integrity: version.integrity,
          publisher: version.publisher,
          publishedAt: version.published_at,
          downloadUrl: `${API_BASE}/packages/${encodeURIComponent(version.package_name)}/versions/${encodeURIComponent(version.version)}/download`
        }, { "Cache-Control": "public, max-age=300, immutable" });
      }
      if (segments[0] === "packages" && segments.length === 3 &&
          segments[2] === "maintainers" && method === "POST") {
        if (request.headers.get("cookie")) requireBrowserOrigin(request, origin);
        const user = await requireUser(pool, request, origin);
        return await manageMaintainer(pool, user, segments[1], "", request, false);
      }
      if (segments[0] === "packages" && segments.length === 4 &&
          segments[2] === "maintainers" && method === "DELETE") {
        if (request.headers.get("cookie")) requireBrowserOrigin(request, origin);
        const user = await requireUser(pool, request, origin);
        return await manageMaintainer(pool, user, segments[1], segments[3], request, true);
      }
      if (segments[0] === "packages" && segments.length === 3 &&
          segments[2] === "transfer" && method === "POST") {
        if (request.headers.get("cookie")) requireBrowserOrigin(request, origin);
        const user = await requireUser(pool, request, origin);
        const packageName = segments[1];
        const body = await readJson(request);
        const username = normalizeUsername(body.username);
        if (!username) fail(400, "invalid_username", "Target developer username is invalid.");
        await withTransaction(pool, async client => {
          const current = await client.query(
            "SELECT owner_id FROM zpm_packages WHERE name = $1 FOR UPDATE",
            [packageName]
          );
          if (!current.rowCount) fail(404, "package_not_found", "Package was not found.");
          if (current.rows[0].owner_id !== user.id) {
            fail(403, "owner_required", "Only the package owner can transfer ownership.");
          }
          const target = await client.query(
            "SELECT id FROM zpm_users WHERE username = $1",
            [username]
          );
          if (!target.rowCount) fail(404, "account_not_found", "Developer account was not found.");
          await client.query(
            "UPDATE zpm_packages SET owner_id = $2, updated_at = now() WHERE name = $1",
            [packageName, target.rows[0].id]
          );
          await client.query(
            `INSERT INTO zpm_registry_audit (actor_id, package_name, action, details)
             VALUES ($1, $2, 'transfer', $3::jsonb)`,
            [user.id, packageName, JSON.stringify({ to: username })]
          );
        });
        return jsonResponse(200, { package: packageName, owner: username });
      }
      return jsonResponse(404, {
        error: { code: "not_found", message: "Registry endpoint was not found." }
      });
    } catch (error) {
      if (error instanceof RegistryError) {
        const responseHeaders = new Headers(headers);
        for (const [key, value] of Object.entries(error.headers)) {
          responseHeaders.set(key, value);
        }
        responseHeaders.set("Content-Type", "application/json; charset=utf-8");
        responseHeaders.set("Cache-Control", "no-store");
        responseHeaders.set("X-Content-Type-Options", "nosniff");
        return new Response(JSON.stringify({
          error: { code: error.code, message: error.message }
        }), { status: error.status, headers: responseHeaders });
      }
      console.error("ZPM registry request failed.", error);
      return jsonResponse(500, {
        error: { code: "internal_error", message: "The registry request could not be completed." }
      });
    }
  };
}

export { extractArchive, parseVersion, satisfiesVersion };
