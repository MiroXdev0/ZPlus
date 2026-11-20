import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { spawn } from "node:child_process";
import { fileURLToPath } from "node:url";
import test from "node:test";
import { generateArtifacts } from "../../tools/zplus-web-interop/src/generator.mjs";

const root = fileURLToPath(new URL("../../", import.meta.url));
const source = fileURLToPath(new URL("./server.zp", import.meta.url));
const executable = process.env.ZP_EXECUTABLE;

test("generated TS/JS client talks to the Z+ HTTP/JSON server", {
  skip: !executable && "Set ZP_EXECUTABLE to a built zp executable to run this integration test."
}, async t => {
  const server = spawn(executable, ["run", source], { cwd: root, windowsHide: true });
  let output = "";
  server.stdout.setEncoding("utf8");
  server.stderr.setEncoding("utf8");
  server.stdout.on("data", chunk => { output += chunk; });
  server.stderr.on("data", chunk => { output += chunk; });
  const serverExit = new Promise((resolve, reject) => {
    server.on("error", reject);
    server.on("exit", code => {
      if (code === 0) resolve();
      else reject(new Error(`Z+ server exited with ${code}. ${output}`));
    });
  });
  t.after(() => {
    if (server.exitCode === null) server.kill();
  });

  const spec = JSON.parse(await readFile(new URL("./openapi.json", import.meta.url), "utf8"));
  const generated = generateArtifacts(spec);
  const moduleUrl = `data:text/javascript;base64,${Buffer.from(generated["zplus-api.js"]).toString("base64")}`;
  const { createClient } = await import(moduleUrl);
  const client = createClient({
    baseUrl: "http://127.0.0.1:18789",
    fetch: async (...args) => {
      let lastError;
      for (let attempt = 0; attempt < 100; attempt += 1) {
        try {
          return await fetch(...args);
        } catch (error) {
          lastError = error;
          if (server.exitCode !== null) break;
          await new Promise(resolve => setTimeout(resolve, 50));
        }
      }
      throw new Error(`Could not connect to the Z+ server. ${output}`, { cause: lastError });
    }
  });

  assert.deepEqual(await client.getGreeting({ path: { name: "Ada" }, query: { locale: "en" } }), {
    message: "Hello Ada",
    locale: "en"
  });
  assert.deepEqual(await client.echo({ body: { value: "round trip" } }), { value: "round trip" });

  let timeout;
  const timedOut = new Promise((_, reject) => {
    timeout = setTimeout(() => {
      server.kill();
      reject(new Error(`Z+ server did not exit after two requests. ${output}`));
    }, 10000);
    timeout.unref();
  });
  try {
    await Promise.race([serverExit, timedOut]);
  } finally {
    clearTimeout(timeout);
  }
});
