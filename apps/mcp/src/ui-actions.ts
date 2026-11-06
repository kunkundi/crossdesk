import { z } from "zod";
import { parse, uiConnect, unlockPassword, id, Failure, type Data } from "./common.js";
import { Sessions, type View } from "./sessions.js";

export const uiSchemas = {
  ready: z.strictObject({}),
  devices: z.strictObject({}),
  status: z.strictObject({}),
  connect: uiConnect,
  video_open: z.strictObject({}),
  recovery: z.strictObject({ enabled: z.boolean(), max_attempts: z.int().min(1).max(5).default(3) }),
  unlock: z.strictObject({
    request_id: id,
    password: unlockPassword.optional(),
    remember_password: z.boolean().default(false),
    cancel: z.boolean().default(false),
  }),
  control: z.strictObject({
    operation: z.enum(["pause", "resume", "disconnect"]),
  }),
};
export type UIAction = keyof typeof uiSchemas;
export async function uiAction(
  sessions: Sessions,
  view: View,
  name: string,
  args: unknown,
  signal?: AbortSignal,
): Promise<Data> {
  if (!Object.hasOwn(uiSchemas, name)) throw new Failure("UNKNOWN_ACTION");
  const p = parse(uiSchemas[name as UIAction], args) as Data;
  view.lastSeen = Date.now();
  switch (name) {
    case "ready":
      view.ready = true;
      return sessions.describe(view);
    case "devices": {
      const config = await sessions.store.config();
      return {
        devices: await sessions.store.list(),
        defaults: { server: config.server, port: config.port },
      };
    }
    case "status":
      return sessions.status(undefined, view.id, signal);
    case "connect":
      return sessions.connectUI(view, p, signal);
    case "control":
      return sessions.viewerControl(view, p.operation);
    case "unlock":
      if (!p.cancel && !p.password) throw new Failure("UNLOCK_CREDENTIAL_REQUIRED");
      return sessions.unlockUI(view, p, signal);
    case "recovery":
      if (!view.session) throw new Failure("NOT_READY");
      return sessions.setRecovery(view.session, p.enabled, p.max_attempts);
    default:
      throw new Failure("UNKNOWN_ACTION");
  }
}
