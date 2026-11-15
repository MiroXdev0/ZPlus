import {
  DeleteObjectCommand,
  GetObjectCommand,
  PutObjectCommand,
  S3Client
} from "@aws-sdk/client-s3";

const BUCKET = "zplus-packages";
const MAX_ARCHIVE_BYTES = 4 * 1024 * 1024;
const PACKAGE_NAME = /^[a-z](?:[a-z0-9-]{0,62}[a-z0-9])?$/;
const SEMANTIC_VERSION = /^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?(?:\+([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?$/;
const ARCHIVE_NAME = /^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}\.zpm$/;

function assertPackageKey(key) {
  const segments = typeof key === "string" ? key.split("/") : [];
  if (segments.length !== 4 || segments[0] !== "packages" ||
      !PACKAGE_NAME.test(segments[1]) || !SEMANTIC_VERSION.test(segments[2]) ||
      !ARCHIVE_NAME.test(segments[3])) {
    throw new Error("invalid Neon package storage key");
  }
}

function createS3Client(env) {
  const names = [
    "AWS_ACCESS_KEY_ID",
    "AWS_SECRET_ACCESS_KEY",
    "AWS_ENDPOINT_URL_S3",
    "AWS_REGION"
  ];
  const present = names.filter(name => env[name]);
  if (present.length === 0) return null;
  const missing = names.filter(name => !env[name]);
  if (missing.length) {
    throw new Error(
      `Neon Object Storage is partially configured; missing ${missing.join(", ")}`
    );
  }

  let endpoint;
  try {
    endpoint = new URL(env.AWS_ENDPOINT_URL_S3);
  } catch {
    throw new Error("AWS_ENDPOINT_URL_S3 must be a valid HTTPS URL");
  }
  if (endpoint.protocol !== "https:" || endpoint.username || endpoint.password ||
      endpoint.search || endpoint.hash) {
    throw new Error("AWS_ENDPOINT_URL_S3 must be a credential-free HTTPS endpoint");
  }

  return new S3Client({
    region: env.AWS_REGION,
    endpoint: endpoint.origin,
    credentials: {
      accessKeyId: env.AWS_ACCESS_KEY_ID,
      secretAccessKey: env.AWS_SECRET_ACCESS_KEY
    },
    forcePathStyle: true,
    requestChecksumCalculation: "WHEN_REQUIRED"
  });
}

async function readArchive(response) {
  const body = response.Body;
  if (!body) throw new Error("Neon Object Storage returned an empty package body");
  if (Number.isFinite(response.ContentLength) &&
      response.ContentLength > MAX_ARCHIVE_BYTES) {
    body.destroy?.();
    throw new Error("stored package archive exceeds the registry size limit");
  }

  const chunks = [];
  let size = 0;
  try {
    for await (const chunk of body) {
      const bytes = Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk);
      size += bytes.length;
      if (size > MAX_ARCHIVE_BYTES) {
        body.destroy?.();
        throw new Error("stored package archive exceeds the registry size limit");
      }
      chunks.push(bytes);
    }
  } catch (error) {
    body.destroy?.();
    throw error;
  }
  if (Number.isFinite(response.ContentLength) && size !== response.ContentLength) {
    throw new Error("Neon Object Storage returned a truncated package archive");
  }
  return Buffer.concat(chunks, size);
}

export function createNeonPackageStorage({
  env = process.env,
  client = createS3Client(env)
} = {}) {
  if (!client) return null;
  return {
    async put(key, body, { contentType = "application/vnd.zpm.archive" } = {}) {
      assertPackageKey(key);
      const archive = Buffer.isBuffer(body) ? body : Buffer.from(body);
      if (archive.length > MAX_ARCHIVE_BYTES) {
        throw new Error("package archive exceeds the registry size limit");
      }
      await client.send(new PutObjectCommand({
        Bucket: BUCKET,
        Key: key,
        Body: archive,
        ContentLength: archive.length,
        ContentType: contentType,
        CacheControl: "private, no-store"
      }));
    },
    async get(key) {
      assertPackageKey(key);
      const response = await client.send(new GetObjectCommand({
        Bucket: BUCKET,
        Key: key
      }));
      return readArchive(response);
    },
    async del(key) {
      assertPackageKey(key);
      await client.send(new DeleteObjectCommand({
        Bucket: BUCKET,
        Key: key
      }));
    }
  };
}
