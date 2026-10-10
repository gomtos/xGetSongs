import { Channel, invoke } from "@tauri-apps/api/core";
import { createBridge } from "./bridge.ts";

/** The bridge of this window: the Tauri commands of the Rust side. */
export const bridge = createBridge({ invoke, channel: <T>() => new Channel<T>() });
