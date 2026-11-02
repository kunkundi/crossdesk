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
const key = z
  .string()
  .min(1)
  .max(16)
  .regex(/^[A-Z0-9_]+$/);
export const action = z.discriminatedUnion("type", [
  point.extend({ type: z.literal("move") }),
  point.extend({ type: z.literal("click"), button }),
  point.extend({ type: z.literal("double_click"), button }),
  point.extend({
    type: z.literal("scroll"),
    direction: z.enum(["up", "down", "left", "right"]),
    ticks: z.int().min(1).max(10).optional(),
  }),
  z.strictObject({
    type: z.literal("drag"),
    path: z.array(point).min(2).max(128),
    duration_ms: z.int().min(100).max(2000).optional(),
  }),
  z.strictObject({ type: z.literal("key_press"), key }),
  z.strictObject({
    type: z.literal("hotkey"),
    keys: z.array(key).min(2).max(6),
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
  persistent_authorization: z.boolean().default(false),
});
