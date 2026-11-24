import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { generateMetadata } from "./generate-ecosystem-metadata.mjs";

const root = resolve(fileURLToPath(new URL("..", import.meta.url)));
const read = path => readFile(resolve(root, path), "utf8");
const metadata = await generateMetadata(root);
const generated = JSON.parse(await read("website/ecosystem-metadata.json"));
const extension = JSON.parse(await read("ZPlus_VS_code_extension/package.json"));
const extensionLock = JSON.parse(await read("ZPlus_VS_code_extension/package-lock.json"));
const html = await read("website/index.html");
const app = await read("website/app.js");
const websiteProducts = await read("website/ecosystem-content.js");
const websiteDocs = await read("website/content.js");
const robots = await read("website/robots.txt");
const sitemap = await read("website/sitemap.xml");
const extensionReadme = await read("ZPlus_VS_code_extension/README.md");
const issueTemplate = await read(".github/ISSUE_TEMPLATE/config.yml");

assert.deepEqual(generated, metadata, "Generated website metadata is stale; run the metadata generator.");
assert.equal(metadata.project.website, "https://z-plus-lang.vercel.app/");
assert.equal(metadata.project.repository, "https://github.com/MiroXdev0/ZPlus");
assert.equal(metadata.components.vscodeExtension.marketplaceId, "MiroXdev0.zplus-language");
assert.equal(extension.homepage, metadata.project.website);
assert.equal(extension.repository.url.replace(/\.git$/, ""), metadata.project.repository);
assert.equal(extension.bugs.url, metadata.project.issueTracker);
assert.equal(extension.publisher, "MiroXdev0");
assert.equal(`${extension.publisher}.${extension.name}`, metadata.components.vscodeExtension.marketplaceId);
assert.equal(extensionLock.version, extension.version);
assert.equal(extensionLock.packages[""].version, extension.version);
assert.deepEqual(extension.contributes.languages.find(item => item.id === metadata.language.identifier).extensions, metadata.language.fileExtensions);
assert.ok(metadata.language.fileExtensions.includes(".zp"));
assert.ok(metadata.language.fileExtensions.includes(".zh"));

for (const value of [
  metadata.project.website,
  metadata.project.repository,
  metadata.project.documentation,
  metadata.project.issueTracker,
  metadata.components.vscodeExtension.marketplaceUrl,
  metadata.releases.page,
  metadata.releases.api,
  metadata.standardLibrary.documentation
]) {
  const url = new URL(value);
  assert.equal(url.protocol, "https:", `${value} must use HTTPS`);
}

assert.match(html, new RegExp(`<meta property="og:url" content="${metadata.project.website.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}">`));
assert.match(html, new RegExp(`<link rel="canonical" href="${metadata.project.website.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}">`));
assert.ok(app.includes('fetch("/ecosystem-metadata.json"'), "Website app must load the generated metadata JSON.");
assert.ok(app.includes("metadata.project.website"), "Website route metadata must use the generated canonical URL.");
assert.ok(app.includes("metadata.components[componentKey].version") && app.includes("components.compiler.version"), "Website component versions must use generated metadata.");
assert.ok(app.includes("metadata.releases.api"), "Website release information must use generated metadata.");
assert.ok(app.includes("metadata.standardLibrary.modules"), "Website standard-library listing must use generated metadata.");
assert.ok(app.includes("metadata.platforms.compilerBuilds"), "Website platform information must use generated metadata.");
for (const component of Object.values(metadata.components)) {
  const escapedVersion = component.version.replaceAll(".", "\\.");
  assert.doesNotMatch(websiteProducts, new RegExp(`\\b${escapedVersion}\\b`), "Website product copy must not duplicate component versions.");
}
assert.doesNotMatch(websiteDocs, new RegExp(metadata.components.vscodeExtension.version.replaceAll(".", "\\.")), "Website docs must not duplicate the current extension version.");
assert.match(robots, new RegExp(`^Sitemap: ${metadata.project.website.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}sitemap\\.xml$`, "m"));
assert.ok(sitemap.includes(`<loc>${metadata.project.website}</loc>`));
assert.ok([...sitemap.matchAll(/<loc>([^<]+)<\/loc>/g)].every(([, url]) => url.startsWith(metadata.project.website)), "Every sitemap URL must use the canonical host.");
assert.ok(extensionReadme.includes(metadata.project.website));
assert.ok(extensionReadme.includes(metadata.components.vscodeExtension.marketplaceUrl));
assert.ok(issueTemplate.includes(metadata.project.website));
assert.ok([...extensionReadme.matchAll(/https:\/\/[A-Za-z0-9-]+\.vercel\.app\//g)]
  .every(([url]) => url === metadata.project.website), "Extension documentation must use the generated canonical website URL.");
assert.doesNotMatch(extensionReadme, /https:\/\/z-plus-flax\.vercel\.app|https:\/\/zpluse\.vercel\.app/i);

const compatibilityStates = new Set(["verified", "assumed", "unverified", "unknown"]);
assert.ok(compatibilityStates.has(metadata.compatibility.extensionToCompiler.status));
assert.ok(compatibilityStates.has(metadata.compatibility.compilerToStandardLibrary.status));
if (metadata.compatibility.extensionToCompiler.status === "unknown" || metadata.compatibility.extensionToCompiler.status === "unverified") {
  assert.equal(metadata.compatibility.extensionToCompiler.compilerRange, null);
  assert.equal(metadata.compatibility.extensionToCompiler.standardLibraryInterface, null);
}
for (const version of Object.values(metadata.components).map(component => component.version)) {
  assert.ok(version, "Each component must have an independently sourced version.");
}

process.stdout.write("Ecosystem links, website consumption, extension metadata, versions, and compatibility policy are consistent.\n");
