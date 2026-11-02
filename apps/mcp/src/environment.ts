import { statSync } from "node:fs";

// MCP stdio is normally piped even when its host is an interactive terminal.
// Detect the desktop independently, without opening or taking over /dev/tty.
export function displayEnvironment(
  platform = process.platform,
  env = process.env,
  tty = !!(process.stdin.isTTY || process.stdout.isTTY || process.stderr.isTTY),
): { gui: boolean; terminal: boolean } {
  const ssh = !!(env.SSH_CONNECTION || env.SSH_TTY || env.SSH_CLIENT);
  let gui = false;
  if (platform === "linux") gui = !!(env.DISPLAY || env.WAYLAND_DISPLAY);
  else if (platform === "darwin" && !ssh) {
    try {
      const uid = statSync("/dev/console").uid;
      gui = uid !== 0 && uid === process.getuid?.();
    } catch { /* No logged-in desktop session. */ }
  } else if (platform === "win32") {
    gui = !ssh && env.SESSIONNAME?.toLowerCase() !== "services";
  }
  return {
    gui,
    terminal: tty || !!env.SSH_TTY || (!!env.TERM && env.TERM !== "dumb"),
  };
}
