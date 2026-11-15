import { Readable } from "node:stream";
import { pipeline } from "node:stream/promises";

function toWebHeaders(incomingHeaders) {
  const headers = new Headers();
  for (const [name, value] of Object.entries(incomingHeaders)) {
    if (value !== undefined) {
      headers.set(name, Array.isArray(value) ? value.join(", ") : value);
    }
  }
  return headers;
}

export function createVercelNodeHandler(webHandler, baseOrigin) {
  return async (request, response) => {
    const method = request.method || "GET";
    const webRequest = new Request(new URL(request.url || "/", baseOrigin), {
      method,
      headers: toWebHeaders(request.headers),
      ...(method !== "GET" && method !== "HEAD"
        ? { body: Readable.toWeb(request), duplex: "half" }
        : {})
    });
    const webResponse = await webHandler(webRequest);

    response.statusCode = webResponse.status;
    webResponse.headers.forEach((value, name) => response.setHeader(name, value));
    if (method === "HEAD") {
      await webResponse.body?.cancel();
      response.end();
    } else if (webResponse.body) {
      await pipeline(Readable.fromWeb(webResponse.body), response);
    } else {
      response.end();
    }
  };
}
