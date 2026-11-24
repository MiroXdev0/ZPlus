import { readFile, readdir, writeFile } from "node:fs/promises";
import { resolve, relative, sep } from "node:path";
import { fileURLToPath } from "node:url";

const repositoryRoot = resolve(fileURLToPath(new URL("..", import.meta.url)));
const outputPath = resolve(repositoryRoot, "website/ecosystem-metadata.json");
const semverPattern = /^\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?$/;

function fail(message) {
  throw new Error(`Ecosystem metadata: ${message}`);
}

async function readText(root, path) {
  try {
    return await readFile(resolve(root, path), "utf8");
  } catch (error) {
    fail(`cannot read ${path}: ${error.message}`);
  }
}

function extractVersion(text, expression, label, source) {
  const match = text.match(expression);
  const version = match?.[1]?.trim();
  if (!version || !semverPattern.test(version)) {
    fail(`could not extract a valid ${label} version from ${source}`);
  }
  return version;
}

function extractCmakeString(text, variable, label) {
  const match = text.match(new RegExp(`set\\s*\\(\\s*${variable}\\s+"([^"]+)"\\s*\\)`, "i"));
  if (!match?.[1]) fail(`could not extract ${label} from CMakeLists.txt (${variable})`);
  return match[1];
}

function replaceRequired(source, expression, replacement, label) {
  const matches = [...source.matchAll(new RegExp(expression.source, `${expression.flags}g`))];
  if (matches.length !== 1) fail(`expected exactly one ${label} in website source, found ${matches.length}`);
  return source.replace(expression, replacement);
}

function syncExtensionPackage(source, metadata) {
  let result = replaceRequired(source,
    /("homepage"\s*:\s*)"(?:[^"\\]|\\.)*"/,
    `$1${JSON.stringify(metadata.project.website)}`, "extension homepage");
  result = replaceRequired(result,
    /("repository"\s*:\s*\{[\s\S]*?"url"\s*:\s*)"(?:[^"\\]|\\.)*"/,
    `$1${JSON.stringify(`${metadata.project.repository}.git`)}`, "extension repository URL");
  result = replaceRequired(result,
    /("bugs"\s*:\s*\{[\s\S]*?"url"\s*:\s*)"(?:[^"\\]|\\.)*"/,
    `$1${JSON.stringify(metadata.project.issueTracker)}`, "extension issue tracker URL");
  return result;
}

function syncExtensionReadme(source, metadata) {
  const websitePattern = /https:\/\/[A-Za-z0-9-]+\.vercel\.app\//g;
  const websiteMatches = [...source.matchAll(websitePattern)];
  if (!websiteMatches.length) fail("extension README has no official website links to synchronize");
  let result = source.replace(websitePattern, metadata.project.website);
  result = replaceRequired(result,
    /https:\/\/marketplace\.visualstudio\.com\/items\?itemName=[A-Za-z0-9.-]+/,
    metadata.components.vscodeExtension.marketplaceUrl,
    "extension Marketplace link");
  return result;
}

function syncIssueTemplate(source, metadata) {
  return replaceRequired(source,
    /(\s+url:\s*)https:\/\/[A-Za-z0-9.-]+\.vercel\.app\/?/,
    `$1${metadata.project.website}`,
    "GitHub issue template documentation link");
}

async function writeIfChanged(path, expected) {
  let current;
  try {
    current = await readFile(path, "utf8");
  } catch (error) {
    if (error.code !== "ENOENT") fail(`cannot read ${path}: ${error.message}`);
  }
  if (current !== expected) await writeFile(path, expected, "utf8");
}

function validateSchema(value, schema, schemaRoot, location = "$") {
  if (schema.$ref) {
    const target = schema.$ref.replace(/^#\//, "").split("/").reduce((node, part) => node?.[part], schemaRoot);
    if (!target) fail(`schema reference ${schema.$ref} is invalid`);
    return validateSchema(value, target, schemaRoot, location);
  }
  const types = Array.isArray(schema.type) ? schema.type : schema.type ? [schema.type] : [];
  const actualType = value === null ? "null" : Array.isArray(value) ? "array" : typeof value;
  const typeMatches = types.some(type => type === "number"
    ? typeof value === "number"
    : type === "integer"
      ? Number.isInteger(value)
      : type === actualType);
  if (types.length && !typeMatches) {
    fail(`${location} must be ${types.join(" or ")}, got ${actualType}`);
  }
  if (schema.const !== undefined && value !== schema.const) fail(`${location} must equal ${JSON.stringify(schema.const)}`);
  if (schema.enum && !schema.enum.includes(value)) fail(`${location} must be one of: ${schema.enum.join(", ")}`);
  if (typeof value === "string") {
    if (schema.minLength !== undefined && value.length < schema.minLength) fail(`${location} must not be empty`);
    if (schema.pattern && !new RegExp(schema.pattern).test(value)) fail(`${location} has an invalid format`);
    if (schema.format === "uri") {
      try {
        new URL(value);
      } catch {
        fail(`${location} must be a URL`);
      }
    }
  }
  if (Array.isArray(value)) {
    if (schema.minItems !== undefined && value.length < schema.minItems) fail(`${location} must contain at least ${schema.minItems} item(s)`);
    if (schema.uniqueItems && new Set(value.map(item => JSON.stringify(item))).size !== value.length) fail(`${location} must not contain duplicate items`);
    if (schema.items) value.forEach((item, index) => validateSchema(item, schema.items, schemaRoot, `${location}[${index}]`));
  }
  if (value && typeof value === "object" && !Array.isArray(value)) {
    for (const key of schema.required || []) {
      if (!Object.hasOwn(value, key)) fail(`${location}.${key} is required`);
    }
    for (const [key, item] of Object.entries(value)) {
      const propertySchema = schema.properties?.[key];
      if (propertySchema) validateSchema(item, propertySchema, schemaRoot, `${location}.${key}`);
      else if (schema.additionalProperties === false) fail(`${location}.${key} is not allowed by the schema`);
    }
  }
}

async function listModules(root) {
  const base = resolve(root, "stdlib/zp");
  const modules = [];
  async function visit(directory) {
    let entries;
    try {
      entries = await readdir(directory, { withFileTypes: true });
    } catch (error) {
      fail(`cannot inspect stdlib/zp: ${error.message}`);
    }
    for (const entry of entries) {
      const path = resolve(directory, entry.name);
      if (entry.isDirectory()) await visit(path);
      else if (entry.isFile() && entry.name.endsWith(".zh")) {
        const parts = relative(base, path).split(sep).join("/").replace(/\.zh$/i, "").split("/");
        if (parts.length > 1 && parts.at(-1) === parts.at(-2)) parts.pop();
        modules.push(`zp.${parts.join(".")}`);
      }
    }
  }
  await visit(base);
  if (!modules.length) fail("no standard-library modules were found under stdlib/zp");
  return [...new Set(modules)].sort((left, right) => left.localeCompare(right));
}

export async function generateMetadata(root = repositoryRoot) {
  const cmake = await readText(root, "CMakeLists.txt");
  const packageText = await readText(root, "ZPlus_VS_code_extension/package.json");
  let extensionPackage;
  try {
    extensionPackage = JSON.parse(packageText);
  } catch (error) {
    fail(`ZPlus_VS_code_extension/package.json is invalid JSON: ${error.message}`);
  }

  const compilerVersion = extractVersion(cmake, /project\s*\(\s*ZPlus\s+VERSION\s+"?([^\s)";]+)/i, "compiler", "CMakeLists.txt");
  const zpmVersion = extractVersion(cmake, /set\s*\(\s*ZPM_VERSION\s+"([^"]+)"\s*\)/i, "ZPM", "CMakeLists.txt");
  const zpbuildVersion = extractVersion(cmake, /set\s*\(\s*ZBUILD_VERSION\s+"([^"]+)"\s*\)/i, "ZPBuild", "CMakeLists.txt");
  if (typeof extensionPackage.version !== "string" || !semverPattern.test(extensionPackage.version)) {
    fail("could not extract a valid VS Code extension version from ZPlus_VS_code_extension/package.json");
  }
  if (typeof extensionPackage.publisher !== "string" || !/^[A-Za-z0-9-]+$/.test(extensionPackage.publisher)) {
    fail("could not extract a valid VS Code Marketplace publisher from ZPlus_VS_code_extension/package.json");
  }
  const websiteUrl = extractCmakeString(cmake, "ZPLUS_OFFICIAL_WEBSITE_URL", "canonical website URL");
  const previousWebsiteUrl = extractCmakeString(cmake, "ZPLUS_PREVIOUS_WEBSITE_URL", "previous website URL");
  const repository = extractCmakeString(cmake, "ZPLUS_COMPILER_REPOSITORY_URL", "compiler repository URL");
  const releaseRepository = extractCmakeString(cmake, "ZPLUS_RELEASE_REPOSITORY_URL", "release repository URL");
  const expectedRegistryUrl = `${websiteUrl}api/registry`;
  if (!cmake.includes('set(ZPM_DEFAULT_REGISTRY_URL "${ZPLUS_OFFICIAL_WEBSITE_URL}api/registry")')) {
    fail("ZPM_DEFAULT_REGISTRY_URL must derive from ZPLUS_OFFICIAL_WEBSITE_URL in CMakeLists.txt");
  }

  const language = extensionPackage.contributes?.languages?.find(item => item.id === "zplus");
  if (!language || !Array.isArray(language.extensions) || !language.extensions.length) {
    fail("could not extract the Z+ language identifier and file extensions from the extension manifest");
  }
  const releasePath = new URL(releaseRepository).pathname;
  const marketplaceId = `${extensionPackage.publisher}.${extensionPackage.name}`;
  const metadata = {
    schemaVersion: 1,
    project: {
      name: "Z+",
      description: "A statically typed programming language implemented in C++20, with a compiler, interpreter, native backend, package manager, build system, and editor tools.",
      website: websiteUrl,
      previousWebsite: previousWebsiteUrl,
      repository,
      documentation: new URL("docs", websiteUrl).href,
      issueTracker: `${repository}/issues`,
      registryApi: expectedRegistryUrl,
      socialImage: new URL("assets/favicon-dark.png", websiteUrl).href
    },
    components: {
      compiler: { name: "Z+ Compiler", version: compilerVersion, source: "CMakeLists.txt:project(VERSION)" },
      zpm: { name: "ZPM", version: zpmVersion, source: "CMakeLists.txt:ZPM_VERSION" },
      zpbuild: { name: "ZPBuild", version: zpbuildVersion, source: "CMakeLists.txt:ZBUILD_VERSION" },
      vscodeExtension: {
        name: extensionPackage.displayName || "VS Code extension",
        version: extensionPackage.version,
        marketplaceId,
        marketplaceUrl: `https://marketplace.visualstudio.com/items?itemName=${encodeURIComponent(marketplaceId)}`,
        source: "ZPlus_VS_code_extension/package.json"
      }
    },
    platforms: {
      compilerBuilds: [
        { operatingSystem: "Windows", architecture: "x86_64", status: "unverified", evidence: [".github/workflows/ci.yml", "README.md"] },
        { operatingSystem: "Linux", architecture: "x86_64", status: "unverified", evidence: [".github/workflows/ci.yml", "README.md"] }
      ],
      nativeBackend: [
        { operatingSystem: "Windows", architecture: "x86_64", status: "supported", evidence: ["docs/BACKEND.md", ".github/workflows/ci.yml"] },
        { operatingSystem: "Linux", architecture: "x86_64", status: "partial", evidence: ["docs/BACKEND.md", ".github/workflows/ci.yml"] }
      ],
      extension: {
        vscodeEngine: extensionPackage.engines?.vscode || "",
        testedInCi: ["ubuntu-latest"]
      }
    },
    installations: [
      { id: "source-build", method: "Build from source with CMake", url: repository, documentation: `${repository}/blob/main/README.md#build-from-source`, platforms: ["Windows x86_64", "Linux x86_64"] },
      { id: "windows-x86_64-installer", method: "Windows x86_64 installer from official releases", url: `${releaseRepository}/releases`, documentation: new URL("docs/installation", websiteUrl).href, platforms: ["Windows x86_64"] },
      { id: "vscode-marketplace", method: "Visual Studio Marketplace", url: `https://marketplace.visualstudio.com/items?itemName=${encodeURIComponent(marketplaceId)}`, documentation: new URL("vscode", websiteUrl).href, platforms: [] }
    ],
    language: { identifier: language.id, fileExtensions: [...language.extensions] },
    standardLibrary: {
      path: "stdlib/zp",
      version: null,
      modules: await listModules(root),
      documentation: new URL("docs/standard-library", websiteUrl).href
    },
    compatibility: {
      extensionToCompiler: {
        status: "unknown",
        compilerRange: null,
        standardLibraryInterface: null,
        evidence: "The extension delegates to the installed compiler. No compiler-version range or standard-library interface pin is declared in the extension source, documentation, or tests."
      },
      compilerToStandardLibrary: {
        status: "verified",
        compilerRange: compilerVersion,
        standardLibraryInterface: "co-shipped repository sources; no separate standard-library version is declared",
        evidence: "Compiler and standard library are built and tested from the same repository checkout; stdlib/zp has no independent version declaration."
      }
    },
    releases: {
      repository: releaseRepository,
      page: `${releaseRepository}/releases`,
      api: `https://api.github.com/repos/${releasePath.slice(1)}/releases?per_page=100`,
      downloadPathPrefix: `${releasePath}/releases/download/`,
      assetTargets: [{
        id: "windows-x86_64",
        label: "Windows x86_64",
        filePattern: "\\.exe$",
        installerPattern: "\\b(?:setup|installer)\\b",
        platformPattern: "\\b(?:windows|win64)\\b",
        architecturePattern: "\\b(?:x64|amd64|x86\\s+64)\\b",
        conflictPattern: "\\b(?:linux|macos|darwin|osx|arm64|aarch64|i386|i686|win32|x86\\s+32)\\b"
      }]
    }
  };

  const schema = JSON.parse(await readText(root, "ecosystem/metadata.schema.json"));
  validateSchema(metadata, schema, schema);
  for (const [label, url] of Object.entries({
    website: metadata.project.website,
    previousWebsite: metadata.project.previousWebsite,
    repository: metadata.project.repository,
    documentation: metadata.project.documentation,
    issueTracker: metadata.project.issueTracker,
    registryApi: metadata.project.registryApi,
    socialImage: metadata.project.socialImage,
    marketplace: metadata.components.vscodeExtension.marketplaceUrl,
    releases: metadata.releases.page,
    releaseApi: metadata.releases.api
  })) {
    try {
      const parsed = new URL(url);
      if (parsed.protocol !== "https:") fail(`${label} must use HTTPS`);
    } catch (error) {
      if (error.message.startsWith("Ecosystem metadata:")) throw error;
      fail(`${label} is not a valid URL`);
    }
  }
  return metadata;
}

async function main() {
  const args = process.argv.slice(2);
  let check = false;
  let root = repositoryRoot;
  for (let index = 0; index < args.length; index += 1) {
    if (args[index] === "--check") check = true;
    else if (args[index] === "--root" && args[index + 1]) root = resolve(args[++index]);
    else fail(`unknown option ${args[index]}`);
  }
  const metadata = await generateMetadata(root);
  const generated = `${JSON.stringify(metadata, null, 2)}\n`;
  const outputPath = resolve(root, "website/ecosystem-metadata.json");
  const indexPath = resolve(root, "website/index.html");
  const robotsPath = resolve(root, "website/robots.txt");
  const sitemapPath = resolve(root, "website/sitemap.xml");
  const issueTemplatePath = resolve(root, ".github/ISSUE_TEMPLATE/config.yml");
  const extensionPackagePath = resolve(root, "ZPlus_VS_code_extension/package.json");
  const extensionReadmePath = resolve(root, "ZPlus_VS_code_extension/README.md");
  const sourceIndex = await readText(root, "website/index.html");
  const sourceRobots = await readText(root, "website/robots.txt");
  const sourceSitemap = await readText(root, "website/sitemap.xml");
  const sourceIssueTemplate = await readText(root, ".github/ISSUE_TEMPLATE/config.yml");
  const sourceExtensionPackage = await readText(root, "ZPlus_VS_code_extension/package.json");
  const sourceExtensionReadme = await readText(root, "ZPlus_VS_code_extension/README.md");
  const expectedExtensionPackage = syncExtensionPackage(sourceExtensionPackage, metadata);
  const expectedExtensionReadme = syncExtensionReadme(sourceExtensionReadme, metadata);
  const expectedIssueTemplate = syncIssueTemplate(sourceIssueTemplate, metadata);
  const expectedIndex = replaceRequired(
    replaceRequired(
      replaceRequired(sourceIndex, /(<meta\s+property="og:url"\s+content=")[^"]*(">)/, `$1${metadata.project.website}$2`, "Open Graph URL"),
      /(<meta\s+property="og:image"\s+content=")[^"]*(">)/, `$1${metadata.project.socialImage}$2`, "Open Graph image"),
    /(<link\s+rel="canonical"\s+href=")[^"]*(">)/, `$1${metadata.project.website}$2`, "canonical link");
  const expectedRobots = replaceRequired(sourceRobots, /^Sitemap: .+$/m,
    `Sitemap: ${new URL("sitemap.xml", metadata.project.website).href}`, "robots sitemap URL");
  let sitemapCount = 0;
  const expectedSitemap = sourceSitemap.replace(/<loc>(https?:\/\/[^<]+)<\/loc>/g, (_match, location) => {
    sitemapCount += 1;
    const path = new URL(location).pathname + new URL(location).search + new URL(location).hash;
    return `<loc>${new URL(path, metadata.project.website).href}</loc>`;
  });
  if (!sitemapCount) fail("website/sitemap.xml has no loc entries to canonicalize");
  if (check) {
    let current;
    try {
      current = await readFile(outputPath, "utf8");
    } catch {
      fail("website/ecosystem-metadata.json is missing; run node scripts/generate-ecosystem-metadata.mjs");
    }
    if (current !== generated) fail("website/ecosystem-metadata.json is stale; run node scripts/generate-ecosystem-metadata.mjs and commit the result");
    for (const [path, expected, label] of [
      [indexPath, expectedIndex, "website/index.html SEO URLs"],
      [robotsPath, expectedRobots, "website/robots.txt sitemap URL"],
      [sitemapPath, expectedSitemap, "website/sitemap.xml canonical URLs"],
      [issueTemplatePath, expectedIssueTemplate, "GitHub issue template documentation URL"],
      [extensionPackagePath, expectedExtensionPackage, "VS Code extension official links"],
      [extensionReadmePath, expectedExtensionReadme, "VS Code extension README official links"]
    ]) {
      const actual = await readFile(path, "utf8");
      if (actual !== expected) fail(`${label} are stale; run node scripts/generate-ecosystem-metadata.mjs and commit the result`);
    }
    process.stdout.write("Ecosystem metadata and generated SEO URLs are current and schema-valid.\n");
    return;
  }
  await writeIfChanged(outputPath, generated);
  await Promise.all([
    writeIfChanged(indexPath, expectedIndex),
    writeIfChanged(robotsPath, expectedRobots),
    writeIfChanged(sitemapPath, expectedSitemap),
    writeIfChanged(issueTemplatePath, expectedIssueTemplate),
    writeIfChanged(extensionPackagePath, expectedExtensionPackage),
    writeIfChanged(extensionReadmePath, expectedExtensionReadme)
  ]);
  process.stdout.write(`Generated ${relative(root, outputPath)} and synchronized website SEO plus GitHub and VS Code project links from CMakeLists.txt and ZPlus_VS_code_extension/package.json.\n`);
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  main().catch(error => {
    process.stderr.write(`${error.message}\n`);
    process.exitCode = 1;
  });
}
