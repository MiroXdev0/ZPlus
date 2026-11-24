import { createServer } from "node:http";
import { readFile } from "node:fs/promises";
import { extname, relative, resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";

const root = fileURLToPath(new URL(".", import.meta.url));
const contentTypes = {
  ".css": "text/css; charset=utf-8",
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".json": "application/json; charset=utf-8",
  ".mjs": "text/javascript; charset=utf-8",
  ".txt": "text/plain; charset=utf-8",
  ".xml": "application/xml; charset=utf-8",
  ".png": "image/png",
  ".svg": "image/svg+xml"
};
const port = Number(process.env.PORT || 8000);

createServer(async (request, response) => {
  if (request.method !== "GET" && request.method !== "HEAD") {
    response.writeHead(405, { Allow: "GET, HEAD" }).end();
    return;
  }
  let pathname;
  try {
    pathname = decodeURIComponent(new URL(request.url, "http://localhost").pathname);
  } catch {
    response.writeHead(400).end("Invalid request path");
    return;
  }
  if (pathname === "/api/registry" || pathname.startsWith("/api/registry/")) {
    response.writeHead(503, {
      "Content-Type": "application/json; charset=utf-8",
      "Cache-Control": "no-store",
      "X-Content-Type-Options": "nosniff"
    }).end(JSON.stringify({
      error: {
        code: "registry_api_unavailable",
        message: "The static development server does not run the registry API. Use the Vercel development server for registry routes."
      }
    }));
    return;
  }
  let target = resolve(root, pathname === "/favicon.ico" ? "./assets/favicon-dark.png" : `.${pathname}`);
  const relativeTarget = relative(root, target);
  if (relativeTarget === ".." || relativeTarget.startsWith(`..${sep}`) || relativeTarget.includes(`..${sep}`)) {
    response.writeHead(403).end("Forbidden");
    return;
  }
  if (pathname.endsWith("/")) target = resolve(target, "index.html");
  try {
    const body = await readFile(target);
    response.writeHead(200, {
      "Content-Type": contentTypes[extname(target).toLowerCase()] || "application/octet-stream",
      "X-Content-Type-Options": "nosniff"
    });
    response.end(request.method === "HEAD" ? undefined : body);
  } catch (error) {
    if (error.code !== "ENOENT" && error.code !== "EISDIR") {
      response.writeHead(500).end("Unable to read website file");
      return;
    }
    if (extname(pathname)) {
      response.writeHead(404).end("Not found");
      return;
    }
    try {
      const body = await readFile(resolve(root, "index.html"));
      response.writeHead(200, {
        "Content-Type": "text/html; charset=utf-8",
        "X-Content-Type-Options": "nosniff"
      });
      response.end(request.method === "HEAD" ? undefined : body);
    } catch (fallbackError) {
      response.writeHead(500).end("Unable to load website entry point");
    }
  }
}).listen(port, () => {
  console.log(`Z+ website available at http://localhost:${port}`);
});
