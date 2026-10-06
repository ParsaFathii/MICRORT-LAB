/**
 * MicroRT-Lab — global playback clock.
 *
 * A single requestAnimationFrame loop advances the workstation cursor via
 * store.advancePlayback(dtMs) while playback is active. The loop is a module
 * singleton with reference counting, so mounting this hook in several views
 * (or the shell) never double-advances the cursor.
 *
 * Mounted once by WorkstationShell — views (timeline/live/…) read the cursor
 * from the store; they do not need their own driver.
 */

import { useEffect } from "react";

import { useWorkstation } from "@/store/workstation";

let listeners = 0;
let running = false;
let rafId = 0;
let lastFrame = 0;

function frame(now: number): void {
  rafId = requestAnimationFrame(frame);
  const dt = now - lastFrame;
  lastFrame = now;
  if (dt > 0) {
    useWorkstation.getState().advancePlayback(dt);
  }
}

export function usePlaybackClock(): void {
  const playing = useWorkstation((s) => s.playback.playing);

  useEffect(() => {
    if (!playing) return;
    listeners += 1;
    if (!running) {
      running = true;
      lastFrame = performance.now();
      rafId = requestAnimationFrame(frame);
    }
    return () => {
      listeners -= 1;
      if (listeners <= 0 && running) {
        running = false;
        listeners = 0;
        cancelAnimationFrame(rafId);
      }
    };
  }, [playing]);
}
