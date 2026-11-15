import { attachDatabasePool } from "@vercel/functions";
import { Pool } from "pg";
import { createRegistryHandler } from "../lib/registry.js";
import { createNeonPackageStorage } from "../lib/neon-storage.js";

const pool = process.env.DATABASE_URL
  ? new Pool({
      connectionString: process.env.DATABASE_URL,
      max: 5,
      connectionTimeoutMillis: 5000,
      idleTimeoutMillis: 10000
    })
  : null;

if (pool) attachDatabasePool(pool);

const storage = createNeonPackageStorage();

const handler = createRegistryHandler({
  pool,
  storage
});

export default handler;
