export type BrowserHost = "codex" | "deepseek-harness";

// Client identity selects presentation defaults, never device authorization.
export function browserHost(info: { name?: string } | undefined): BrowserHost | undefined {
  if (info?.name === "codex-mcp-client") return "codex";
  if (info?.name === "dsh-mcp-client") return "deepseek-harness";
  return undefined;
}

export function browserNextStep(host?: BrowserHost): string {
  const open = host === "deepseek-harness"
    ? "The CrossDesk MCP plugin for DeepSeek Harness Desktop automatically opens browser_launch_url in this session's sidebar. Immediately check crossdesk_get_session with view_id and wait_ms=25000; do not inspect or reinstall plugins before checking panel_status. A waiting panel is still starting. Only if panel_status remains not_confirmed, diagnose the CrossDesk plugin described in the CrossDesk MCP README; a Markdown link can also be opened with the host's Open in sidebar action. Do not use file preview or claim the sidebar is open before the panel reports ready."
    : "Open browser_launch_url in the host's built-in browser/sidebar using its browser-open tool (Codex: open_in_codex, target.type=browser, target.url=browser_launch_url, placement=right). If no host browser is available, show pairing_command for the user to run locally.";
  return `${open} Do not fetch, read, quote, or upload the launcher contents: they contain a one-time pairing credential. Then call crossdesk_get_session with view_id and wait_ms=25000. If the launcher expired, call crossdesk_open_browser with this view_id to issue another; do not reconnect the remote device.`;
}
