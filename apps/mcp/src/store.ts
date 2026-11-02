import { createHash } from "node:crypto";
import {
  mkdir,
  readFile,
  writeFile,
  rename,
  unlink,
  open,
  chmod,
} from "node:fs/promises";
import { homedir } from "node:os";
import { join, resolve } from "node:path";
import { z } from "zod";
import {
  alias,
  host,
  mode,
  panel,
  videoOptions,
  Failure,
  parse,
  token,
  type Data,
} from "./common.js";

const deviceSchema = z.strictObject({
  alias,
  device_id: z.string().regex(/^\d{9}$/),
  server: host,
  port: z.int().min(1).max(65535),
  authorization: mode.optional(),
  credential: z.enum(["keyring", "environment"]).optional(),
  environment_variable: z
    .string()
    .regex(/^[A-Z][A-Z0-9_]{0,127}$/)
    .optional(),
});
export type Device = z.infer<typeof deviceSchema>;
const configSchema = z.strictObject({
  version: z.literal(1),
  panel: panel.default("auto"),
  video: videoOptions.prefault({}),
  server: host.default("api.crossdesk.cn"),
  port: z.int().min(1).max(65535).default(9099),
  devices: z.array(deviceSchema).max(256),
});
export type Config = z.infer<typeof configSchema>;
export class DeviceStore {
  readonly directory: string;
  readonly file: string;
  constructor(directory?: string) {
    this.directory = resolve(
      directory ?? join(homedir(), ".config", "crossdesk", "mcp"),
    );
    this.file = join(this.directory, "devices.json");
  }
  async initialize() {
    await mkdir(this.directory, { recursive: true, mode: 0o700 });
    if (process.platform !== "win32") await chmod(this.directory, 0o700);
    await mkdir(join(this.directory, "logs"), { recursive: true, mode: 0o700 });
  }
  async config(): Promise<Config> {
    try {
      return parse(configSchema, JSON.parse(await readFile(this.file, "utf8")));
    } catch (error) {
      if ((error as NodeJS.ErrnoException).code === "ENOENT")
        return configSchema.parse({ version: 1, devices: [] });
      throw new Failure("CONFIG_INVALID");
    }
  }
  async list(): Promise<Data[]> {
    return (await this.config()).devices.map((d) => ({
      device_alias: d.alias,
      device_id: d.device_id,
      server: d.server,
      port: d.port,
      authorization: d.authorization ?? "none",
      credential_provider: d.credential ?? "none",
    }));
  }
  async device(name: string): Promise<Device> {
    const d = (await this.config()).devices.find((d) => d.alias === name);
    if (!d) throw new Failure("DEVICE_NOT_FOUND");
    return d;
  }
  private account(d: Device) {
    return createHash("sha256")
      .update(JSON.stringify([this.directory, d.server, d.port, d.device_id]))
      .digest("hex");
  }
  private async entry(d: Device) {
    try {
      const { AsyncEntry } = await import("@napi-rs/keyring");
      return new AsyncEntry("crossdesk-mcp", this.account(d), {
        linux: { store: "secret-service" },
      });
    } catch {
      throw new Failure("CREDENTIAL_STORE_UNAVAILABLE");
    }
  }
  async password(d: Device): Promise<string> {
    if (d.credential === "environment") {
      const secret =
        d.environment_variable && process.env[d.environment_variable];
      if (secret !== undefined) return secret;
    } else if (d.credential === "keyring") {
      try {
        const secret = await (await this.entry(d)).getPassword();
        if (secret !== null) return secret;
      } catch {
        throw new Failure("CREDENTIAL_STORE_UNAVAILABLE");
      }
    }
    throw new Failure("AUTH_REQUIRED");
  }
  async save(d: Device, password?: string) {
    parse(deviceSchema, d);
    const lock = await this.lock("config");
    try {
      const config = await this.config();
      const previous = config.devices.find((item) => item.alias === d.alias);
      if (
        previous &&
        (previous.device_id !== d.device_id ||
          previous.server !== d.server ||
          previous.port !== d.port)
      )
        throw new Failure("DEVICE_ALIAS_CONFLICT");
      if (password !== undefined) {
        try {
          await (await this.entry(d)).setPassword(password);
          d.credential = "keyring";
          delete d.environment_variable;
        } catch {
          throw new Failure("CREDENTIAL_STORE_UNAVAILABLE");
        }
      }
      config.devices = [
        ...config.devices.filter((item) => item.alias !== d.alias),
        d,
      ];
      parse(configSchema, config);
      const temporary = `${this.file}.${token()}.tmp`;
      try {
        await writeFile(temporary, JSON.stringify(config, null, 2) + "\n", {
          mode: 0o600,
          flag: "wx",
        });
        await rename(temporary, this.file);
      } finally {
        await unlink(temporary).catch(() => {});
      }
    } finally {
      await lock();
    }
  }
  async lock(name: string): Promise<() => Promise<void>> {
    const path = join(
      this.directory,
      `lock-${createHash("sha256").update(name).digest("hex").slice(0, 24)}`,
    );
    for (let attempt = 0; attempt < 2; ++attempt) {
      try {
        const handle = await open(path, "wx", 0o600);
        await handle.writeFile(String(process.pid));
        await handle.close();
        let released = false;
        return async () => {
          if (released) return;
          released = true;
          await unlink(path).catch(() => {});
        };
      } catch (error) {
        if ((error as NodeJS.ErrnoException).code !== "EEXIST")
          throw new Failure("CONFIG_WRITE_FAILED");
        const pid = Number(await readFile(path, "utf8").catch(() => "0"));
        if (!Number.isSafeInteger(pid) || pid < 1)
          throw new Failure("DEVICE_BUSY");
        try {
          process.kill(pid, 0);
          throw new Failure("DEVICE_BUSY");
        } catch (e) {
          if ((e as NodeJS.ErrnoException).code !== "ESRCH")
            throw new Failure("DEVICE_BUSY");
        }
        await unlink(path).catch(() => {});
      }
    }
    throw new Failure("DEVICE_BUSY");
  }
}
