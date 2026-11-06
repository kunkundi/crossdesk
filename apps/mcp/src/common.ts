import { randomBytes, timingSafeEqual } from "node:crypto";
import { z } from "zod";

export type Data = Record<string, any>;
export const token = () => randomBytes(24).toString("base64url");
export const equal = (a: string, b: string) => {
  const left = Buffer.from(a),
    right = Buffer.from(b);
  return left.length === right.length && timingSafeEqual(left, right);
};
export class Failure extends Error {
  constructor(
    public code: string,
    public sideEffect = false,
    public retryable = false,
    public reason?: string,
  ) {
    super(code);
  }
}
export function failure(error: unknown): Data {
  const e = error instanceof Failure ? error : new Failure("INTERNAL_ERROR");
  return {
    code: e.code,
    retryable: e.retryable,
    side_effect_possible: e.sideEffect,
    ...(e.reason ? { reason: e.reason } : {}),
  };
}
export function parse<T extends z.ZodType>(
  schema: T,
  value: unknown,
): z.output<T> {
  const result = schema.safeParse(value);
  if (!result.success) throw new Failure("INVALID_ARGUMENT");
  return result.data;
}
export const id = z
  .string()
  .min(1)
  .max(128)
  .regex(/^[a-zA-Z0-9_-]+$/);
export const alias = z
  .string()
  .min(1)
  .max(80)
  .regex(/^[\p{L}\p{N} _.-]+$/u);
export const mode = z.enum(["observe", "control"]);
export const unlockPassword = z.string().min(1).max(256)
  .refine((value) => Buffer.byteLength(value) <= 256 && !/[\0\r\n\t]/.test(value));
export const panel = z.enum(["auto", "apps", "browser", "terminal", "off"]);
export type Panel = z.infer<typeof panel>;
export const videoOptions = z.strictObject({
  decode_path: z.enum(["browser", "minirtc"]).default("browser"),
  decoder: z.enum(["auto", "software"]).default("auto"),
});
export type VideoOptions = z.infer<typeof videoOptions>;
export const host = z
  .string()
  .min(1)
  .max(253)
  .regex(/^[a-zA-Z0-9.:-]+$/);
export const point = z.strictObject({
  x: z.number().finite().min(0).max(8192),
  y: z.number().finite().min(0).max(8192),
});
const button = z.enum(["left", "right", "middle"]).optional();
const unique = (keys: string[]) => new Set(keys).size === keys.length;
const modifiers = z.array(z.enum(["CTRL", "ALT", "SHIFT", "META"])).max(4).refine(unique)
  .describe("Modifiers held during this action and released afterwards, including on cancellation.").optional();
const key = z.enum([
  ..."ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789".split(""),
  "BACKSPACE", "TAB", "ENTER", "SHIFT", "CTRL", "ALT", "PAUSE", "CAPS_LOCK",
  "ESC", "SPACE", "PAGE_UP", "PAGE_DOWN", "END", "HOME", "LEFT", "UP", "RIGHT", "DOWN",
  "INSERT", "DELETE", "META", "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
  "F13", "F14", "F15", "F16", "F17", "F18",
  "F19", "F20", "F21", "F22", "F23", "F24",
  "NUMPAD0", "NUMPAD1", "NUMPAD2", "NUMPAD3", "NUMPAD4", "NUMPAD5",
  "NUMPAD6", "NUMPAD7", "NUMPAD8", "NUMPAD9", "NUM_LOCK", "SCROLL_LOCK",
  "PRINT_SCREEN", "MENU", "MULTIPLY", "ADD", "SUBTRACT", "DECIMAL",
  "DIVIDE", "VOLUME_MUTE", "VOLUME_DOWN", "VOLUME_UP", "MEDIA_NEXT", "MEDIA_PREVIOUS",
  "MEDIA_STOP", "MEDIA_PLAY_PAUSE",
  "SEMICOLON", "EQUAL", "COMMA", "MINUS", "PERIOD", "SLASH", "BACKQUOTE", "BRACKET_LEFT", "BACKSLASH", "BRACKET_RIGHT", "QUOTE",
]);
export const action = z.discriminatedUnion("type", [
  point.extend({ type: z.literal("move"), modifiers }),
  point.extend({ type: z.literal("mouse_down"), button }),
  point.extend({ type: z.literal("mouse_up"), button }),
  z.strictObject({ type: z.literal("key_down"), key }),
  z.strictObject({ type: z.literal("key_up"), key }),
  point.extend({ type: z.literal("click"), button, modifiers }),
  point.extend({ type: z.literal("double_click"), button, modifiers }),
  point.extend({
    type: z.literal("scroll"),
    direction: z.enum(["up", "down", "left", "right"]),
    ticks: z.int().min(1).max(10).optional(),
    modifiers,
  }),
  z.strictObject({
    type: z.literal("drag"),
    path: z.array(point).min(2).max(128),
    duration_ms: z.int().min(100).max(30000).optional(),
    button,
    modifiers,
  }),
  z.strictObject({ type: z.literal("key_press"), key, modifiers,
    duration_ms: z.int().min(10).max(30000).optional() })
    .refine((p) => !p.modifiers?.some((modifier) => modifier === p.key)),
  z.strictObject({
    type: z.literal("hotkey"),
    duration_ms: z.int().min(10).max(30000).optional(),
    keys: z.array(key).min(2).max(6).refine(unique),
  }),
  z.strictObject({
    type: z.literal("type_text"),
    text: z
      .string()
      .min(1)
      .max(16384)
      .refine((s) => Buffer.byteLength(s) <= 16384 && !s.includes("\0")),
  }),
]);
const normalizedPoint = z.strictObject({ x: z.number().finite().min(0).max(1), y: z.number().finite().min(0).max(1) });
export const viewerInput = z.strictObject({
  input_epoch: z.int().min(0), generation: z.int().min(0),
  width: z.int().min(1).max(8192), height: z.int().min(1).max(8192),
  action: z.discriminatedUnion("type", [
    normalizedPoint.extend({ type: z.literal("move") }),
    normalizedPoint.extend({ type: z.literal("mouse_down"), button }),
    normalizedPoint.extend({ type: z.literal("mouse_up"), button }),
    normalizedPoint.extend({ type: z.literal("scroll"), direction: z.enum(["up", "down", "left", "right"]), ticks: z.int().min(1).max(10) }),
    z.strictObject({ type: z.literal("key_down"), key }),
    z.strictObject({ type: z.literal("key_up"), key }),
    z.strictObject({ type: z.literal("type_text"), text: z.string().min(1).max(16384)
      .refine((value) => Buffer.byteLength(value) <= 16384 && !value.includes("\0")) }),
    z.strictObject({ type: z.literal("release") }),
  ]),
});
export const screenshot = z.strictObject({
  session_id: id,
  region: z
    .strictObject({
      x: z.int().min(0),
      y: z.int().min(0),
      width: z.int().min(1).max(8192),
      height: z.int().min(1).max(8192),
    })
    .optional(),
  max_edge: z.int().min(64).max(2048).optional(),
  after_frame_id: z.int().min(0).optional(),
  generation: z.int().min(0).optional(),
  timeout_ms: z.int().min(0).max(5000).optional(),
});
export const uiConnect = z.strictObject({
  device_alias: alias.optional(),
  device_id: z
    .string()
    .regex(/^\d{9}$/)
    .optional(),
  label: alias.optional(),
  password: z
    .string()
    .max(256)
    .refine((s) => Buffer.byteLength(s) <= 256 && !/[\0\r\n]/.test(s))
    .optional(),
  mode: mode.default("observe"),
  display_index: z.int().min(0).max(63).default(0),
  server: host.optional(),
  port: z.int().min(1).max(65535).optional(),
  save_device: z.boolean().default(false),
  remember_password: z.boolean().default(false),
  unlock_password: unlockPassword.optional(),
  remember_unlock_password: z.boolean().default(false),
  persistent_authorization: z.boolean().default(false),
});
