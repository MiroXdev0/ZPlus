const HTTP_METHODS = new Set(["get", "post", "put", "patch", "delete", "head", "options"]);
const IDENTIFIER = /^[A-Za-z_$][A-Za-z0-9_$]*$/;
const RESERVED_IDENTIFIERS = new Set([
  "await", "break", "case", "catch", "class", "const", "continue", "debugger",
  "default", "delete", "do", "else", "enum", "export", "extends", "false",
  "finally", "for", "function", "if", "implements", "import", "in", "instanceof",
  "interface", "let", "new", "null", "package", "private", "protected", "public",
  "return", "static", "super", "switch", "this", "throw", "true", "try", "typeof",
  "var", "void", "while", "with", "yield", "constructor", "prototype", "__proto__"
]);

function fail(message) {
  throw new Error(`OpenAPI: ${message}`);
}

function isObject(value) {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

function object(value, label) {
  if (!isObject(value)) fail(`${label} must be an object`);
  return value;
}

function quote(value) {
  return JSON.stringify(value);
}

function pascalCase(value) {
  const result = value.replace(/(^|[^A-Za-z0-9_$]+)([A-Za-z0-9_$])/g, (_, _separator, character) =>
    character.toUpperCase());
  return /^[A-Za-z_$]/.test(result) ? result : `Operation${result}`;
}

function schemaReference(reference) {
  const match = /^#\/components\/schemas\/(.+)$/.exec(reference);
  if (!match) fail(`only local #/components/schemas references are supported: ${reference}`);
  const name = match[1].replace(/~1/g, "/").replace(/~0/g, "~");
  if (!IDENTIFIER.test(name) || RESERVED_IDENTIFIERS.has(name)) {
    fail(`schema name '${name}' must be a non-reserved TypeScript identifier`);
  }
  return name;
}

function schemaType(schemaValue, context) {
  const schema = object(schemaValue, context);

  if (schema.$ref !== undefined) {
    if (typeof schema.$ref !== "string") fail(`${context} has an invalid $ref`);
    return schemaReference(schema.$ref);
  }

  const unsupported = ["not", "patternProperties", "propertyNames", "prefixItems", "contains", "if", "then", "else"];
  const unsupportedKeyword = unsupported.find(keyword => schema[keyword] !== undefined);
  if (unsupportedKeyword) fail(`${context} uses unsupported schema keyword '${unsupportedKeyword}'`);

  const constraints = [];
  if (schema.oneOf !== undefined || schema.anyOf !== undefined) {
    const composition = schema.oneOf ?? schema.anyOf;
    if (!Array.isArray(composition) || composition.length === 0) {
      fail(`${context} must have a non-empty oneOf or anyOf array`);
    }
    constraints.push(`(${composition.map((entry, index) =>
      schemaType(entry, `${context} alternative ${index + 1}`)).join(" | ")})`);
  }
  if (schema.allOf !== undefined) {
    if (!Array.isArray(schema.allOf) || schema.allOf.length === 0) {
      fail(`${context}.allOf must be a non-empty array`);
    }
    constraints.push(...schema.allOf.map((entry, index) =>
      `(${schemaType(entry, `${context} allOf item ${index + 1}`)})`));
  }

  let enumType;
  if (schema.enum !== undefined) {
    if (!Array.isArray(schema.enum) || schema.enum.length === 0 ||
        schema.enum.some(value => value !== null && !["string", "number", "boolean"].includes(typeof value))) {
      fail(`${context}.enum must contain strings, numbers, booleans, or null`);
    }
    enumType = schema.enum.map(quote).join(" | ");
  }

  let types = schema.type;
  if (Array.isArray(types)) {
    if (types.length === 0 || types.some(type => typeof type !== "string")) {
      fail(`${context}.type must be a string or a non-empty array of strings`);
    }
  } else if (types === undefined) {
    types = schema.properties !== undefined || schema.additionalProperties !== undefined
      ? "object"
      : "unknown";
  } else if (typeof types !== "string") {
    fail(`${context}.type must be a string`);
  }

  const mapped = (type) => {
    switch (type) {
      case "null": return "null";
      case "string": return "string";
      case "number":
      case "integer": return "number";
      case "boolean": return "boolean";
      case "array":
        if (schema.items === undefined) fail(`${context}.items is required for array schemas`);
        return `Array<${schemaType(schema.items, `${context}.items`)}>`;
      case "object": {
        const properties = schema.properties === undefined ? {} : object(schema.properties, `${context}.properties`);
        const required = schema.required === undefined ? [] : schema.required;
        if (!Array.isArray(required) || required.some(name => typeof name !== "string")) {
          fail(`${context}.required must be an array of property names`);
        }
        const missing = required.find(name => !Object.hasOwn(properties, name));
        if (missing !== undefined) fail(`${context}.required references missing property '${missing}'`);
        const members = Object.entries(properties).map(([name, value]) => {
          const memberType = schemaType(value, `${context}.${name}`);
          return `  ${quote(name)}${required.includes(name) ? "" : "?"}: ${memberType};`;
        });
        if (schema.additionalProperties !== false) {
          const valueType = schema.additionalProperties === undefined || schema.additionalProperties === true
            ? "unknown"
            : schemaType(schema.additionalProperties, `${context}.additionalProperties`);
          members.unshift(`  [key: string]: ${valueType};`);
        }
        return members.length === 0 ? "Record<string, unknown>" : `{\n${members.join("\n")}\n}`;
      }
      case "unknown": return "unknown";
      default: fail(`${context} uses unsupported type '${type}'`);
    }
  };
  const isConstrained = schema.oneOf !== undefined || schema.anyOf !== undefined || schema.allOf !== undefined;
  if (schema.type !== undefined || schema.properties !== undefined || schema.additionalProperties !== undefined) {
    constraints.unshift((Array.isArray(types) ? types : [types]).map(mapped).join(" | "));
  }
  if (enumType !== undefined) constraints.push(`(${enumType})`);
  if (constraints.length === 0 && !isConstrained) constraints.push("unknown");
  let result = constraints.length === 1 ? constraints[0] : constraints.map(type => `(${type})`).join(" & ");
  if (schema.nullable === true && !(Array.isArray(types) && types.includes("null")) && types !== "null") {
    result = `${result} | null`;
  }
  return result;
}

function jsonMediaSchema(content, context) {
  const entries = Object.entries(object(content, context));
  if (entries.length === 0) return null;
  const jsonEntries = entries.filter(([mediaType]) =>
    mediaType.toLowerCase() === "application/json" ||
    /^application\/[a-z0-9.+-]+\+json$/i.test(mediaType));
  if (jsonEntries.length !== entries.length) {
    fail(`${context} only supports application/json media types`);
  }
  const schemas = jsonEntries.map(([mediaType, mediaValue]) => {
    const media = object(mediaValue, `${context}.${mediaType}`);
    return media.schema === undefined ? "unknown" : schemaType(media.schema, `${context}.${mediaType}.schema`);
  });
  return [...new Set(schemas)].join(" | ");
}

function normalizeParameters(pathItem, operation, path) {
  const combined = new Map();
  for (const [source, list] of [
    ["path", pathItem.parameters ?? []],
    ["operation", operation.parameters ?? []]
  ]) {
    if (!Array.isArray(list)) fail(`${path} ${source} parameters must be an array`);
    for (const [index, raw] of list.entries()) {
      const parameter = object(raw, `${path} ${source} parameter ${index + 1}`);
      if (parameter.$ref !== undefined) fail(`${path} parameter references are not supported`);
      const { name, in: location } = parameter;
      if (typeof name !== "string" || name.length === 0) fail(`${path} has a parameter without a name`);
      if (!["path", "query", "header"].includes(location)) {
        fail(`${path} parameter '${name}' uses unsupported location '${location}'`);
      }
      if (parameter.schema === undefined) fail(`${path} parameter '${name}' requires a schema`);
      const schema = object(parameter.schema, `${path} parameter '${name}' schema`);
      if (schema.nullable === true) fail(`${path} parameter '${name}' cannot be nullable`);
      if (location === "path" && (schema.type === "array" || schema.type === "object")) {
        fail(`${path} path parameter '${name}' must be scalar`);
      }
      if (location === "header" && (schema.type === "array" || schema.type === "object")) {
        fail(`${path} header parameter '${name}' must be scalar`);
      }
      if (schema.type === "array" &&
          (!isObject(schema.items) || !["string", "number", "integer", "boolean"].includes(schema.items.type))) {
        fail(`${path} parameter '${name}' arrays must contain primitive values`);
      }
      if (schema.type !== "array" && !["string", "number", "integer", "boolean"].includes(schema.type)) {
        fail(`${path} parameter '${name}' must use a primitive or primitive-array schema`);
      }
      const style = parameter.style ?? (location === "query" ? "form" : "simple");
      if ((location === "query" && style !== "form") ||
          (location !== "query" && style !== "simple")) {
        fail(`${path} parameter '${name}' uses unsupported serialization style '${style}'`);
      }
      const type = schemaType(schema, `${path} parameter '${name}' schema`);
      if (schema.type === "array" && location !== "query") {
        fail(`${path} arrays are supported only for query parameters`);
      }
      const normalizedName = location === "header" ? name.toLowerCase() : name;
      const key = `${location}:${normalizedName}`;
      const previous = combined.get(key);
      if (previous && previous.source === source) {
        fail(`${path} declares parameter '${name}' more than once`);
      }
      combined.set(key, {
        name,
        location,
        required: location === "path" || parameter.required === true,
        type: location === "header" ? "string" : type,
        schemaType: schema.type,
        explode: parameter.explode !== false,
        source
      });
    }
  }

  const parameters = [...combined.values()];
  const pathNames = [...path.matchAll(/\{([^{}]+)\}/g)].map(match => match[1]);
  if (path.includes("{") && pathNames.length === 0) fail(`${path} contains an invalid path template`);
  if (new Set(pathNames).size !== pathNames.length) fail(`${path} repeats a path parameter`);
  const declaredPathNames = parameters.filter(parameter => parameter.location === "path").map(parameter => parameter.name);
  const missing = pathNames.find(name => !declaredPathNames.includes(name));
  const extra = declaredPathNames.find(name => !pathNames.includes(name));
  if (missing) fail(`${path} is missing the required path parameter '${missing}'`);
  if (extra) fail(`${path} declares path parameter '${extra}' but has no matching placeholder`);
  return parameters;
}

function operationName(method, path, operation) {
  const value = operation.operationId ??
    `${method}_${path.replace(/\{([^{}]+)\}/g, "$1").replace(/[^A-Za-z0-9_$]+/g, "_")}`;
  if (typeof value !== "string" || !IDENTIFIER.test(value) || RESERVED_IDENTIFIERS.has(value)) {
    fail(`${method.toUpperCase()} ${path} needs a non-reserved JavaScript operationId`);
  }
  return value;
}

function parseOperations(document) {
  if (typeof document.openapi !== "string" || !/^3\.(0|1)\.\d+(?:[-+][A-Za-z0-9.-]+)?$/.test(document.openapi)) {
    fail("a valid OpenAPI 3.0 or 3.1 version is required");
  }
  const components = document.components === undefined ? {} : object(document.components, "components");
  const schemas = components.schemas === undefined ? {} : object(components.schemas, "components.schemas");
  for (const name of Object.keys(schemas)) {
    if (!IDENTIFIER.test(name) || RESERVED_IDENTIFIERS.has(name)) {
      fail(`schema name '${name}' must be a non-reserved TypeScript identifier`);
    }
  }
  const paths = object(document.paths, "paths");
  const operations = [];
  const operationIds = new Set();
  const inputNames = new Set();

  for (const [path, rawPathItem] of Object.entries(paths)) {
    if (!path.startsWith("/") || path.includes("?") || path.includes("#")) {
      fail(`path '${path}' must begin with '/' and cannot include a query or fragment`);
    }
    const pathItem = object(rawPathItem, `path '${path}'`);
    if (pathItem.$ref !== undefined) fail(`path item references are not supported for '${path}'`);
    for (const [method, rawOperation] of Object.entries(pathItem)) {
      if (method === "parameters" || method === "summary" || method === "description" ||
          method === "servers" || method === "x-zplus") continue;
      if (!HTTP_METHODS.has(method.toLowerCase())) continue;
      const operation = object(rawOperation, `${method.toUpperCase()} ${path}`);
      const id = operationName(method.toLowerCase(), path, operation);
      if (operationIds.has(id)) fail(`operationId '${id}' is duplicated`);
      operationIds.add(id);
      const inputName = `${pascalCase(id)}Input`;
      if (inputNames.has(inputName)) fail(`generated input type '${inputName}' is duplicated`);
      inputNames.add(inputName);

      const parameters = normalizeParameters(pathItem, operation, path);
      const responses = object(operation.responses, `${method.toUpperCase()} ${path} responses`);
      const successResponses = [];
      for (const [status, responseValue] of Object.entries(responses)) {
        if (!/^2\d\d$/.test(status) && status !== "2XX" && status !== "default") continue;
        const response = object(responseValue, `${method.toUpperCase()} ${path} response '${status}'`);
        if (response.$ref !== undefined) fail(`${method.toUpperCase()} ${path} response references are not supported`);
        if (response.content === undefined) {
          successResponses.push({ status, type: "void", json: false });
        } else {
          const type = jsonMediaSchema(response.content, `${method.toUpperCase()} ${path} response '${status}' content`);
          if (type === null) successResponses.push({ status, type: "void", json: false });
          else successResponses.push({ status, type, json: true });
        }
      }
      if (successResponses.length === 0) fail(`${method.toUpperCase()} ${path} needs a 2xx or default response`);

      let requestBody;
      if (operation.requestBody !== undefined) {
        const body = object(operation.requestBody, `${method.toUpperCase()} ${path} requestBody`);
        if (body.$ref !== undefined) fail(`${method.toUpperCase()} ${path} requestBody references are not supported`);
        if (body.content === undefined) fail(`${method.toUpperCase()} ${path} requestBody requires content`);
        const type = jsonMediaSchema(body.content, `${method.toUpperCase()} ${path} requestBody content`);
        if (type === null) fail(`${method.toUpperCase()} ${path} requestBody content cannot be empty`);
        requestBody = { type, required: body.required === true };
      }

      operations.push({
        id,
        inputName,
        method: method.toUpperCase(),
        path,
        parameters,
        requestBody,
        responses: successResponses,
        returnType: [...new Set(successResponses.map(response => response.type))].join(" | ")
      });
    }
  }
  if (operations.length === 0) fail("paths must contain at least one supported HTTP operation");

  const references = [...JSON.stringify({ schemas, operations }).matchAll(/"#\/components\/schemas\/([^"]+)"/g)];
  for (const match of references) {
    const name = match[1].replace(/~1/g, "/").replace(/~0/g, "~");
    if (!Object.hasOwn(schemas, name)) fail(`reference '#/components/schemas/${match[1]}' does not exist`);
  }
  return { schemas, operations };
}

function generateTypes(schemas, operations) {
  const lines = [
    "/* Generated by @zplus/web-interop. Do not edit by hand. */",
    "export interface ApiClientOptions {",
    "  baseUrl: string;",
    "  headers?: HeadersInit | (() => HeadersInit);",
    "  credentials?: RequestCredentials;",
    "  fetch?: typeof globalThis.fetch;",
    "}",
    "",
    "export class ApiError extends Error {",
    "  readonly status: number;",
    "  readonly body: unknown;",
    "  readonly response: Response;",
    "  constructor(response: Response, body: unknown);",
    "}",
    ""
  ];

  for (const [name, schema] of Object.entries(schemas)) {
    lines.push(`export type ${name} = ${schemaType(schema, `components.schemas.${name}`)};`, "");
  }

  for (const operation of operations) {
    const fields = ["  signal?: AbortSignal;"];
    const pathParameters = operation.parameters.filter(parameter => parameter.location === "path");
    const queryParameters = operation.parameters.filter(parameter => parameter.location === "query");
    const headerParameters = operation.parameters.filter(parameter => parameter.location === "header");
    if (pathParameters.length) {
      fields.push(`  path${pathParameters.some(parameter => parameter.required) ? "" : "?"}: {`);
      for (const parameter of pathParameters) {
        fields.push(`    ${quote(parameter.name)}${parameter.required ? "" : "?"}: ${parameter.type};`);
      }
      fields.push("  };");
    }
    if (queryParameters.length) {
      fields.push(`  query${queryParameters.some(parameter => parameter.required) ? "" : "?"}: {`);
      for (const parameter of queryParameters) {
        fields.push(`    ${quote(parameter.name)}${parameter.required ? "" : "?"}: ${parameter.type};`);
      }
      fields.push("  };");
    }
    if (headerParameters.length) {
      fields.push(`  headers${headerParameters.some(parameter => parameter.required) ? "" : "?"}: Record<string, string> & {`);
      for (const parameter of headerParameters) {
        fields.push(`    ${quote(parameter.name)}${parameter.required ? "" : "?"}: ${parameter.type};`);
      }
      fields.push("  };");
    } else {
      fields.push("  headers?: Record<string, string>;");
    }
    if (operation.requestBody) {
      fields.push(`  body${operation.requestBody.required ? "" : "?"}: ${operation.requestBody.type};`);
    }
    lines.push(`export interface ${operation.inputName} {`, ...fields, "}", "");
  }

  lines.push("export interface ApiClient {");
  for (const operation of operations) {
    const inputRequired = operation.parameters.some(parameter => parameter.required) ||
      operation.requestBody?.required === true;
    lines.push(`  ${operation.id}(input${inputRequired ? "" : "?"}: ${operation.inputName}): Promise<${operation.returnType}>;`);
  }
  lines.push("}", "", "export function createClient(options: ApiClientOptions): ApiClient;", "");
  return lines.join("\n");
}

function generateClient(operations) {
  const serializedOperations = JSON.stringify(operations, null, 2);
  return `/* Generated by @zplus/web-interop. Do not edit by hand. */
const operations = Object.freeze(${serializedOperations});

export class ApiError extends Error {
  constructor(response, body) {
    super(\`HTTP \${response.status} \${response.statusText}\`.trim());
    this.name = "ApiError";
    this.status = response.status;
    this.body = body;
    this.response = response;
  }
}

function isJsonContentType(value) {
  return /^application\\/(?:[a-z0-9.+-]+\\+)?json(?:\\s*;|$)/i.test(value);
}

async function readBody(response, strictJson = false) {
  const text = await response.text();
  if (text.length === 0) return undefined;
  if (!isJsonContentType(response.headers.get("content-type") || "")) return text;
  try {
    return JSON.parse(text);
  } catch (error) {
    if (strictJson) throw error;
    return text;
  }
}

function appendParameter(url, parameter, value) {
  if (value === undefined) {
    if (parameter.required) throw new TypeError(\`Missing required \${parameter.location} parameter '\${parameter.name}'.\`);
    return;
  }
  if (value === null || typeof value === "object") {
    if (Array.isArray(value) && parameter.location === "query") {
      for (const item of value) url.searchParams.append(parameter.name, String(item));
      return;
    }
    throw new TypeError(\`Parameter '\${parameter.name}' must be a scalar.\`);
  }
  if (parameter.location === "path") return;
  return value;
}

function pathValue(value, name) {
  if (value === undefined || value === null || typeof value === "object") {
    throw new TypeError(\`Missing or invalid required path parameter '\${name}'.\`);
  }
  return encodeURIComponent(String(value));
}

export function createClient(options) {
  if (!options || typeof options.baseUrl !== "string") {
    throw new TypeError("createClient requires a baseUrl string.");
  }
  let base;
  try {
    base = new URL(options.baseUrl);
  } catch {
    throw new TypeError("baseUrl must be an absolute HTTP or HTTPS URL.");
  }
  if (!["http:", "https:"].includes(base.protocol) || base.search || base.hash) {
    throw new TypeError("baseUrl must be an HTTP(S) URL without a query or fragment.");
  }
  if (!base.pathname.endsWith("/")) base.pathname += "/";
  const fetcher = options.fetch ?? globalThis.fetch;
  if (typeof fetcher !== "function") throw new TypeError("A Fetch implementation is required.");
  const client = Object.create(null);

  for (const operation of operations) {
    client[operation.id] = async (input = {}) => {
      if (input === null || typeof input !== "object") throw new TypeError("Operation input must be an object.");
      const requestPath = operation.path.replace(/\\{([^{}]+)\\}/g, (_, name) =>
        pathValue(input.path?.[name], name));
      const url = new URL(requestPath.replace(/^\\//, ""), base);
      for (const parameter of operation.parameters) {
        if (parameter.location === "query") {
          const value = appendParameter(url, parameter, input.query?.[parameter.name]);
          if (Array.isArray(value)) {
            if (parameter.explode === false) {
              url.searchParams.append(parameter.name, value.map(String).join(","));
            } else {
              for (const item of value) url.searchParams.append(parameter.name, String(item));
            }
          } else if (value !== undefined) {
            url.searchParams.append(parameter.name, String(value));
          }
        }
      }

      const defaults = typeof options.headers === "function" ? options.headers() : options.headers;
      const headers = new Headers(defaults);
      headers.set("Accept", "application/json");
      if (input.headers !== undefined) {
        for (const [name, value] of new Headers(input.headers)) headers.set(name, value);
      }
      for (const parameter of operation.parameters) {
        if (parameter.location !== "header") continue;
        const value = new Headers(input.headers).get(parameter.name);
        if (value === null && parameter.required) {
          throw new TypeError(\`Missing required header parameter '\${parameter.name}'.\`);
        }
        if (value !== null) headers.set(parameter.name, value);
      }

      const init = {
        method: operation.method,
        headers,
        credentials: options.credentials ?? "same-origin",
        signal: input.signal
      };
      if (operation.requestBody) {
        if (input.body === undefined && operation.requestBody.required) {
          throw new TypeError("Missing required JSON request body.");
        }
        if (input.body !== undefined) {
          headers.set("Content-Type", "application/json");
          init.body = JSON.stringify(input.body);
        }
      }
      const response = await fetcher(url, init);
      const responseSpec = operation.responses.find(item => item.status === String(response.status)) ??
        operation.responses.find(item => item.status === "2XX") ??
        operation.responses.find(item => item.status === "default");
      const body = await readBody(response, response.ok && responseSpec?.json === true);
      if (!response.ok) throw new ApiError(response, body);

      if (responseSpec?.json && !isJsonContentType(response.headers.get("content-type") || "")) {
        throw new TypeError(\`Expected a JSON response from \${operation.id}.\`);
      }
      if (responseSpec && !responseSpec.json && body !== undefined) {
        throw new TypeError(\`Expected an empty response from \${operation.id}.\`);
      }
      if (responseSpec?.json && body === undefined) {
        throw new TypeError(\`Expected a JSON response body from \${operation.id}.\`);
      }
      return body;
    };
  }
  return Object.freeze(client);
}
`;
}

export function generateArtifacts(document) {
  object(document, "document");
  const { schemas, operations } = parseOperations(document);
  return {
    "zplus-api.js": generateClient(operations),
    "zplus-api.d.ts": generateTypes(schemas, operations)
  };
}
