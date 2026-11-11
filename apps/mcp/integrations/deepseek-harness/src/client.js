export const inject = ["uiSession", "sidebarRight", "sidebarRightTabs"];

export function apply(ctx) {
  // Desktop's isolated webview supports loopback video. A Web iframe cannot
  // embed CrossDesk's protected page; do not weaken its framing policy.
  if (globalThis.dshDesktop?.protocolVersion !== 1 || !globalThis.dshDesktop.browser) return;
  ctx.effect(() => {
    const lifetime = new AbortController();
    const client = crypto.randomUUID();
    const opened = new Map();
    const views = new Map();
    const current = ctx.uiSession.adapter.current;
    let timer;
    let running = false;
    const request = async (session, action, id) => {
      const response = await fetch("/api/crossdesk.sidebar", {
        method: "POST", headers: { "content-type": "application/json" },
        body: JSON.stringify({ action, session_id: session, client_id: client, id }),
        signal: AbortSignal.any([lifetime.signal, AbortSignal.timeout(4000)]),
      });
      if (!response.ok) throw new Error("CrossDesk sidebar bridge unavailable");
      return response.json();
    };
    const tick = async () => {
      if (running || lifetime.signal.aborted) return;
      clearTimeout(timer);
      running = true;
      try {
        const session = current.getSnapshot().key;
        if (typeof session !== "string" || !ctx.sidebarRightTabs.get("browser")) return;
        const { panel } = await request(session, "poll");
        if (!panel) return;
        if (current.getSnapshot().key !== session || lifetime.signal.aborted) {
          if (!lifetime.signal.aborted) await request(session, "release", panel.id);
          return;
        }
        if (panel.session_id !== session || panel.expires_at <= Date.now() ||
            !/^http:\/\/127\.0\.0\.1:\d+\/launch\/[a-zA-Z0-9_-]{32,128}$/.test(panel.url)) return;
        for (const [id, expires] of opened) if (expires <= Date.now()) opened.delete(id);
        if (!opened.has(panel.id)) {
          const key = `${session}:${panel.view_id}`;
          const previous = views.get(key);
          const tabs = ctx.sidebarRight.openTabs.getSnapshot();
          for (const [view, id] of views) if (!tabs.some(tab => tab.tabId === id)) views.delete(view);
          const replaceTab = tabs.some(tab => tab.sessionId === session && tab.tabId === previous) ? previous : undefined;
          ctx.sidebarRight.openTab("browser", { params: { url: panel.url }, ...(replaceTab ? { replaceTab } : {}), revealIfOpened: false });
          const active = ctx.sidebarRight.active();
          if (active?.kind !== "browser") throw new Error("CrossDesk sidebar did not open");
          views.set(key, active.id);
          opened.set(panel.id, panel.expires_at);
        }
        await request(session, "ack", panel.id);
      } catch {
        // Retry transient connection/mount failures within the launcher's TTL.
        // Never log the private launcher URL or consume a second remote session.
      } finally {
        running = false;
        if (!lifetime.signal.aborted) timer = setTimeout(tick, 750);
      }
    };
    const unsubscribe = current.subscribe(() => void tick());
    void tick();
    return () => { lifetime.abort(); clearTimeout(timer); unsubscribe(); opened.clear(); views.clear(); };
  });
}
