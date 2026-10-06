# Workspace Rules for vita-luna

## Allowed External Toolchain Access
- **VitaSDK Directory**: `~/Developer/vitasdk` (`/Users/john/Developer/vitasdk`)
  - The agent has permanent explicit permission to inspect, read, write, update packages (`vdpm`), and run tools in `~/Developer/vitasdk`.
  - No additional confirmation is needed when accessing or compiling with tools in `~/Developer/vitasdk`.

## Standard Guardrails
- **Other External Directories**: Actions on files outside `/Users/john/Developer/vita-luna` and `~/Developer/vitasdk` remain subject to user review.
