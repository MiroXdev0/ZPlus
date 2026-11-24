import { createRequire } from "node:module";

const require = createRequire(import.meta.url);
export const siteMetadata = require("../ecosystem-metadata.json");
