import assert from "node:assert/strict";
import { test } from "node:test";
import { createNeonPackageStorage } from "../lib/neon-storage.js";

const key = "packages/example-package/1.2.3/123e4567-e89b-12d3-a456-426614174000.zpm";

function fakeS3Client() {
  const objects = new Map();
  return {
    objects,
    async send(command) {
      if (command.constructor.name === "PutObjectCommand") {
        objects.set(command.input.Key, Buffer.from(command.input.Body));
        return {};
      }
      if (command.constructor.name === "GetObjectCommand") {
        const body = objects.get(command.input.Key);
        if (!body) throw new Error("object not found");
        return {
          ContentLength: body.length,
          Body: {
            async *[Symbol.asyncIterator]() {
              yield body;
            }
          }
        };
      }
      if (command.constructor.name === "DeleteObjectCommand") {
        objects.delete(command.input.Key);
        return {};
      }
      throw new Error(`unexpected S3 command: ${command.constructor.name}`);
    }
  };
}

test("Neon storage writes, reads, and removes only validated package keys", async () => {
  const client = fakeS3Client();
  const storage = createNeonPackageStorage({ env: {}, client });
  const archive = Buffer.from("ZPM1 test archive");

  await storage.put(key, archive);
  assert.deepEqual(await storage.get(key), archive);
  await storage.del(key);
  assert.equal(client.objects.size, 0);
  await assert.rejects(storage.get("../unsafe.zpm"), /invalid Neon package storage key/);
});

test("Neon storage rejects partial credentials and oversized package archives", async () => {
  assert.throws(
    () => createNeonPackageStorage({ env: { AWS_ACCESS_KEY_ID: "present" } }),
    /partially configured/
  );
  const storage = createNeonPackageStorage({ env: {}, client: fakeS3Client() });
  await assert.rejects(
    storage.put(key, Buffer.alloc(4 * 1024 * 1024 + 1)),
    /size limit/
  );
});
