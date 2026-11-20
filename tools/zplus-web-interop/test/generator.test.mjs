import assert from "node:assert/strict";
import { mkdtemp, readFile, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { writeFile } from "node:fs/promises";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import test from "node:test";
import { generateArtifacts } from "../src/generator.mjs";

const fixture = {
  openapi: "3.1.0",
  info: { title: "Greeting API", version: "1.0.0" },
  paths: {
    "/greetings/{name}": {
      parameters: [{ name: "name", in: "path", required: true, schema: { type: "string" } }],
      get: {
        operationId: "getGreeting",
        parameters: [{ name: "locale", in: "query", schema: { type: "string" } }],
        responses: {
          "200": {
            description: "Greeting",
            content: { "application/json": { schema: { $ref: "#/components/schemas/Greeting" } } }
          }
        }
      }
    },
    "/echo": {
      post: {
        operationId: "echo",
        requestBody: {
          required: true,
          content: { "application/json": { schema: { $ref: "#/components/schemas/EchoInput" } } }
        },
        responses: {
          "200": {
            description: "Echo",
            content: { "application/json": { schema: { $ref: "#/components/schemas/EchoInput" } } }
          }
        }
      }
    }
  },
  components: {
    schemas: {
      Greeting: {
        type: "object",
        required: ["message"],
        properties: { message: { type: "string" }, tags: { type: "array", items: { type: "string" } } }
      },
      EchoInput: {
        type: "object",
        required: ["value"],
        properties: { value: { type: "string" } },
        additionalProperties: false
      }
    }
  }
};

test("generates ESM operations, TypeScript request/response types and local schema references", () => {
  const output = generateArtifacts(fixture);
  assert.match(output["zplus-api.js"], /export function createClient/);
  assert.match(output["zplus-api.js"], /getGreeting/);
  assert.match(output["zplus-api.js"], /encodeURIComponent/);
  assert.match(output["zplus-api.d.ts"], /export type Greeting =/);
  assert.match(output["zplus-api.d.ts"], /getGreeting\(input: GetGreetingInput\): Promise<Greeting>/);
  assert.match(output["zplus-api.d.ts"], /body: EchoInput/);
});

test("types HTTP header parameters as strings", () => {
  const document = structuredClone(fixture);
  document.paths["/greetings/{name}"].get.parameters.push({
    name: "X-Request-Count",
    in: "header",
    schema: { type: "integer" }
  });

  const declarations = generateArtifacts(document)["zplus-api.d.ts"];
  assert.match(declarations, /"X-Request-Count"\?: string;/);
  assert.doesNotMatch(declarations, /"X-Request-Count"\?: number;/);
});

test("generated Fetch client serializes inputs and exposes HTTP failures", async () => {
  const output = generateArtifacts(fixture);
  const moduleUrl = `data:text/javascript;base64,${Buffer.from(output["zplus-api.js"]).toString("base64")}`;
  const { ApiError, createClient } = await import(moduleUrl);
  const calls = [];
  const client = createClient({
    baseUrl: "https://api.example.test/v1",
    fetch: async (url, init) => {
      calls.push({ url: new URL(url), init });
      return Response.json({ message: "Hello Ada", tags: [] });
    }
  });

  const greeting = await client.getGreeting({ path: { name: "Ada Lovelace" }, query: { locale: "en-US" } });
  assert.equal(greeting.message, "Hello Ada");
  assert.equal(calls[0].url.pathname, "/v1/greetings/Ada%20Lovelace");
  assert.equal(calls[0].url.searchParams.get("locale"), "en-US");
  assert.equal(calls[0].init.method, "GET");

  await client.echo({ body: { value: "round trip" } });
  assert.equal(calls[1].init.headers.get("content-type"), "application/json");
  assert.deepEqual(JSON.parse(calls[1].init.body), { value: "round trip" });

  const failingClient = createClient({
    baseUrl: "https://api.example.test",
    fetch: async () => Response.json({ error: "unauthorized" }, { status: 401 })
  });
  await assert.rejects(failingClient.getGreeting({ path: { name: "Ada" } }), error => {
    assert.ok(error instanceof ApiError);
    assert.equal(error.status, 401);
    assert.deepEqual(error.body, { error: "unauthorized" });
    return true;
  });
});

test("rejects remote refs, missing path parameters, and non-JSON payloads", () => {
  const remoteRef = structuredClone(fixture);
  remoteRef.paths["/echo"].post.requestBody.content["application/json"].schema.$ref = "https://example.test/schema.json";
  assert.throws(() => generateArtifacts(remoteRef), /only local #\/components\/schemas references/);

  const missingPath = structuredClone(fixture);
  missingPath.paths["/greetings/{name}"].parameters = [];
  assert.throws(() => generateArtifacts(missingPath), /missing the required path parameter 'name'/);

  const binary = structuredClone(fixture);
  binary.paths["/echo"].post.requestBody.content = { "application/octet-stream": { schema: { type: "string" } } };
  assert.throws(() => generateArtifacts(binary), /only supports application\/json media types/);
});

test("CLI writes only generated files and requires --force to replace them", async () => {
  const root = await mkdtemp(join(tmpdir(), "zplus-web-interop-"));
  try {
    const spec = join(root, "openapi.json");
    const output = join(root, "generated");
    const script = fileURLToPath(new URL("../bin/zplus-web-interop.mjs", import.meta.url));
    await writeFile(spec, JSON.stringify(fixture));

    const first = spawnSync(process.execPath, [script, "generate", spec, "--out", output], { encoding: "utf8" });
    assert.equal(first.status, 0, first.stderr);
    assert.match(await readFile(join(output, "zplus-api.js"), "utf8"), /getGreeting/);

    const second = spawnSync(process.execPath, [script, "generate", spec, "--out", output], { encoding: "utf8" });
    assert.equal(second.status, 1);
    assert.match(second.stderr, /already exists/);
    const forced = spawnSync(process.execPath, [script, "generate", spec, "--out", output, "--force"], { encoding: "utf8" });
    assert.equal(forced.status, 0, forced.stderr);
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});
