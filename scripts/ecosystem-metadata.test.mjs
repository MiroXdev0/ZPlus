import assert from "node:assert/strict";
import { mkdtemp, mkdir, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { execFile } from "node:child_process";
import { promisify } from "node:util";
import test from "node:test";
import { generateMetadata } from "./generate-ecosystem-metadata.mjs";

const execFileAsync = promisify(execFile);

async function createFixture() {
  const root = await mkdtemp(resolve(tmpdir(), "zplus-ecosystem-"));
  await mkdir(resolve(root, "ecosystem"), { recursive: true });
  await mkdir(resolve(root, "website"), { recursive: true });
  await mkdir(resolve(root, ".github/ISSUE_TEMPLATE"), { recursive: true });
  await mkdir(resolve(root, "ZPlus_VS_code_extension"), { recursive: true });
  await mkdir(resolve(root, "stdlib/zp/math"), { recursive: true });
  await writeFile(resolve(root, "ecosystem/metadata.schema.json"), await readFile(new URL("../ecosystem/metadata.schema.json", import.meta.url)));
  await writeFile(resolve(root, "stdlib/zp/math/math.zh"), "start {} stop\n");
  await writeFile(resolve(root, "CMakeLists.txt"), `project(ZPlus\n    VERSION 2.4.6\n    LANGUAGES CXX\n)\nset(ZPLUS_OFFICIAL_WEBSITE_URL "https://z-plus-lang.vercel.app/")\nset(ZPLUS_PREVIOUS_WEBSITE_URL "https://z-plus-flax.vercel.app/")\nset(ZPLUS_COMPILER_REPOSITORY_URL "https://github.com/MiroXdev0/ZPlus")\nset(ZPLUS_RELEASE_REPOSITORY_URL "https://github.com/MiroXdev0/ZPluse-release")\nset(ZPM_DEFAULT_REGISTRY_URL "\${ZPLUS_OFFICIAL_WEBSITE_URL}api/registry")\nset(ZBUILD_VERSION "3.5.7")\nset(ZPM_VERSION "4.6.8")\n`);
  await writeFile(resolve(root, "ZPlus_VS_code_extension/README.md"), "Z+ official site: https://old-site.vercel.app/\nMarketplace: https://marketplace.visualstudio.com/items?itemName=OldPublisher.old-name\n");
  await writeFile(resolve(root, ".github/ISSUE_TEMPLATE/config.yml"), "contact_links:\n  - name: Z+ documentation\n    url: https://old-site.vercel.app/\n");
  await writeFile(resolve(root, "ZPlus_VS_code_extension/package.json"), JSON.stringify({
    name: "zplus-language",
    displayName: "Z+ Programming Language",
    version: "5.7.9",
    publisher: "MiroXdev0",
    homepage: "https://old-site.vercel.app/",
    repository: { type: "git", url: "https://github.com/old/project.git", directory: "ZPlus_VS_code_extension" },
    bugs: { url: "https://github.com/old/project/issues" },
    engines: { vscode: "^1.80.0" },
    contributes: { languages: [{ id: "zplus", extensions: [".zp", ".zh"] }] }
  }));
  return root;
}

test("extracts every component version from its independent authoritative source", async () => {
  const root = await createFixture();
  try {
    const metadata = await generateMetadata(root);
    assert.equal(metadata.components.compiler.version, "2.4.6");
    assert.equal(metadata.components.zpbuild.version, "3.5.7");
    assert.equal(metadata.components.zpm.version, "4.6.8");
    assert.equal(metadata.components.vscodeExtension.version, "5.7.9");
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

test("version edits flow into newly generated metadata without editing other sources", async () => {
  const root = await createFixture();
  try {
    const first = await generateMetadata(root);
    const cmakePath = resolve(root, "CMakeLists.txt");
    const packagePath = resolve(root, "ZPlus_VS_code_extension/package.json");
    const cmake = (await readFile(cmakePath, "utf8"))
      .replace("VERSION 2.4.6", "VERSION 2.4.7")
      .replace('ZBUILD_VERSION "3.5.7"', 'ZBUILD_VERSION "3.5.8"')
      .replace('ZPM_VERSION "4.6.8"', 'ZPM_VERSION "4.6.9"');
    await writeFile(cmakePath, cmake);
    const manifest = JSON.parse(await readFile(packagePath, "utf8"));
    manifest.version = "5.8.0";
    await writeFile(packagePath, JSON.stringify(manifest));
    const updated = await generateMetadata(root);
    assert.deepEqual(Object.values(updated.components).map(component => component.version), ["2.4.7", "4.6.9", "3.5.8", "5.8.0"]);
    assert.notDeepEqual(updated.components, first.components);
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

test("fails with a clear message when an authoritative version is missing", async () => {
  const root = await createFixture();
  try {
    await writeFile(resolve(root, "CMakeLists.txt"), "project(ZPlus LANGUAGES CXX)\nset(ZBUILD_VERSION \"3.5.7\")\nset(ZPM_VERSION \"4.6.8\")\n");
    await assert.rejects(generateMetadata(root), /could not extract a valid compiler version from CMakeLists\.txt/);
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});

test("CI check mode rejects stale generated metadata", async () => {
  const root = await createFixture();
  try {
    const metadata = await generateMetadata(root);
    const canonical = metadata.project.website;
    await writeFile(resolve(root, "website/ecosystem-metadata.json"), `${JSON.stringify(metadata, null, 2)}\n`);
    await writeFile(resolve(root, "website/index.html"), `<meta property="og:url" content="${canonical}">\n<meta property="og:image" content="${metadata.project.socialImage}">\n<link rel="canonical" href="${canonical}">\n`);
    await writeFile(resolve(root, "website/robots.txt"), `Sitemap: ${new URL("sitemap.xml", canonical).href}\n`);
    await writeFile(resolve(root, "website/sitemap.xml"), `<urlset><url><loc>${canonical}</loc></url></urlset>\n`);
    const generator = fileURLToPath(new URL("./generate-ecosystem-metadata.mjs", import.meta.url));
    await execFileAsync(process.execPath, [generator, "--root", root]);
    const generatedMetadata = await readFile(resolve(root, "website/ecosystem-metadata.json"), "utf8");
    const generatedManifest = await readFile(resolve(root, "ZPlus_VS_code_extension/package.json"), "utf8");
    const generatedReadme = await readFile(resolve(root, "ZPlus_VS_code_extension/README.md"), "utf8");
    const generatedIssueTemplate = await readFile(resolve(root, ".github/ISSUE_TEMPLATE/config.yml"), "utf8");
    await execFileAsync(process.execPath, [generator, "--root", root]);
    assert.equal(await readFile(resolve(root, "website/ecosystem-metadata.json"), "utf8"), generatedMetadata);
    assert.equal(await readFile(resolve(root, "ZPlus_VS_code_extension/package.json"), "utf8"), generatedManifest);
    assert.equal(await readFile(resolve(root, "ZPlus_VS_code_extension/README.md"), "utf8"), generatedReadme);
    assert.equal(await readFile(resolve(root, ".github/ISSUE_TEMPLATE/config.yml"), "utf8"), generatedIssueTemplate);
    assert.equal(JSON.parse(generatedManifest).homepage, "https://z-plus-lang.vercel.app/");
    assert.match(generatedReadme, /https:\/\/z-plus-lang\.vercel\.app\//);
    assert.match(generatedReadme, /MiroXdev0\.zplus-language/);
    assert.match(generatedIssueTemplate, /https:\/\/z-plus-lang\.vercel\.app\//);
    await execFileAsync(process.execPath, [
      generator, "--check", "--root", root
    ]);
    const issueTemplatePath = resolve(root, ".github/ISSUE_TEMPLATE/config.yml");
    const staleIssueTemplate = generatedIssueTemplate.replace("https://z-plus-lang.vercel.app/", "https://stale-site.vercel.app/");
    await writeFile(issueTemplatePath, staleIssueTemplate);
    await assert.rejects(
      execFileAsync(process.execPath, [generator, "--check", "--root", root]),
      error => /GitHub issue template documentation URL are stale/.test(error.stderr)
    );
    const cmakePath = resolve(root, "CMakeLists.txt");
    const cmake = (await readFile(cmakePath, "utf8")).replace("VERSION 2.4.6", "VERSION 2.4.7");
    await writeFile(cmakePath, cmake);
    await assert.rejects(
      execFileAsync(process.execPath, [
        generator, "--check", "--root", root
      ]),
      error => /website\/ecosystem-metadata\.json is stale/.test(error.stderr)
    );
  } finally {
    await rm(root, { recursive: true, force: true });
  }
});
