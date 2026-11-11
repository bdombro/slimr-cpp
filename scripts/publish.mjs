import { execFileSync } from "node:child_process";
import { existsSync, lstatSync, mkdirSync, readFileSync, readdirSync, readlinkSync, realpathSync, symlinkSync, unlinkSync, writeFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const usage = "Usage: just publish [--local [--undo]] [--dry] (--include key,key | --dirty | --all)";
const trailer = "Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>";
const semver = /^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/;
const own = (value, key) => Object.hasOwn(value, key);
const json = value => `${JSON.stringify(value, null, 2)}\n`;
const inside = (parent, child) => child === parent || child.startsWith(`${parent}${path.sep}`);
const stat = file => {
  try { return lstatSync(file); } catch (error) {
    if (error.code === "ENOENT") return undefined;
    throw error;
  }
};

export function parseArgs(args) {
  const options = {};
  for (let i = 0; i < args.length; i++) {
    const key = args[i].slice(2);
    if (!args[i].startsWith("--") || !["include", "dirty", "all", "local", "undo", "dry"].includes(key) || own(options, key))
      throw new Error(`${usage}\nUnknown or repeated argument: ${args[i]}`);
    if (key === "include") {
      const value = args[++i];
      if (!value || value.startsWith("--")) throw new Error(usage);
      options.include = value.split(",");
      if (options.include.some(key => !/^[a-z][a-z0-9-]*$/.test(key)) || new Set(options.include).size !== options.include.length)
        throw new Error("Include keys must be distinct, nonempty package directory keys.");
    } else options[key] = true;
  }
  if (["include", "dirty", "all"].filter(key => own(options, key)).length !== 1 || (options.undo && !options.local))
    throw new Error(usage);
  return options;
}

export class Publisher {
  constructor(options, { directory = root, env = process.env, output = console.log, execute } = {}) {
    this.options = options;
    this.directory = path.resolve(directory);
    this.env = env;
    this.output = output;
    this.execute = execute ?? ((command, args) => execFileSync(command, args, {
      cwd: this.directory, env: this.env, encoding: "utf8", stdio: ["ignore", "pipe", "pipe"],
    }));
  }

  git(...args) {
    // A read-only preview must not refresh/write the index through git status.
    return this.execute("git", args[0] === "status" ? ["--no-optional-locks", ...args] : args).trimEnd();
  }
  read(file) { return JSON.parse(readFileSync(path.join(this.directory, file), "utf8")); }
  write(file, value) {
    const destination = path.join(this.directory, file);
    mkdirSync(path.dirname(destination), { recursive: true });
    writeFileSync(destination, json(value));
  }

  assertPlainPath(file) {
    let current = this.directory;
    for (const component of file.split("/")) {
      current = path.join(current, component);
      if (stat(current)?.isSymbolicLink()) throw new Error(`Refusing symlinked release path: ${file}`);
    }
  }

  packages() {
    const packages = readdirSync(path.join(this.directory, "packages"), { withFileTypes: true })
      .filter(entry => entry.isDirectory()).map(entry => {
        const directory = `packages/${entry.name}`;
        this.assertPlainPath(`${directory}/vcpkg.json`);
        const manifest = this.read(`${directory}/vcpkg.json`);
        if (!/^[a-z][a-z0-9-]*$/.test(entry.name) || !/^[a-z][a-z0-9-]*$/.test(manifest.name))
          throw new Error(`Invalid package identity: ${directory}`);
        return { key: entry.name, directory, name: manifest.name, manifest };
      }).sort((a, b) => a.key.localeCompare(b.key));
    if (new Set(packages.map(pkg => pkg.name)).size !== packages.length) throw new Error("Duplicate manifest package names.");
    return packages;
  }

  dependencies(manifest) {
    return [manifest.dependencies ?? [], ...Object.values(manifest.features ?? {}).map(feature => feature.dependencies ?? [])];
  }

  dirtyPaths(...paths) {
    return this.git("status", "--porcelain=v1", "-z", "--untracked-files=all", "--no-renames", "--", ...paths)
      .split("\0").filter(Boolean).map(line => line.slice(3));
  }

  localPlan(packages) {
    const configured = this.env.VCPKG_OVERLAY_PORTS;
    if (!configured || configured !== configured.trim() || /[:;\r\n]/.test(configured))
      throw new Error("VCPKG_OVERLAY_PORTS must name one directory, not a path list.");
    const overlay = path.resolve(this.directory, configured);
    if (inside(this.directory, overlay) || inside(overlay, this.directory))
      throw new Error("Overlay must be separate from the source checkout.");
    if (this.env.VCPKG_ROOT) {
      const configuredRoot = path.resolve(this.directory, this.env.VCPKG_ROOT);
      const vcpkg = existsSync(configuredRoot) ? realpathSync(configuredRoot) : configuredRoot;
      if (overlay === vcpkg || ["buildtrees", "downloads", "installed", "packages", "ports", "scripts", ".git"]
        .some(directory => inside(path.join(vcpkg, directory), overlay)))
        throw new Error("Overlay must not be a vcpkg source, installed, or cache directory.");
    }
    // Refuse symlinked ancestors: otherwise a benign-looking overlay can write into source/caches.
    for (let current = overlay; ; current = path.dirname(current)) {
      const info = stat(current);
      if (info && (!info.isDirectory() || info.isSymbolicLink())) throw new Error(`Overlay ancestor is not a real directory: ${current}`);
      if (current === path.dirname(current)) break;
    }
    return {
      mode: "local", overlay,
      packages: packages.map(pkg => {
        const destination = path.join(overlay, pkg.name);
        const target = path.join(this.directory, pkg.directory);
        const info = stat(destination);
        if (info && !info.isSymbolicLink()) throw new Error(`Refusing nonlink collision: ${destination}`);
        const previousTarget = info ? readlinkSync(destination) : null;
        const correct = info && path.resolve(overlay, previousTarget) === target;
        return { key: pkg.key, destination, target, previousTarget,
          action: this.options.undo ? (info ? "remove" : "already absent") : (info ? (correct ? "already correct" : "replace") : "create") };
      }),
      checks: [], commits: [], destination: null,
    };
  }

  destination() {
    const branch = this.git("symbolic-ref", "--quiet", "--short", "HEAD");
    const config = key => {
      try { return this.git("config", "--get", key); } catch (error) {
        if (error.status === 1) return "";
        throw error;
      }
    };
    const remote = config(`branch.${branch}.remote`);
    const ref = config(`branch.${branch}.merge`);
    if (!remote || remote === "." || !ref.startsWith("refs/heads/"))
      return { branch, blocked: "Configure a branch upstream remote and refs/heads destination before release." };
    let url = this.git("remote", "get-url", "--push", "--all", remote);
    if (!url || url.includes("\n")) throw new Error("Release requires exactly one configured push URL.");
    if (/^https?:\/\//i.test(url)) {
      const displayed = new URL(url);
      displayed.username = "";
      displayed.password = "";
      displayed.search = "";
      displayed.hash = "";
      url = displayed.toString();
    }
    return { branch, remote, ref, url };
  }

  validateVersion(value, label) {
    if (!semver.test(value)) throw new Error(`Expected numeric version x.y.z: ${label}`);
    const parts = value.split(".").map(Number);
    if (parts.some(part => !Number.isSafeInteger(part)) || !Number.isSafeInteger(parts[2] + 1))
      throw new Error(`Version overflow: ${label}`);
    return parts;
  }

  registryPlan(selected, all) {
    const reasons = new Map(selected.map(pkg => [pkg.name, "selected"]));
    let changed = true;
    while (changed) {
      changed = false;
      for (const pkg of all) {
        const dependency = this.dependencies(pkg.manifest).flat().find(dep => reasons.has(typeof dep === "string" ? dep : dep.name));
        if (!reasons.has(pkg.name) && dependency) {
          reasons.set(pkg.name, `depends on ${typeof dependency === "string" ? dependency : dependency.name}`);
          changed = true;
        }
      }
    }
    const packages = all.filter(pkg => reasons.has(pkg.name));
    const versions = new Map(packages.map(pkg => {
      const parts = this.validateVersion(pkg.manifest.version, pkg.name);
      if (["version-semver", "version-date", "version-string"].some(key => own(pkg.manifest, key)))
        throw new Error(`Unsupported version scheme: ${pkg.name}`);
      parts[2]++;
      return [pkg.name, parts.join(".")];
    }));
    this.assertPlainPath("versions/baseline.json");
    if (this.dirtyPaths("versions").length) throw new Error("Preexisting registry metadata edits are ambiguous; commit or remove them first.");
    const baseline = existsSync(path.join(this.directory, "versions/baseline.json")) ? this.read("versions/baseline.json") : { default: {} };
    if (!baseline.default || typeof baseline.default !== "object" || Array.isArray(baseline.default)) throw new Error("Invalid registry baseline.");
    for (const [name, entry] of Object.entries(baseline.default)) {
      this.validateVersion(entry.baseline, name);
      if (!Number.isSafeInteger(entry["port-version"]) || entry["port-version"] < 0) throw new Error(`Invalid baseline port-version: ${name}`);
    }
    const planned = packages.map(pkg => {
      const manifest = structuredClone(pkg.manifest);
      const constraints = [];
      manifest.version = versions.get(pkg.name);
      delete manifest["port-version"];
      for (const dependencies of this.dependencies(manifest)) {
        for (let index = 0; index < dependencies.length; index++) {
          const dep = dependencies[index];
          const name = typeof dep === "string" ? dep : dep.name;
          if (!versions.has(name)) continue;
          const minimum = versions.get(name);
          const previous = typeof dep === "string" ? undefined : dep["version>="];
          if (previous) {
            const [version, port = "0", ...extra] = previous.split("#");
            const parts = this.validateVersion(version, name);
            if (extra.length || !/^\d+$/.test(port)) throw new Error(`Invalid dependency minimum: ${name}`);
            const next = minimum.split(".").map(Number);
            const comparison = parts.map((value, i) => value - next[i]).find(value => value !== 0) ?? 0;
            if (comparison > 0 || (comparison === 0 && Number(port) > 0)) throw new Error(`Dependency minimum exceeds planned release: ${name} ${previous}`);
          }
          dependencies[index] = { ...(typeof dep === "string" ? { name } : dep), "version>=": minimum };
          if (previous !== minimum) constraints.push({ name, from: previous ?? "(unconstrained)", to: minimum });
        }
      }
      const historyPath = `versions/${pkg.name[0]}-/${pkg.name}.json`;
      this.assertPlainPath(historyPath);
      const history = existsSync(path.join(this.directory, historyPath)) ? this.read(historyPath) : { versions: [] };
      if (!Array.isArray(history.versions)) throw new Error(`Invalid history: ${historyPath}`);
      const seen = new Set();
      for (const entry of history.versions) {
        this.validateVersion(entry.version, historyPath);
        if (!/^(?:[a-f0-9]{40}|[a-f0-9]{64})$/.test(entry["git-tree"]) ||
            !Number.isSafeInteger(entry["port-version"]) || entry["port-version"] < 0)
          throw new Error(`Invalid history entry: ${historyPath}`);
        const identity = `${entry.version}#${entry["port-version"]}`;
        if (seen.has(identity)) throw new Error(`Duplicate history entry: ${historyPath}`);
        seen.add(identity);
      }
      const current = baseline.default[pkg.name];
      if (current && !seen.has(`${current.baseline}#${current["port-version"]}`))
        throw new Error(`Baseline has no matching version history: ${pkg.name}`);
      for (const entry of history.versions) {
        const previous = entry.version.split(".").map(Number);
        const next = manifest.version.split(".").map(Number);
        const comparison = previous.map((value, i) => value - next[i]).find(value => value !== 0) ?? 0;
        if (comparison >= 0) throw new Error(`Version already registered or not newer than history: ${pkg.name} ${manifest.version}`);
      }
      return { key: pkg.key, name: pkg.name, directory: pkg.directory, reason: reasons.get(pkg.name),
        from: pkg.manifest.version, to: manifest.version, constraints, manifest, historyPath, history };
    });
    return {
      mode: "registry", packages: planned, baseline,
      metadataPaths: ["versions/baseline.json", ...planned.map(pkg => pkg.historyPath)],
      checks: planned.flatMap(pkg => ["test-pkg", "build-pkg", "lint-pkg"].map(task => ["just", task, pkg.key])),
      commits: ["Package source and bumped manifests", "Registry histories and baseline (real committed package trees)"],
      destination: this.destination(),
    };
  }

  planChanges() {
    const all = this.packages();
    const keys = this.options.include;
    if (keys?.some(key => !all.some(pkg => pkg.key === key))) throw new Error(`Unknown package key in: ${keys.join(",")}`);
    const dirty = this.options.dirty ? this.dirtyPaths("packages") : [];
    const selected = all.filter(pkg => this.options.all || keys?.includes(pkg.key) || dirty.some(file => file.startsWith(`${pkg.directory}/`)));
    if (this.options.local) return this.localPlan(selected);
    return this.registryPlan(selected, all);
  }

  showChanges(plan) {
    this.output(json(plan));
    if (plan.mode === "registry") this.output("Future package-tree hashes and registry baseline SHA are unknown until committed.");
  }

  commit(message, paths) {
    this.git("add", "-A", "--", ...paths);
    // --only excludes unrelated pre-staged work without resetting or stashing the index.
    this.git("commit", "--only", "-m", `${message}\n\n${trailer}`, "--", ...paths);
    return this.git("rev-parse", "HEAD");
  }

  publish() {
    const plan = this.planChanges();
    this.showChanges(plan);
    if (this.options.dry) {
      this.output("Dry run: checks were not run; nothing changed.");
      return plan;
    }
    if (!plan.packages.length) { this.output("No packages to publish."); return plan; }
    if (plan.mode === "local") {
      // Revalidate the entire set before the first mkdir/link/unlink.
      if (json(this.planChanges()) !== json(plan)) throw new Error("Local plan changed; retry preview.");
      if (!this.options.undo) mkdirSync(plan.overlay, { recursive: true });
      for (const pkg of plan.packages) {
        if (pkg.action === "replace") unlinkSync(pkg.destination);
        if (pkg.action === "create" || pkg.action === "replace") symlinkSync(pkg.target, pkg.destination, "dir");
        if (pkg.action === "remove") unlinkSync(pkg.destination);
      }
      return plan;
    }
    if (plan.destination.blocked) throw new Error(plan.destination.blocked);
    const state = { phase: "checks", sourceCommit: null, metadataCommit: null, push: "not attempted" };
    const initialHead = this.git("rev-parse", "HEAD");
    try {
      for (const [command, ...args] of plan.checks) {
        this.output(`Running: ${command} ${args.join(" ")}`);
        const output = this.execute(command, args);
        if (output) this.output(output);
      }
      if (this.git("rev-parse", "HEAD") !== initialHead || json(this.planChanges()) !== json(plan))
        throw new Error("Release plan changed during checks; no release writes performed.");
      state.phase = "version writes";
      for (const pkg of plan.packages) this.write(`${pkg.directory}/vcpkg.json`, pkg.manifest);
      state.phase = "source commit";
      state.sourceCommit = this.commit("Release packages: " + plan.packages.map(pkg => `${pkg.name} ${pkg.to}`).join(", "), plan.packages.map(pkg => pkg.directory));
      state.phase = "registry metadata";
      if (this.dirtyPaths("versions").length) throw new Error("Registry metadata changed during source commit; refusing to overwrite it.");
      for (const file of plan.metadataPaths) this.assertPlainPath(file);
      for (const pkg of plan.packages) {
        const tree = this.git("rev-parse", `${state.sourceCommit}:${pkg.directory}`);
        if (this.git("cat-file", "-t", tree) !== "tree") throw new Error(`Not a committed package tree: ${pkg.name}`);
        pkg.history.versions.unshift({ "git-tree": tree, version: pkg.to, "port-version": 0 });
        plan.baseline.default[pkg.name] = { baseline: pkg.to, "port-version": 0 };
      }
      for (const pkg of plan.packages) this.write(pkg.historyPath, pkg.history);
      this.write("versions/baseline.json", plan.baseline);
      state.phase = "metadata commit";
      state.metadataCommit = this.commit("Update registry baseline", plan.metadataPaths);
      state.phase = "push";
      state.push = "attempted; remote outcome unknown on failure";
      this.git("push", "--", plan.destination.remote, `HEAD:${plan.destination.ref}`);
      state.push = "confirmed";
      this.output(`Registry baseline: ${state.metadataCommit}\nSource commit: ${state.sourceCommit}\nPush: confirmed`);
      return state;
    } catch (cause) {
      let observedHead;
      try { observedHead = this.git("rev-parse", "HEAD"); } catch { observedHead = "unavailable"; }
      const error = new Error(`Release failed: ${cause.message}\n${json({ ...state, observedHead })}No rollback attempted. Inspect working tree, index and remote before retrying.`, { cause });
      error.releaseState = state;
      throw error;
    }
  }
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try { new Publisher(parseArgs(process.argv.slice(2))).publish(); }
  catch (error) { console.error(error.message); process.exitCode = 1; }
}
