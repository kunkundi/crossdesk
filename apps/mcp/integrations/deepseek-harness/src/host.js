import { randomUUID } from "node:crypto";

export const name = "crossdesk-sidebar";
export const inject = ["connection", "tools"];
const route = "/api/crossdesk.sidebar";

// Only results from this configured MCP namespace can request navigation.
export function apply(ctx, config = {}) {
  const serverName = config.serverName ?? "crossdesk";
  if (!/^[a-zA-Z0-9_-]{1,24}$/.test(serverName)) throw new Error("Invalid CrossDesk MCP serverName");
  const tools = new Set(["crossdesk_open", "crossdesk_connect", "crossdesk_open_browser", "crossdesk_unlock"]
    .map(tool => `mcp__${serverName}__${tool}`));
  const pending = new Map();
  const prune = () => {
    for (const [id, item] of pending) if (item.expires_at <= Date.now()) pending.delete(id);
  };
  ctx.on("tools/result", (exec, result) => {
    if (!tools.has(exec.name) || result.isError || !exec.agent?.session?.id) return;
    const panel = result.value?.structuredContent;
    if (panel?.browser_host !== "deepseek-harness" || panel.browser_launch !== "host" ||
        typeof panel.view_id !== "string" || !/^[a-zA-Z0-9_-]{16,128}$/.test(panel.view_id) ||
        typeof panel.browser_launch_url !== "string" ||
        !/^http:\/\/127\.0\.0\.1:\d+\/launch\/[a-zA-Z0-9_-]{32,128}$/.test(panel.browser_launch_url)) return;
    let url;
    try { url = new URL(panel.browser_launch_url); } catch { return; }
    if (url.origin !== panel.browser_url || !Number.isFinite(panel.browser_launch_expires_at_ms)) return;
    prune();
    const expires = Math.min(panel.browser_launch_expires_at_ms, Date.now() + 60000);
    if (expires <= Date.now()) return;
    const session = exec.agent.session.id;
    for (const [id, item] of pending)
      if (item.session_id === session && item.view_id === panel.view_id) pending.delete(id);
    if (pending.size >= 64) return;
    const id = randomUUID();
    pending.set(id, { id, session_id: session, view_id: panel.view_id,
      url: url.href, expires_at: expires, owner: undefined, leased_until: 0 });
  });
  // Connection owns authentication and the application-origin fence. The
  // bridge never reads credentials, launches processes, or contacts a URL.
  ctx.connection.fetch.register({
    path: route, methods: ["POST"], requestBody: "buffered",
    async fetch(request) {
      const reply = (value, status = 200) => Response.json(value, { status, headers: { "cache-control": "no-store" } });
      if (Number(request.headers.get("content-length")) > 2048) return reply({}, 413);
      let p;
      try {
        const body = await request.text();
        if (body.length > 2048) return reply({}, 413);
        p = JSON.parse(body);
      } catch { return reply({}, 400); }
      if (!p || typeof p.session_id !== "string" || p.session_id.length > 128 ||
          typeof p.client_id !== "string" || !/^[a-zA-Z0-9_-]{16,80}$/.test(p.client_id)) return reply({}, 400);
      prune();
      if (p.action === "poll") {
        const item = [...pending.values()].find(item => item.session_id === p.session_id &&
          (!item.owner || item.owner === p.client_id || item.leased_until <= Date.now()));
        if (!item) return reply({});
        item.owner = p.client_id;
        item.leased_until = Date.now() + 5000;
        const { owner, leased_until, ...panel } = item;
        return reply({ panel });
      }
      if (p.action !== "ack" && p.action !== "release") return reply({}, 400);
      const item = pending.get(p.id);
      if (item?.session_id === p.session_id && item.owner === p.client_id) {
        if (p.action === "ack") pending.delete(p.id);
        else { item.owner = undefined; item.leased_until = 0; }
      }
      return reply({});
    },
  });
  ctx.effect(() => () => pending.clear());
}
