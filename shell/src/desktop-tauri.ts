import { invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";
import { createTauriDesktop } from "./desktop.ts";

/** The desktop of this window: the Tauri commands of the Rust side and the events it sends. */
export const createRealDesktop = () => createTauriDesktop({ invoke, listen: (event, handler) => listen(event, () => void handler()) });
