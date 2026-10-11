// The server answers the tests build on: the items and the resolve responses of FakeApi.kt, and a UiState around a response.

import type { InputKind, LyricsOutcome, ResolveResponse, ResolvedItem } from "../../src/bridge/types.ts";
import { initialUiState, type UiState } from "../../src/state/types.ts";

export const LYRICS_OUTCOMES: LyricsOutcome[] = ["DESCRIPTION", "ONLINE", "NOT_FOUND", "SEARCH_OFF"];

const pad = (value: number, length: number) => String(value).padStart(length, "0");

/** An available item: artist `A<rank>` and track `T<rank>` unless given. */
export function item(rank: number, artist = `A${rank}`, track = `T${rank}`): ResolvedItem {
  return {
    rank,
    videoId: `vid${pad(rank, 8)}`,
    title: `${artist} - ${track}`,
    channel: null,
    artist,
    track,
    lowConfidence: false,
    available: true,
    unavailableReason: null,
    expectedFileName: `${pad(rank, 3)} ${artist} - ${track}.m4a`,
  };
}

/** An item the server cannot download (a private video). */
export function unavailableItem(rank = 2, reason = "비공개 영상"): ResolvedItem {
  return {
    rank,
    videoId: `vid${pad(rank, 8)}`,
    title: "[Private video]",
    channel: null,
    artist: "",
    track: "",
    lowConfidence: false,
    available: false,
    unavailableReason: reason,
    expectedFileName: null,
  };
}

/** A playlist of three: two available (the third with a guessed artist) and a private one between them. */
export function playlist(alsoVideoId: string | null = null): ResolveResponse {
  return {
    resolveId: "resolve-1",
    kind: "PLAYLIST",
    playlistTitle: "Sample",
    items: [item(1), unavailableItem(2), { ...item(3), lowConfidence: true }],
    truncated: false,
    alsoVideoId,
  };
}

/** A single video, by `IU` called `Love`. */
export function video(): ResolveResponse {
  return {
    resolveId: "resolve-2",
    kind: "VIDEO",
    playlistTitle: null,
    items: [item(1, "IU", "Love")],
    truncated: false,
    alsoVideoId: null,
  };
}

/** A state with a response resolved into it, as the labels tests need it (no rows). */
export function resolvedState(
  kind: InputKind,
  playlistTitle: string | null = null,
  outputDir = "D:\\Music",
  albumName: string | null = null,
): UiState {
  return initialUiState({
    outputDir,
    albumName,
    resolved: { resolveId: "r1", kind, playlistTitle, items: [], truncated: false, alsoVideoId: null },
  });
}
