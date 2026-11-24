import { attachDatabasePool } from "@vercel/functions";
import { Pool } from "pg";
import { createRegistryHandler } from "../lib/registry.js";
import { createNeonPackageStorage } from "../lib/neon-storage.js";
import { siteMetadata } from "../lib/site-metadata.js";
import { createVercelNodeHandler } from "../lib/vercel-adapter.js";

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

export default createVercelNodeHandler(
  handler,
  process.env.ZPM_PUBLIC_ORIGIN || siteMetadata.project.website
);
