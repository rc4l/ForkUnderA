import { test } from "node:test";
import assert from "node:assert/strict";
import path from "node:path";
import { judgeRegistryHost, registryBesideEngine } from "../scenarios/registryhost.mjs";

const VERIFYING = "+ Adding 127.0.0.1:10880 (revision 5172) to the verification list.\n";
const LISTED = "+ Adding 127.0.0.1:10880 (revision 5172) to the server list.\n";

test("the registry beside a build's engine is in that build's server-registry folder", () => {
  assert.equal(registryBesideEngine(path.join("b", "Release", "forkundera.exe")), path.join("b", "server-registry", "Release", "forkundera-server-registry.exe"));
});

test("a listed, answered server passes", () => {
  assert.deepEqual(judgeRegistryHost(VERIFYING + LISTED, 10880, { registryReplied: true }), []);
});

test("each step the announce did not reach is named", () => {
  assert.deepEqual(judgeRegistryHost("", 10880, null), ["the registry never put 127.0.0.1:10880 on its verification list", "the host never heard the registry answer"]);
  assert.deepEqual(judgeRegistryHost(VERIFYING, 10880, { registryReplied: false }), ["the registry never listed 127.0.0.1:10880 after verifying it", "the host never heard the registry answer"]);
  assert.deepEqual(judgeRegistryHost(VERIFYING + LISTED, 10881, { registryReplied: true }), ["the registry never put 127.0.0.1:10881 on its verification list"]);
});
