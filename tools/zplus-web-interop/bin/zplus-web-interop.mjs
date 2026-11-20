#!/usr/bin/env node
import { access, mkdir, readFile, writeFile } from "node:fs/promises";
import { constants } from "node:fs";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { generateArtifacts } from "../src/generator.mjs";

const help = `Z+ web interoperability generator

Usage:
  zplus-web-interop generate <openapi.json> [--out <directory>] [--force]
  zplus-web-interop --help
  zplus-web-interop --version

Generates a JavaScript ESM Fetch client and TypeScript declarations from a
supported OpenAPI 3.0/3.1 JSON document. External references are not fetched.
`;

async function main(args) {
  if (args.length === 0 || args[0] === "--help" || args[0] === "-h") {
    process.stdout.write(help);
    return;
  }
  if (args[0] === "--version") {
    const packageJson = JSON.parse(await readFile(new URL("../package.json", import.meta.url), "utf8"));
    process.stdout.write(`${packageJson.version}\n`);
    return;
  }
  if (args[0] !== "generate" || !args[1]) {
    throw new Error("expected 'generate <openapi.json>'; run with --help for usage");
  }

  const inputPath = resolve(args[1]);
  let outputPath = resolve("src/generated/zplus-api");
  let force = false;
  for (let index = 2; index < args.length; index += 1) {
    if (args[index] === "--out" && args[index + 1]) {
      outputPath = resolve(args[++index]);
    } else if (args[index] === "--force") {
      force = true;
    } else {
      throw new Error(`unknown or incomplete option '${args[index]}'`);
    }
  }

  const source = await readFile(inputPath, "utf8");
  let document;
  try {
    document = JSON.parse(source);
  } catch (error) {
    throw new Error(`cannot parse '${inputPath}' as JSON: ${error.message}`);
  }
  const artifacts = generateArtifacts(document);
  await mkdir(outputPath, { recursive: true });
  const destinations = Object.keys(artifacts).map(name => resolve(outputPath, name));
  if (!force) {
    for (const destination of destinations) {
      try {
        await access(destination, constants.F_OK);
        throw new Error(`'${destination}' already exists; pass --force to replace generated output`);
      } catch (error) {
        if (error.code !== "ENOENT") throw error;
      }
    }
  }
  for (const [name, content] of Object.entries(artifacts)) {
    await writeFile(resolve(outputPath, name), content, "utf8");
  }
  process.stdout.write(`Generated ${Object.keys(artifacts).join(" and ")} in ${outputPath}\n`);
}

main(process.argv.slice(2)).catch(error => {
  process.stderr.write(`zplus-web-interop: ${error.message}\n`);
  process.exitCode = 1;
});
