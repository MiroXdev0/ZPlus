import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { mkdtemp, mkdir, readFile, rm, writeFile } from "node:fs/promises";
import { createServer } from "node:http";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { PGlite } from "@electric-sql/pglite";
import { createRegistryHandler } from "../lib/registry.js";

const [zpmArgument, zpbuildArgument, zpArgument] = process.argv.slice(2);
if (!zpmArgument || !zpbuildArgument || !zpArgument) {
  throw new Error("Usage: node tests/registry-client-e2e.mjs <zpm> <zpbuild> <zp>");
}

const zpm = resolve(zpmArgument);
const zpbuild = resolve(zpbuildArgument);
const zp = resolve(zpArgument);
const temporaryRoot = await mkdtemp(join(tmpdir(), "zpm-registry-e2e-"));
const cacheDirectory = join(temporaryRoot, "cache");
const bootstrapEnv = {
  ...process.env,
  ZPM_CACHE_DIR: cacheDirectory
};
const database = new PGlite();
const pool = {
  query: (...args) => database.query(...args),
  connect: async () => ({
    query: (...args) => database.query(...args),
    release() {}
  })
};
const objects = new Map();
let origin;
const storage = {
  async put(key, body) {
    objects.set(key, Buffer.from(body));
  },
  async get(key) {
    const body = objects.get(key);
    if (!body) throw new Error("object not found");
    return Buffer.from(body);
  },
  async del(key) {
    objects.delete(key);
  }
};

function jsonResponse(response) {
  return response.arrayBuffer().then(body => Buffer.from(body));
}

function run(executable, args, cwd, env) {
  return new Promise((resolveRun, rejectRun) => {
    const child = spawn(executable, args, { cwd, env, windowsHide: true });
    let stdout = "";
    let stderr = "";
    let settled = false;
    const timeout = setTimeout(() => {
      child.kill();
      settled = true;
      rejectRun(new Error(`${executable} ${args.join(" ")} timed out`));
    }, 120000);
    child.stdout.setEncoding("utf8").on("data", chunk => { stdout += chunk; });
    child.stderr.setEncoding("utf8").on("data", chunk => { stderr += chunk; });
    child.once("error", error => {
      if (settled) return;
      settled = true;
      clearTimeout(timeout);
      rejectRun(error);
    });
    child.once("close", (code, signal) => {
      if (settled) return;
      settled = true;
      clearTimeout(timeout);
      if (code !== 0) {
        rejectRun(new Error(
          `${executable} ${args.join(" ")} failed (${code ?? signal}):\n${stdout}${stderr}`
        ));
      } else {
        resolveRun(stdout);
      }
    });
  });
}

function registryRequest(path, { method = "GET", headers = {}, body } = {}) {
  return new Request(`${origin}/api/registry${path}`, {
    method,
    headers,
    body
  });
}

async function initializePackage(name, directory, source, dependencies = {}) {
  await mkdir(directory, { recursive: true });
  await run(zpm, ["init", name], directory, bootstrapEnv);
  await mkdir(join(directory, "src"), { recursive: true });
  await writeFile(join(directory, "src", "main.zh"), source);
  const manifestPath = join(directory, "zpm.json");
  const manifest = JSON.parse(await readFile(manifestPath, "utf8"));
  manifest.dependencies = dependencies;
  await writeFile(manifestPath, `${JSON.stringify(manifest, null, 2)}\n`);
}

async function publishArchive(directory, name) {
  const archivePath = join(directory, `${name}.zpm`);
  await run(zpm, ["pack", directory, archivePath], directory, bootstrapEnv);
  const archive = await readFile(archivePath);
  const response = await handler(registryRequest("/publish", {
    method: "POST",
    headers: {
      authorization: `Bearer ${cliToken}`,
      "content-type": "application/vnd.zpm.archive"
    },
    body: archive
  }));
  assert.equal(response.status, 201, await response.text());
  return archivePath;
}

let handler;
let cliToken;
let server;
try {
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

  server = createServer(async (request, response) => {
    try {
      const chunks = [];
      for await (const chunk of request) chunks.push(chunk);
      const body = Buffer.concat(chunks);
      const apiRequest = new Request(`${origin}${request.url}`, {
        method: request.method,
        headers: request.headers,
        ...(body.length ? { body, duplex: "half" } : {})
      });
      const result = await handler(apiRequest);
      response.writeHead(result.status, Object.fromEntries(result.headers));
      response.end(await jsonResponse(result));
    } catch (error) {
      response.writeHead(500, { "Content-Type": "application/json" });
      response.end(JSON.stringify({
        error: { code: "test_registry_error", message: error.message }
      }));
    }
  });
  await new Promise((resolveListen, reject) => {
    server.once("error", reject);
    server.listen(0, "0.0.0.0", resolveListen);
  });
  const address = server.address();
  origin = `http://localhost:${address.port}`;
  handler = createRegistryHandler({ pool, storage, publicOrigin: origin });

  const account = await handler(registryRequest("/auth/register", {
    method: "POST",
    headers: { "content-type": "application/json", origin },
    body: JSON.stringify({
      username: "registry-owner",
      email: "registry-owner@example.test",
      password: "registry-test-password"
    })
  }));
  assert.equal(account.status, 201);
  const login = await handler(registryRequest("/auth/cli-login", {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({
      email: "registry-owner@example.test",
      password: "registry-test-password"
    })
  }));
  assert.equal(login.status, 200);
  cliToken = (await login.json()).token;

  const coreDirectory = join(temporaryRoot, "registry-core");
  await initializePackage("registry-core", coreDirectory,
    "start {\n" +
    "    pub fn greeting(): string {\n" +
    "        return \"Registry transitive package works\"\n" +
    "    } end fn\n" +
    "} stop\n");
  await run(zpbuild, ["build"], coreDirectory, bootstrapEnv);
  await publishArchive(coreDirectory, "registry-core");

  const fixtureDirectory = join(temporaryRoot, "registry-fixture");
  await initializePackage("registry-fixture", fixtureDirectory,
    "use \"registry-core\" as core\n" +
    "start {\n" +
    "    pub fn greeting(): string {\n" +
    "        return core.greeting()\n" +
    "    } end fn\n" +
    "} stop\n");
  await run(zpm, ["add", coreDirectory], fixtureDirectory, bootstrapEnv);
  await run(zpbuild, ["build"], fixtureDirectory, bootstrapEnv);
  await run(zpm, ["remove", "registry-core"], fixtureDirectory, bootstrapEnv);
  const fixtureManifestPath = join(fixtureDirectory, "zpm.json");
  const fixtureManifest = JSON.parse(await readFile(fixtureManifestPath, "utf8"));
  fixtureManifest.dependencies = { "registry-core": "^0.1.0" };
  await writeFile(
    fixtureManifestPath,
    `${JSON.stringify(fixtureManifest, null, 2)}\n`
  );
  await publishArchive(fixtureDirectory, "registry-fixture");

  const clientDirectory = join(temporaryRoot, "consumer project");
  await mkdir(clientDirectory, { recursive: true });
  await run(zpm, ["init", "registry-consumer"], clientDirectory, bootstrapEnv);
  await mkdir(join(clientDirectory, "src"), { recursive: true });
  await writeFile(join(clientDirectory, "src", "main.zp"),
    "use \"registry-fixture\" as fixture\n" +
    "start {\n" +
    "    say(fixture.greeting())\n" +
    "} stop\n");

  const env = {
    ...bootstrapEnv,
    ZPM_REGISTRY_URL: `${origin}/api/registry`,
    ZPM_CACHE_DIR: cacheDirectory
  };
  assert.match(await run(zpm, ["search", "registry-fixture"], clientDirectory, env),
    /registry-fixture/);
  assert.match(await run(zpm, ["info", "registry-fixture"], clientDirectory, env),
    /0\.1\.0/);
  await run(zpm, ["add", "registry-fixture"], clientDirectory, env);
  assert.match(
    await readFile(join(clientDirectory, "zpm-lock.json"), "utf8"),
    /registry-core/
  );
  assert.ok(await readFile(
    join(clientDirectory, "z+_packages", "registry-core", "src", "main.zh")
  ));
  assert.ok(await readFile(
    join(clientDirectory, "z+_packages", "registry-fixture", "src", "main.zh")
  ));

  await run(zpbuild, ["build"], clientDirectory, env);
  assert.match(await run(zpbuild, ["run"], clientDirectory, env),
    /Registry transitive package works/);
  assert.match(await run(zp, ["run", "src/main.zp"], clientDirectory, env),
    /Registry transitive package works/);

  await rm(join(clientDirectory, "z+_packages", "registry-core"), {
    recursive: true,
    force: true
  });
  await rm(join(clientDirectory, "z+_packages", "registry-fixture"), {
    recursive: true,
    force: true
  });
  await new Promise(resolveClose => server.close(resolveClose));
  server = null;
  await run(zpm, ["install", "--locked"], clientDirectory, env);
  assert.ok(await readFile(
    join(clientDirectory, "z+_packages", "registry-core", "src", "main.zh")
  ));
  assert.ok(await readFile(
    join(clientDirectory, "z+_packages", "registry-fixture", "src", "main.zh")
  ));

  console.log("ZPM registry client end-to-end test passed.");
} finally {
  if (server) await new Promise(resolveClose => server.close(resolveClose));
  await database.close();
  await rm(temporaryRoot, { recursive: true, force: true });
}
