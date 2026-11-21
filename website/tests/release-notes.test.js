import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { JSDOM } from "jsdom";
import test from "node:test";

const root = new URL("../", import.meta.url);
const fixture = await readFile(new URL("./fixtures/release-1.3.5.html", import.meta.url), "utf8");
const sanitizerScript = await readFile(new URL("../vendor/purify.min.js", import.meta.url), "utf8");
const rendererScript = await readFile(new URL("../release-notes.js", import.meta.url), "utf8");

function createRenderer() {
  const dom = new JSDOM("<!doctype html><html><body></body></html>", { runScripts: "outside-only" });
  dom.window.eval(sanitizerScript);
  dom.window.eval(rendererScript);
  return { dom, render: dom.window.renderReleaseNotes };
}

test("Z+ 1.3.5 release-note HTML retains its authored document structure", async () => {
  const { dom, render } = createRenderer();
  const container = dom.window.document.createElement("div");
  container.innerHTML = render(fixture);

  assert.equal(container.querySelector("h1")?.textContent, "Z+ Compiler 1.3.5");
  assert.match(container.querySelector("p").textContent, /final stabilization release/);
  assert.ok(container.querySelector("ul ul li code"));
  assert.equal(container.querySelectorAll("table tbody tr").length, 3);
  assert.equal(container.querySelector('a[href="https://github.com/MiroXdev0/ZPluse-release/releases/tag/v1.3.5"]')?.textContent, "official 1.3.5 release");
  assert.equal(container.querySelector("img")?.getAttribute("src"), "https://zpluse.vercel.app/assets/zplus-logo-light.png");
  assert.equal(container.querySelector("img")?.getAttribute("alt"), "Z+ logo");
  assert.equal(container.querySelector("pre code")?.textContent, "zp --version\nzp check hello.zp");
  assert.ok(container.querySelector("blockquote p"));
  assert.ok(container.querySelector("hr"));
  dom.window.close();
});

test("release-note sanitizer removes executable markup and unsafe URL schemes", () => {
  const { dom, render } = createRenderer();
  const container = dom.window.document.createElement("div");
  container.innerHTML = render(`
    <h2 onclick="alert(1)">Safe heading</h2>
    <script>window.compromised = true</script>
    <img src="x" onerror="alert(1)" style="position:fixed">
    <a href="javascript:alert(1)" onmouseover="alert(1)">unsafe link</a>
    <iframe src="https://attacker.example"></iframe>
  `);

  assert.equal(container.querySelector("h2")?.textContent, "Safe heading");
  assert.equal(container.querySelector("h2")?.hasAttribute("onclick"), false);
  assert.equal(container.querySelector("script"), null);
  assert.equal(container.querySelector("iframe"), null);
  assert.equal(container.querySelector("img")?.hasAttribute("onerror"), false);
  assert.equal(container.querySelector("img")?.hasAttribute("style"), false);
  assert.equal(container.querySelector("a")?.getAttribute("href"), null);
  assert.equal(container.querySelector("a")?.hasAttribute("onmouseover"), false);
  assert.equal(dom.window.compromised, undefined);
  dom.window.close();
});

test("site loads the local sanitizer before the release renderer and renders sanitized HTML", async () => {
  const html = await readFile(fileURLToPath(new URL("index.html", root)), "utf8");
  const app = await readFile(fileURLToPath(new URL("app.js", root)), "utf8");
  assert.ok(html.indexOf("/vendor/purify.min.js") < html.indexOf("/release-notes.js"));
  assert.ok(html.indexOf("/release-notes.js") < html.indexOf("/app.js"));
  assert.match(app, /window\.renderReleaseNotes\(release\.body\)/);
  assert.doesNotMatch(app, /<pre>\$\{escapeHtml\(release\.body\)\}<\/pre>/);
});
