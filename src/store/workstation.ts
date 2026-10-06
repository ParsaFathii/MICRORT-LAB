/**
 * MicroRT-Lab — workstation UI state (single global zustand store).
 *
 * Server data (experiments, simulations, results) lives in TanStack Query
 * (see src/hooks/useSimulation.ts); this store holds only view/playback state.
 */

import { create } from "zustand";

export const VIEW_IDS = [
  "workbench",
  "timeline",
  "live",
  "trace",
  "resources",
  "deadlock",
  "memory",
  "metrics",
  "compare",
  "reports",
  "docs",
] as const;

export type ViewId = (typeof VIEW_IDS)[number];

export const PLAYBACK_SPEEDS = [1, 2, 5, 10, 25, 50] as const;

export interface PlaybackState {
  playing: boolean;
  /** ticks per second */
  speed: number;
  cursorTick: number;
}

export interface FiltersState {
  /** HIDDEN task ids (exclusion list — the store does not know every task id); empty = all visible */
  tasks: string[];
  /** enabled trace event types; empty = all types */
  eventTypes: string[];
}

export interface WorkstationState {
  activeView: ViewId;
  simId: string | null;
  /** last simulated tick of the loaded simulation (playback clamp bound) */
  horizon: number;
  playback: PlaybackState;
  filters: FiltersState;

  setActiveView: (view: ViewId) => void;
  /** Load a simulation; resets playback (cursor 0, paused) and task filters. */
  setSimulation: (id: string | null) => void;
  /** Keep the playback clamp bound in sync with the loaded result doc. */
  setHorizon: (tick: number) => void;

  play: () => void;
  pause: () => void;
  togglePlay: () => void;
  setSpeed: (speed: number) => void;
  setCursor: (tick: number) => void;
  stepForward: () => void;
  stepBackward: () => void;
  /** rAF driver: advance the cursor by dtMs at the current speed, clamped. */
  advancePlayback: (dtMs: number) => void;

  toggleTaskFilter: (taskId: string) => void;
  setEventTypeFilters: (types: string[]) => void;
}

const clamp = (value: number, min: number, max: number): number =>
  Math.min(max, Math.max(min, value));

export const useWorkstation = create<WorkstationState>((set, get) => ({
  activeView: "workbench",
  simId: null,
  horizon: 0,
  playback: { playing: false, speed: 5, cursorTick: 0 },
  filters: { tasks: [], eventTypes: [] },

  setActiveView: (view) => set({ activeView: view }),

  setSimulation: (id) =>
    set({
      simId: id,
      playback: { playing: false, speed: get().playback.speed, cursorTick: 0 },
      filters: { tasks: [], eventTypes: get().filters.eventTypes },
    }),

  setHorizon: (tick) =>
    set((state) => ({
      horizon: Math.max(0, Math.round(tick)),
      playback: {
        ...state.playback,
        cursorTick: clamp(state.playback.cursorTick, 0, Math.max(0, Math.round(tick))),
      },
    })),

  play: () => set((s) => ({ playback: { ...s.playback, playing: true } })),
  pause: () => set((s) => ({ playback: { ...s.playback, playing: false } })),
  togglePlay: () => set((s) => ({ playback: { ...s.playback, playing: !s.playback.playing } })),

  setSpeed: (speed) =>
    set((s) => ({
      playback: { ...s.playback, speed: clamp(speed, 1, 100) },
    })),

  setCursor: (tick) =>
    set((s) => ({
      playback: { ...s.playback, cursorTick: clamp(Math.round(tick), 0, s.horizon) },
    })),

  stepForward: () =>
    set((s) => ({
      playback: { ...s.playback, cursorTick: clamp(s.playback.cursorTick + 1, 0, s.horizon) },
    })),

  stepBackward: () =>
    set((s) => ({
      playback: { ...s.playback, cursorTick: clamp(s.playback.cursorTick - 1, 0, s.horizon) },
    })),

  advancePlayback: (dtMs) =>
    set((s) => {
      if (!s.playback.playing || s.horizon <= 0) return s;
      const next = clamp(
        s.playback.cursorTick + (s.playback.speed * dtMs) / 1000,
        0,
        s.horizon,
      );
      if (next === s.playback.cursorTick) return s;
      return { playback: { ...s.playback, cursorTick: next } };
    }),

  /** Toggle task visibility: hidden ids accumulate in filters.tasks. */
  toggleTaskFilter: (taskId) =>
    set((s) => {
      const hidden = s.filters.tasks.includes(taskId);
      const tasks = hidden
        ? s.filters.tasks.filter((t) => t !== taskId)
        : [...s.filters.tasks, taskId];
      return { filters: { ...s.filters, tasks } };
    }),

  setEventTypeFilters: (types) => set((s) => ({ filters: { ...s.filters, eventTypes: types } })),
}));
