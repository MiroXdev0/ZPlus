import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { createServer } from "node:http";
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { JSDOM } from "jsdom";
import test from "node:test";

const root = new URL("../", import.meta.url);
const read = path => readFile(fileURLToPath(new URL(path, root)), "utf8");
const [html, content, ecosystem, app, sanitizer, releaseRenderer, metadataText] = await Promise.all([
  read("index.html"), read("content.js"), read("ecosystem-content.js"), read("app.js"),
  read("vendor/purify.min.js"), read("release-notes.js"), read("ecosystem-metadata.json")
]);
const metadata = JSON.parse(metadataText);

const release = {
  id: 135,
  tag_name: "v1.3.5",
  name: "Z+ 1.3.5 Windows x86_64",
  draft: false,
  prerelease: false,
  published_at: "2026-11-21T17:05:17Z",
  html_url: "https://github.com/MiroXdev0/ZPluse-release/releases/tag/v1.3.5",
  body: '<h2>Release notes</h2><script>window.compromised = true</script><p onclick="alert(1)">Verified release body.</p>',
  assets: [{
    name: "ZPlus-1.3.5-Setup.exe",
    state: "uploaded",
    size: 7078982,
    browser_download_url: "https://github.com/MiroXdev0/ZPluse-release/releases/download/v1.3.5/ZPlus-1.3.5-Setup.exe"
  }]
};

function createSite({ fetchMetadata = false } = {}) {
  const dom = new JSDOM(html, { runScripts: "outside-only", url: metadata.project.website });
  dom.window.scrollTo = () => {};
  dom.window.requestAnimationFrame = callback => callback();
  if (!fetchMetadata) dom.window.ZPlusMetadata = metadata;
  dom.window.fetch = async input => {
    const url = new URL(input, dom.window.location.origin);
    return {
      ok: true,
      status: 200,
      headers: { get: () => null },
      json: async () => url.pathname === "/ecosystem-metadata.json" ? metadata : [release]
    };
  };
  const copied = [];
  Object.defineProperty(dom.window.navigator, "clipboard", {
    configurable: true,
    value: { writeText: async value => copied.push(value) }
  });
  dom.window.eval(sanitizer);
  dom.window.eval(releaseRenderer);
  dom.window.eval(content);
  dom.window.eval(ecosystem);
  dom.window.eval(app);
  return { dom, copied };
}

function visit(window, path) {
  window.history.pushState(null, "", path);
  window.dispatchEvent(new window.PopStateEvent("popstate"));
}

async function flush() {
  await new Promise(resolve => setTimeout(resolve, 0));
}

test("home retains the dark language-site shell and links into the course", async () => {
  const { dom } = createSite();
  await flush();
  const { document } = dom.window;
  assert.match(document.querySelector("h1").textContent, /Build with Z\+/);
  assert.equal(document.querySelector('meta[name="color-scheme"]').content, "dark");
  assert.ok(document.querySelector('a[href="/learn"]'));
  assert.ok(document.querySelector('a[href="/download"]'));
  assert.equal(dom.window.ZPlusSite.course.flatMap(module => module.lessons).length, 54);
  assert.equal(document.querySelector('link[rel="canonical"]').href, metadata.project.website);
  assert.equal(document.querySelector('meta[property="og:title"]').content, document.title);
  dom.window.close();
});

test("website loads generated metadata and uses it for versions, compatibility and links", async () => {
  const { dom } = createSite({ fetchMetadata: true });
  await flush();
  const { document } = dom.window;
  visit(dom.window, "/compiler");
  assert.match(document.querySelector("main").textContent, new RegExp(metadata.components.compiler.version));
  assert.match(document.querySelector("main").textContent, /zp\.memory\.management/);
  assert.match(document.querySelector("main").textContent, /Windows x86_64/);
  visit(dom.window, "/vscode");
  assert.match(document.querySelector("main").textContent, /Compatibility status: unknown/);
  assert.ok(document.querySelector(`a[href="${metadata.components.vscodeExtension.marketplaceUrl}"]`));
  assert.equal(document.querySelector('meta[property="og:image"]').content, metadata.project.socialImage);
  dom.window.close();
});

test("all product and documentation routes render dedicated content and route metadata", async () => {
  const { dom } = createSite();
  const { document } = dom.window;
  const routes = [
    ["/docs", "Build a working picture"], ["/docs/enums", "Enums"], ["/docs/structs", "Structs"],
    ["/docs/visibility", "Class visibility modifiers"], ["/compiler", "Z+ Compiler"], ["/zpm", "ZPM"],
    ["/zpbuild", "ZPBuild"], ["/vscode", "VS Code extension"], ["/zgraphics", "ZGraphics"],
    ["/ecosystem", "The ecosystem around Z+."], ["/wiki", "Choose a place to begin."]
  ];
  for (const [path, heading] of routes) {
    visit(dom.window, path);
    assert.ok(document.querySelector("main h1")?.textContent.includes(heading), `${path} should render ${heading}`);
    assert.equal(document.querySelector('link[rel="canonical"]').href, new URL(path, metadata.project.website).href);
    assert.equal(document.querySelector('meta[property="og:title"]').content, document.title);
  }
  visit(dom.window, "/docs/visibility");
  assert.match(document.querySelector("main").textContent, /subclass access is not currently available/i);
  await flush();
  dom.window.close();
});

test("all lessons, reference, product, and public tool routes resolve", async () => {
  const { dom } = createSite();
  const { document } = dom.window;
  const routes = new Set([
    "/", "/download", "/releases", "/ecosystem", "/compiler", "/zpm", "/zpbuild", "/vscode", "/zgraphics",
    "/learn", "/docs", "/wiki", "/zts", "/zjs", "/examples", "/packages", "/login", "/register"
  ]);
  for (const article of dom.window.ZPlusSite.docs) routes.add(`/docs/${article.id}`);
  for (const article of dom.window.ZPlusSite.wiki) routes.add(`/wiki/${article.id}`);
  let number = 0;
  for (const module of dom.window.ZPlusSite.course) {
    for (const lesson of module.lessons) {
      number += 1;
      const slug = lesson.title.toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, "");
      routes.add(`/learn/${String(number).padStart(2, "0")}-${slug}`);
    }
  }

  for (const route of routes) {
    visit(dom.window, route);
    const heading = document.querySelector("main h1")?.textContent.trim();
    assert.ok(heading, `${route} should render a page heading`);
    assert.notEqual(heading, "This page isn't in the guide.", `${route} should not render the not-found page`);
  }
  await flush();
  dom.window.close();
});

test("Wiki exposes only the three requested destinations and removes old articles", async () => {
  const { dom } = createSite();
  const { document, ZPlusSite } = dom.window;
  visit(dom.window, "/wiki");
  const destinations = [...document.querySelectorAll('.wiki-destinations a[href^="/wiki/"]')];
  assert.deepEqual(destinations.map(anchor => new URL(anchor.href).pathname), [
    "/wiki/syntax", "/wiki/idea", "/wiki/uses"
  ]);
  assert.deepEqual(Array.from(ZPlusSite.wiki, item => item.id), ["syntax", "idea", "uses"]);
  for (const oldId of ["architecture", "project-layout", "modules", "contributing", "troubleshooting", "faq", "packages"]) {
    visit(dom.window, `/wiki/${oldId}`);
    assert.equal(document.querySelector("main h1").textContent, "This page isn't in the guide.");
  }
  dom.window.close();
});

test("direct Wiki deep links reach the existing SPA entry point", async () => {
  const probe = createServer();
  await new Promise(resolve => probe.listen(0, "127.0.0.1", resolve));
  const port = probe.address().port;
  await new Promise(resolve => probe.close(resolve));
  const serverPath = fileURLToPath(new URL("../server.mjs", import.meta.url));
  const child = spawn(process.execPath, [serverPath], {
    env: { ...process.env, PORT: String(port) },
    stdio: ["ignore", "pipe", "pipe"]
  });
  let serverOutput = "";
  const ready = new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error(`Website server did not start: ${serverOutput}`)), 3000);
    child.stdout.on("data", chunk => {
      serverOutput += chunk.toString();
      if (serverOutput.includes(`http://localhost:${port}`)) {
        clearTimeout(timer);
        resolve();
      }
    });
    child.once("error", error => {
      clearTimeout(timer);
      reject(error);
    });
    child.once("exit", code => {
      clearTimeout(timer);
      reject(new Error(`Website server exited with ${code}: ${serverOutput}`));
    });
  });
  try {
    await ready;
    for (const route of ["/wiki/syntax", "/wiki/idea", "/wiki/uses", "/wiki/architecture"]) {
      const response = await fetch(`http://127.0.0.1:${port}${route}`);
      assert.equal(response.status, 200, `${route} should reach the SPA entry point`);
      assert.match(await response.text(), /<main id="main"/);
    }
  } finally {
    if (child.exitCode === null) {
      child.kill();
      await new Promise(resolve => child.once("exit", resolve));
    }
  }
});

test("registry API paths never fall through to the static SPA", async () => {
  const probe = createServer();
  await new Promise(resolve => probe.listen(0, "127.0.0.1", resolve));
  const port = probe.address().port;
  await new Promise(resolve => probe.close(resolve));
  const serverPath = fileURLToPath(new URL("../server.mjs", import.meta.url));
  const child = spawn(process.execPath, [serverPath], {
    env: { ...process.env, PORT: String(port) },
    stdio: ["ignore", "pipe", "pipe"]
  });
  let serverOutput = "";
  const ready = new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error(`Website server did not start: ${serverOutput}`)), 3000);
    child.stdout.on("data", chunk => {
      serverOutput += chunk.toString();
      if (serverOutput.includes(`http://localhost:${port}`)) {
        clearTimeout(timer);
        resolve();
      }
    });
    child.once("error", error => {
      clearTimeout(timer);
      reject(error);
    });
    child.once("exit", code => {
      clearTimeout(timer);
      reject(new Error(`Website server exited with ${code}: ${serverOutput}`));
    });
  });
  try {
    await ready;
    for (const path of ["/api/registry", "/api/registry/auth/me", "/api/registry/packages?q=example"]) {
      const response = await fetch(`http://127.0.0.1:${port}${path}`);
      assert.equal(response.status, 503, `${path} should report that the local API is unavailable`);
      assert.match(response.headers.get("content-type"), /^application\/json/);
      assert.equal(response.headers.get("cache-control"), "no-store");
      const body = await response.json();
      assert.equal(body.error.code, "registry_api_unavailable");
    }
  } finally {
    if (child.exitCode === null) {
      child.kill();
      await new Promise(resolve => child.once("exit", resolve));
    }
  }
});

test("Vercel routes registry traffic before the SPA fallback", async () => {
  const config = JSON.parse(await read("vercel.json"));
  const rewrites = config.rewrites;
  const registryIndex = rewrites.findIndex(route => route.source === "/api/registry/:path*");
  const spaIndex = rewrites.findIndex(route => route.source === "/:path*");
  assert.ok(registryIndex >= 0, "registry wildcard rewrite must exist");
  assert.ok(spaIndex > registryIndex, "SPA fallback must follow the registry rewrite");
  assert.equal(rewrites[registryIndex].destination, "/api/registry?route=:path*");
  assert.ok(await read("api/registry.js"), "the Vercel function entry point must exist");
});

test("the syntax destination is sectioned, searchable, code-aware, and cross-linked", async () => {
  const { dom } = createSite();
  const { document, ZPlusSite } = dom.window;
  visit(dom.window, "/wiki/syntax");
  const page = document.querySelector("main");
  for (const heading of ["Variables and types", "Operators and expressions", "Functions", "Classes and visibility", "Enums", "Structs", "Modules and imports", "Compiler and execution behavior"]) {
    assert.ok([...page.querySelectorAll("h2")].some(node => node.textContent === heading), `${heading} section should exist`);
  }
  assert.ok(page.querySelector(".table-of-contents a[href=\"#enums\"]"));
  assert.ok(page.querySelectorAll(".code-frame").length >= 8);
  assert.ok(page.querySelector('a[href="/wiki/idea"]'));
  assert.ok(ZPlusSite.wiki.find(item => item.id === "syntax").body.length >= 15);
  visit(dom.window, "/wiki");
  assert.equal(document.querySelectorAll('.wiki-destinations a').length, 3);
  await flush();
  dom.window.close();
});

test("lessons retain their first 52 URLs and new advanced lessons link to syntax", async () => {
  const { dom } = createSite();
  const { document, ZPlusSite } = dom.window;
  let number = 0;
  const ids = [];
  for (const module of ZPlusSite.course) {
    for (const lesson of module.lessons) {
      number += 1;
      const slug = lesson.title.toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, "");
      ids.push(`${String(number).padStart(2, "0")}-${slug}`);
    }
  }
  assert.equal(ids[51], "52-creating-and-sharing-packages");
  assert.equal(ids[52], "53-class-visibility");
  assert.equal(ids[53], "54-enums-and-typed-states");
  for (const id of ids.slice(52)) {
    visit(dom.window, `/learn/${id}`);
    assert.ok(document.querySelector('main a[href="/wiki/syntax"]'));
  }
  const moduleLesson = ZPlusSite.course.flatMap(module => module.lessons).find(lesson => lesson.title === "Creating reusable modules");
  const moduleId = ids[ZPlusSite.course.flatMap(module => module.lessons).indexOf(moduleLesson)];
  visit(dom.window, `/learn/${moduleId}`);
  assert.ok([...document.querySelectorAll(".code-toolbar span")].some(node => node.textContent === "greetings.zh"));
  dom.window.close();
});

test("documentation filter narrows the sidebar and index without hiding every result", async () => {
  const { dom } = createSite();
  visit(dom.window, "/docs");
  const filter = dom.window.document.querySelector("#docs-search");
  filter.value = "enum";
  filter.dispatchEvent(new dom.window.Event("input", { bubbles: true }));
  const visibleLinks = [...dom.window.document.querySelectorAll("[data-doc-link]")].filter(node => !node.hidden);
  assert.equal(visibleLinks.length, 1);
  assert.equal(visibleLinks[0].textContent, "Enums");
  assert.equal(dom.window.document.querySelector("#docs-search-empty").hidden, true);
  filter.value = "nothing matches this";
  filter.dispatchEvent(new dom.window.Event("input", { bubbles: true }));
  assert.equal(dom.window.document.querySelector("#docs-search-empty").hidden, false);
  await flush();
  dom.window.close();
});

test("code copy works and announces completion", async () => {
  const { dom, copied } = createSite();
  visit(dom.window, "/docs/first-program");
  const button = dom.window.document.querySelector(".copy-code");
  button.dispatchEvent(new dom.window.MouseEvent("click", { bubbles: true }));
  await flush();
  assert.match(copied[0], /Hello, Z\+!/);
  assert.equal(button.textContent, "Copied");
  assert.equal(button.getAttribute("aria-label"), "Code copied to clipboard");
  dom.window.close();
});

test("download page uses published asset links and sanitizes release-note HTML", async () => {
  const { dom } = createSite();
  visit(dom.window, "/download");
  await flush();
  const document = dom.window.document;
  assert.match(document.querySelector("#download-compiler-version").textContent, /v1\.3\.5/);
  const installer = document.querySelector('a[href="https://github.com/MiroXdev0/ZPluse-release/releases/download/v1.3.5/ZPlus-1.3.5-Setup.exe"]');
  assert.ok(installer);
  assert.equal(document.querySelector(".release-notes-content script"), null);
  assert.equal(document.querySelector(".release-notes-content p")?.hasAttribute("onclick"), false);
  assert.equal(dom.window.compromised, undefined);
  assert.match(document.querySelector(".download-product-grid").textContent, new RegExp(metadata.components.zpm.version));
  assert.match(document.querySelector(".download-product-grid").textContent, new RegExp(metadata.components.zpbuild.version));
  assert.match(document.querySelector(".download-product-grid").textContent, new RegExp(metadata.components.vscodeExtension.version));
  assert.match(document.querySelector(".download-product-grid").textContent, /Marketplace/);
  assert.ok(document.querySelector(`.download-product-grid a[href="${metadata.components.vscodeExtension.marketplaceUrl}"]`));
  dom.window.close();
});

test("mobile navigation exposes state and the SEO files enumerate public routes", async () => {
  const { dom } = createSite();
  const toggle = dom.window.document.querySelector(".menu-toggle");
  toggle.click();
  assert.equal(toggle.getAttribute("aria-expanded"), "true");
  assert.equal(toggle.getAttribute("aria-label"), "Close navigation");
  toggle.click();
  assert.equal(toggle.getAttribute("aria-expanded"), "false");
  const sitemap = await read("sitemap.xml");
  const robots = await read("robots.txt");
  assert.ok(sitemap.includes(`${metadata.project.website}docs/enums`));
  assert.ok(sitemap.includes(`${metadata.project.website}wiki/syntax`));
  assert.ok(sitemap.includes(`${metadata.project.website}wiki/idea`));
  assert.ok(sitemap.includes(`${metadata.project.website}wiki/uses`));
  assert.ok(sitemap.includes(`${metadata.project.website}zgraphics`));
  assert.ok(robots.includes(`Sitemap: ${new URL("sitemap.xml", metadata.project.website).href}`));
  await flush();
  dom.window.close();
});
