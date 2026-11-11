import test from "node:test";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { existsSync, lstatSync, mkdirSync, mkdtempSync, readFileSync, readlinkSync, readdirSync, rmSync, symlinkSync, writeFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { parseArgs, Publisher } from "./publish.mjs";

const scripts = path.dirname(fileURLToPath(import.meta.url));
const script = path.join(scripts, "publish.mjs");
const encode = value => `${JSON.stringify(value, null, 2)}\n`;
const read = file => JSON.parse(readFileSync(file, "utf8"));

function fixture(t) {
  // All fixtures stay inside this checkout, never the system temporary directory.
  const base = mkdtempSync(path.join(scripts, ".publish-fixture-"));
  t.after(() => rmSync(base, { recursive: true, force: true }));
  const directory = path.join(base, "source");
  const remote = path.join(base, "remote.git");
  const overlay = path.join(base, "overlay");
  mkdirSync(directory);
  const env = { ...process.env, GIT_TERMINAL_PROMPT: "0", VCPKG_OVERLAY_PORTS: overlay };
  const run = (command, args) => execFileSync(command, args, { cwd: directory, env, encoding: "utf8", stdio: ["ignore", "pipe", "pipe"] });
  const git = (...args) => run("git", args).trimEnd();
  git("init", "--initial-branch=main");
  git("config", "user.name", "Publisher fixture");
  git("config", "user.email", "fixture@example.invalid");
  git("config", "commit.gpgsign", "false");
  git("config", "core.hooksPath", path.join(base, "no-hooks"));
  git("init", "--bare", remote);
  const write = (file, value) => {
    const destination = path.join(directory, file);
    mkdirSync(path.dirname(destination), { recursive: true });
    writeFileSync(destination, typeof value === "string" ? value : encode(value));
  };
  for (const [key, dependencies] of [["leaf", []], ["mid", ["slimr-leaf"]], ["app", [{ name: "slimr-mid", features: ["extra"] }]], ["other", []]]) {
    write(`packages/${key}/vcpkg.json`, { name: `slimr-${key}`, version: "0.1.0", dependencies });
    write(`packages/${key}/source.txt`, `${key}\n`);
  }
  write("unrelated.txt", "original\n");
  git("add", ".");
  git("commit", "-m", "Fixture foundation");
  git("remote", "add", "origin", remote);
  git("config", "branch.main.remote", "origin");
  git("config", "branch.main.merge", "refs/heads/main");
  const checks = [];
  const output = [];
  const publisher = (args, intercept) => new Publisher(parseArgs(args), {
    directory, env, output: value => output.push(value),
    execute: (command, args) => {
      const result = intercept?.(command, args);
      if (result !== undefined) return result;
      if (command === "just") { checks.push(args); return ""; }
      return run(command, args);
    },
  });
  return { base, directory, remote, overlay, env, run, git, write, checks, output, publisher };
}

function snapshot(f) {
  const files = {};
  const visit = directory => {
    for (const entry of readdirSync(directory, { withFileTypes: true })) {
      if (entry.name === ".git") continue;
      const file = path.join(directory, entry.name);
      if (entry.isDirectory()) visit(file);
      else files[path.relative(f.directory, file)] = entry.isSymbolicLink() ? readlinkSync(file) : readFileSync(file, "utf8");
    }
  };
  visit(f.directory);
  return { files, head: f.git("rev-parse", "HEAD"), index: f.git("ls-files", "--stage"), status: f.git("status", "--porcelain=v1", "--untracked-files=all") };
}

test("CLI requires one valid selector and rejects malformed or contradictory flags", () => {
  for (const args of [[], ["--local"], ["--all", "--dirty"], ["--include"], ["--include", "leaf,"], ["--include", ",leaf"],
    ["--include", "leaf,leaf"], ["--include", "../leaf"], ["--undo", "--all"], ["--all", "--all"], ["--wat"], ["leaf"]]) {
    assert.throws(() => parseArgs(args), undefined, args.join(" "));
  }
  assert.deepEqual(parseArgs(["--local", "--undo", "--dry", "--include", "leaf,app"]),
    { local: true, undo: true, dry: true, include: ["leaf", "app"] });
});

test("registry preview is check-free, mutation-free and shared with normal planning", t => {
  const f = fixture(t);
  f.write("packages/leaf/source.txt", "changed\n");
  const before = snapshot(f);
  const plan = f.publisher(["--dry", "--include", "leaf"]).publish();
  assert.deepEqual(snapshot(f), before);
  assert.equal(f.checks.length, 0);
  assert.deepEqual(plan, f.publisher(["--include", "leaf"]).planChanges());
  assert.deepEqual(plan.packages.map(pkg => pkg.key), ["app", "leaf", "mid"]);
  assert.deepEqual(plan.checks, ["app", "leaf", "mid"].flatMap(key => ["test-pkg", "build-pkg", "lint-pkg"].map(task => ["just", task, key])));
  assert.equal(plan.packages.find(pkg => pkg.key === "app").manifest.dependencies[0]["version>="], "0.1.1");
  assert.deepEqual(plan.packages.find(pkg => pkg.key === "app").manifest.dependencies[0].features, ["extra"]);
  assert.match(f.output.join("\n"), /checks were not run/);
  assert.match(f.output.join("\n"), /unknown until committed/);
});

test("dirty selection includes unstaged, staged and untracked package files, not unrelated work", t => {
  const f = fixture(t);
  f.write("unrelated.txt", "unrelated");
  f.write("packages/other/source.txt", "unstaged");
  assert.deepEqual(f.publisher(["--local", "--dirty", "--dry"]).publish().packages.map(pkg => pkg.key), ["other"]);
  f.write("packages/leaf/new.txt", "untracked");
  f.git("add", "packages/other/source.txt");
  assert.deepEqual(f.publisher(["--local", "--dirty", "--dry"]).publish().packages.map(pkg => pkg.key), ["leaf", "other"]);
});

test("registry preview omits HTTP credentials and query parameters", t => {
  const f = fixture(t);
  f.git("remote", "set-url", "--push", "origin", "https://private-user:fixture-password@example.invalid/repo.git?token=fixture-token#private");
  const plan = f.publisher(["--dry", "--all"]).publish();
  assert.equal(plan.destination.url, "https://example.invalid/repo.git");
  assert.doesNotMatch(f.output.join("\n"), /private-user|fixture-password|fixture-token/);
  assert.equal(f.checks.length, 0);
});

test("unexpected Git configuration errors are surfaced before release checks", t => {
  const f = fixture(t);
  const before = snapshot(f);
  const publisher = f.publisher(["--all"], (command, args) => {
    if (command === "git" && args[0] === "config" && args[1] === "--get")
      throw Object.assign(new Error("config read failed"), { status: 128 });
  });
  assert.throws(() => publisher.publish(), /config read failed/);
  assert.deepEqual(snapshot(f), before);
  assert.equal(f.checks.length, 0);
});

test("feature dependencies participate in closure and retain dependency options during constraint updates", t => {
  const f = fixture(t);
  f.write("packages/other/vcpkg.json", {
    name: "slimr-other", version: "0.1.0", "port-version": 3,
    features: { tests: { dependencies: [{ name: "slimr-leaf", "version>=": "0.1.0#2", "default-features": false, platform: "!windows" }] } },
  });
  const plan = f.publisher(["--dry", "--include", "leaf"]).publish();
  const pkg = plan.packages.find(pkg => pkg.key === "other");
  assert.equal(pkg.reason, "depends on slimr-leaf");
  assert.equal(pkg.to, "0.1.1");
  assert.equal(Object.hasOwn(pkg.manifest, "port-version"), false);
  assert.deepEqual(pkg.manifest.features.tests.dependencies, [
    { name: "slimr-leaf", "version>=": "0.1.1", "default-features": false, platform: "!windows" },
  ]);
  assert.equal(f.checks.length, 0);
});

test("local planning validates the complete set before creating any directory/link", t => {
  const f = fixture(t);
  assert.throws(() => f.publisher(["--local", "--include", "leaf,missing"]).publish(), /Unknown package/);
  assert.equal(existsSync(f.overlay), false);
  for (const value of ["", `${f.overlay}:${f.overlay}`, `${f.overlay};other`, f.directory, path.join(f.directory, "packages")]) {
    f.env.VCPKG_OVERLAY_PORTS = value;
    assert.throws(() => f.publisher(["--local", "--all"]).publish());
  }
  f.env.VCPKG_OVERLAY_PORTS = f.overlay;
  f.publisher(["--local", "--dry", "--all"]).publish();
  assert.equal(existsSync(f.overlay), false);
  f.env.VCPKG_ROOT = path.join(f.base, "vcpkg");
  mkdirSync(path.join(f.env.VCPKG_ROOT, "ports"), { recursive: true });
  f.env.VCPKG_OVERLAY_PORTS = path.join(f.env.VCPKG_ROOT, "ports");
  assert.throws(() => f.publisher(["--local", "--all"]).publish(), /source, installed, or cache/);
  assert.deepEqual(readdirSync(f.env.VCPKG_OVERLAY_PORTS), []);
  f.env.VCPKG_OVERLAY_PORTS = f.overlay;
  mkdirSync(f.overlay);
  writeFileSync(path.join(f.overlay, "slimr-other"), "not a link");
  assert.throws(() => f.publisher(["--local", "--all"]).publish(), /nonlink collision/);
  assert.throws(() => f.publisher(["--local", "--undo", "--all"]).publish(), /nonlink collision/);
  assert.equal(existsSync(path.join(f.overlay, "slimr-leaf")), false);
  symlinkSync(path.join(f.base, "old-source"), path.join(f.overlay, "slimr-leaf"));
  assert.throws(() => f.publisher(["--local", "--all"]).publish(), /nonlink collision/);
  assert.equal(readlinkSync(path.join(f.overlay, "slimr-leaf")), path.join(f.base, "old-source"));
  assert.equal(existsSync(path.join(f.overlay, "slimr-app")), false);
  rmSync(path.join(f.overlay, "slimr-other"));
  mkdirSync(path.join(f.overlay, "slimr-other"));
  assert.throws(() => f.publisher(["--local", "--undo", "--all"]).publish(), /nonlink collision/);
});

test("local replacement/undo touch only selected named links, never targets, and are idempotent", t => {
  const f = fixture(t);
  const before = snapshot(f);
  for (let i = 0; i < 2; i++) f.publisher(["--local", "--include", "leaf"]).publish();
  const link = path.join(f.overlay, "slimr-leaf");
  assert.equal(readlinkSync(link), path.join(f.directory, "packages/leaf"));
  assert.equal(existsSync(path.join(f.overlay, "slimr-mid")), false);
  f.publisher(["--local", "--undo", "--dry", "--all"]).publish();
  assert.equal(lstatSync(link).isSymbolicLink(), true);
  for (let i = 0; i < 2; i++) f.publisher(["--local", "--undo", "--all"]).publish();
  assert.equal(existsSync(link), false);
  const foreignTarget = path.join(f.base, "foreign-source");
  mkdirSync(foreignTarget);
  writeFileSync(path.join(foreignTarget, "keep.txt"), "preserve target\n");
  symlinkSync(foreignTarget, link);
  symlinkSync(foreignTarget, path.join(f.overlay, "unselected"));
  const preview = f.publisher(["--local", "--dry", "--include", "leaf"]).publish();
  assert.equal(preview.packages[0].action, "replace");
  assert.equal(readlinkSync(link), foreignTarget);
  f.publisher(["--local", "--include", "leaf"]).publish();
  assert.equal(readlinkSync(link), path.join(f.directory, "packages/leaf"));
  assert.equal(readFileSync(path.join(foreignTarget, "keep.txt"), "utf8"), "preserve target\n");
  f.publisher(["--local", "--undo", "--include", "leaf"]).publish();
  symlinkSync(path.join(f.base, "missing"), link);
  f.publisher(["--local", "--undo", "--include", "leaf"]).publish();
  assert.equal(existsSync(link), false);
  symlinkSync(foreignTarget, link);
  f.publisher(["--local", "--undo", "--include", "leaf"]).publish();
  assert.equal(readlinkSync(path.join(f.overlay, "unselected")), foreignTarget);
  assert.equal(readFileSync(path.join(foreignTarget, "keep.txt"), "utf8"), "preserve target\n");
  assert.deepEqual(snapshot(f), before);
  assert.equal(f.checks.length, 0);
});

test("all checks precede any release writes, commits or pushes", t => {
  const f = fixture(t);
  const before = snapshot(f);
  let count = 0;
  assert.throws(() => f.publisher(["--include", "leaf"], command => {
    if (command === "just" && ++count === 8) throw new Error("check failed");
  }).publish(), error => {
    assert.match(error.message, /check failed/);
    assert.equal(error.releaseState.sourceCommit, null);
    assert.equal(error.releaseState.metadataCommit, null);
    assert.equal(error.releaseState.push, "not attempted");
    return true;
  });
  assert.equal(count, 8);
  assert.deepEqual(snapshot(f), before);
  assert.equal(f.git("--git-dir", f.remote, "for-each-ref", "--format=%(refname)"), "");
});

test("release uses committed trees, preserves unrelated dirty/staged files, history and baseline, and pushes", t => {
  const f = fixture(t);
  const oldTree = f.git("rev-parse", "HEAD:packages/leaf");
  f.write("versions/s-/slimr-leaf.json", { versions: [{ "git-tree": oldTree, version: "0.1.0", "port-version": 0 }] });
  f.write("versions/baseline.json", { default: {
    "slimr-leaf": { baseline: "0.1.0", "port-version": 0 },
    "slimr-other": { baseline: "0.1.0", "port-version": 0 },
  } });
  f.git("add", "versions");
  f.git("commit", "-m", "Existing registry fixture");
  f.write("unrelated.txt", "staged\n");
  f.git("add", "unrelated.txt");
  f.write("unrelated.txt", "unstaged too\n");
  f.write("packages/other/source.txt", "unselected package staged\n");
  f.git("add", "packages/other/source.txt");
  f.write("new-unrelated.txt", "untracked\n");
  f.write("packages/leaf/new-source.txt", "release source\n");
  const unrelatedIndex = f.git("ls-files", "--stage", "--", "unrelated.txt", "packages/other");
  const state = f.publisher(["--include", "leaf"]).publish();
  assert.equal(state.push, "confirmed");
  assert.equal(f.checks.length, 9);
  assert.equal(f.git("ls-files", "--stage", "--", "unrelated.txt", "packages/other"), unrelatedIndex);
  assert.equal(readFileSync(path.join(f.directory, "unrelated.txt"), "utf8"), "unstaged too\n");
  assert.equal(readFileSync(path.join(f.directory, "new-unrelated.txt"), "utf8"), "untracked\n");
  for (const key of ["app", "leaf", "mid"]) {
    const history = read(path.join(f.directory, `versions/s-/slimr-${key}.json`));
    const tree = history.versions[0]["git-tree"];
    assert.equal(tree, f.git("rev-parse", `${state.sourceCommit}:packages/${key}`));
    assert.equal(f.git("cat-file", "-t", tree), "tree");
    assert.equal(history.versions[0].version, "0.1.1");
    assert.equal(JSON.parse(f.git("show", `${tree}:vcpkg.json`)).version, "0.1.1");
    if (key === "leaf") assert.equal(history.versions[1]["git-tree"], oldTree);
  }
  const baseline = read(path.join(f.directory, "versions/baseline.json")).default;
  assert.equal(baseline["slimr-other"].baseline, "0.1.0");
  assert.equal(baseline["slimr-leaf"].baseline, "0.1.1");
  assert.equal(f.git("--git-dir", f.remote, "rev-parse", "refs/heads/main"), state.metadataCommit);
  assert.equal(f.git("show", `${state.sourceCommit}:unrelated.txt`), "original");
  assert.equal(f.git("show", `${state.sourceCommit}:packages/other/source.txt`), "other");
  for (const sha of [state.sourceCommit, state.metadataCommit]) assert.match(f.git("show", "-s", "--format=%B", sha), /Co-authored-by: Copilot </);
  assert.ok(f.git("diff-tree", "--no-commit-id", "--name-only", "-r", state.sourceCommit).split("\n").every(file => /packages\/(app|leaf|mid)\//.test(file)));
  assert.ok(f.git("diff-tree", "--no-commit-id", "--name-only", "-r", state.metadataCommit).split("\n").every(file => file.startsWith("versions/")));
});

test("first release works without history and refuses missing upstream before checks/writes", t => {
  const f = fixture(t);
  f.git("config", "--unset", "branch.main.remote");
  const before = snapshot(f);
  assert.match(f.publisher(["--dry", "--all"]).publish().destination.blocked, /upstream/);
  assert.throws(() => f.publisher(["--all"]).publish(), /upstream/);
  assert.equal(f.checks.length, 0);
  assert.deepEqual(snapshot(f), before);
  f.git("config", "branch.main.remote", "origin");
  const state = f.publisher(["--include", "other"]).publish();
  assert.equal(state.push, "confirmed");
  assert.equal(read(path.join(f.directory, "versions/s-/slimr-other.json")).versions.length, 1);
});

test("preexisting metadata edits and conflicting histories fail before checks/version writes", t => {
  const f = fixture(t);
  f.write("versions/baseline.json", { default: {} });
  assert.throws(() => f.publisher(["--all"]).publish(), /metadata edits/);
  f.git("add", "versions");
  assert.throws(() => f.publisher(["--all"]).publish(), /metadata edits/);
  f.git("commit", "-m", "Fixture baseline");
  const tree = f.git("rev-parse", "HEAD:packages/other");
  f.write("versions/s-/slimr-other.json", { versions: [{ "git-tree": tree, version: "0.1.1", "port-version": 0 }] });
  f.git("add", "versions");
  f.git("commit", "-m", "Conflicting fixture history");
  const before = snapshot(f);
  assert.throws(() => f.publisher(["--include", "other"]).publish(), /already registered/);
  assert.deepEqual(snapshot(f), before);
  assert.equal(f.checks.length, 0);
});

test("source, metadata and push failures report partial state without claiming rollback or success", async t => {
  for (const failure of ["source commit", "metadata commit", "push"]) {
    await t.test(failure, t => {
      const f = fixture(t);
      let commits = 0;
      assert.throws(() => f.publisher(["--include", "other"], (command, args) => {
        if (command !== "git") return;
        if (args[0] === "commit") {
          commits++;
          if ((failure === "source commit" && commits === 1) || (failure === "metadata commit" && commits === 2)) throw new Error("fixture commit failure");
        }
        if (failure === "push" && args[0] === "push") throw new Error("fixture push failure");
      }).publish(), error => {
        assert.equal(error.releaseState.phase, failure);
        assert.equal(Boolean(error.releaseState.sourceCommit), failure !== "source commit");
        assert.equal(Boolean(error.releaseState.metadataCommit), failure === "push");
        assert.match(error.message, /No rollback attempted/);
        assert.ok(!f.output.some(line => line.startsWith("Registry baseline:")));
        if (failure === "push") assert.match(error.releaseState.push, /unknown on failure/);
        return true;
      });
    });
  }
});

test("CLI resolves root from its script, not the caller's working directory", t => {
  const f = fixture(t);
  f.write("scripts/publish.mjs", readFileSync(script, "utf8"));
  const result = execFileSync(process.execPath, [path.join(f.directory, "scripts/publish.mjs"), "--local", "--dry", "--include", "leaf"], {
    cwd: f.base, env: f.env, encoding: "utf8",
  });
  assert.match(result, /packages\/leaf/);
  assert.match(result, /checks were not run/);
  assert.equal(existsSync(f.overlay), false);
});
