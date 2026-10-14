# OpenMPC adapter verification — 2026-10-13

Candidate: 1.1.0. Publication and catalog PR are pending the real MPC-process
install/enable/uninstall test; no new hardware compatibility claim is made yet.

- Source: Force Link Audio 1.0.1 plus isolated OpenMPC startup/config adapter.
- Ableton Link: 902aef95bf94af49746fdda5369b42cdcfa1e6d2.
- OpenMPC installer/tools: e18a0a1da84515096d354f399dd4cd7f9c079fb5.
- ARMv7 hard-float build: passed with Zig 0.16.0, glibc target 2.31.
- Configuration parser: 12 cases passed (disabled/enabled, whitespace, numeric
  bounds, malformed/unknown keys and shell-like values).
- Upstream shared installer suite: 77 checks passed. macOS host used wrappers
  only for BSD sed's `-i` syntax and wc's padded numeric output; shipped installer
  files were not changed. Initial raw BSD runs failed on those host differences.
- Catalog checker: passed. Two expected manual-review flags: sockets in the
  audio publisher/tap and posix_spawn for the separate publisher.
- On the owner's Force, isolated /tmp harness with the actual ARM binaries:
  disabled config starts no helper; enabled config starts the helper; helper
  starts successfully at mode 0644 via the system ELF interpreter; no running
  helper remains after its parent exits. The actual music app was not restarted.
- Pending: legacy-to-addin migration, real MPC audio capture, receiver playback,
  settings preservation on upgrade, uninstall and original-addon rollback.

## Network / process review

The new config defaults to enabled=0. The tap only starts in /proc/self/exe named
MPC. It reads plain key=value settings from its own library folder and never
sources/evaluates them. Its UDP destination and the helper's input bind are fixed
to 127.0.0.1. Enabling the setting opts into Link Audio discovery/publication on
the LAN. There is no HTTP server or remote command endpoint. The helper's spawn
environment does not contain LD_PRELOAD, and it uses Linux parent-death signaling.
