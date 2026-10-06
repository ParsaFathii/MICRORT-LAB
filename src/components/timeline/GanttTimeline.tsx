"use client";

/**
 * MicroRT-Lab — SVG Gantt timeline (the centerpiece instrument).
 *
 * - one lane per task (28px), sticky label column on the left
 * - adaptive time axis on top (sticky), grid lines in the body
 * - wheel zoom anchored at the pointer (0.5..80 px/tick), drag to pan
 * - click anywhere to set the time cursor (parent pauses playback)
 * - draggable accent cursor line with auto-follow while playing
 * - state-colored segments (terminated hatched, ctx segments dashed)
 * - event markers: PREEMPT / CONTEXT_SWITCH / DEADLINE_MISS / DEADLOCK /
 *   TASK_COMPLETE — clickable, feeding the Event Inspector
 *
 * Render layers (grid / segments / markers) are React.memo components fed
 * with memoized layout arrays, so the 60fps cursor updates only reconcile
 * the cursor line, not thousands of rects.
 */

import * as React from "react";

import { Maximize, ZoomIn, ZoomOut } from "lucide-react";

import { Button } from "@/components/ui/button";
import { cn } from "@/lib/utils";
import type {
  GanttSeg,
  SimulationResult,
  TaskResult,
  TaskState,
  TraceEvent,
} from "@/lib/types";

export const LANE_H = 28;
export const AXIS_H = 26;
export const LABEL_W = 132;

const PAD_R = 24;
const MIN_PX = 0.5;
const MAX_PX = 80;

/** selection contract shared with the Event Inspector (TimelineView owns it) */
export type TimelineSelection =
  | { kind: "marker"; events: TraceEvent[] }
  | { kind: "segment"; seg: GanttSeg }
  | null;

export interface GanttTimelineProps {
  result: SimulationResult;
  /** filtered task list (lane order) */
  visibleTasks: TaskResult[];
  cursorTick: number;
  playing: boolean;
  /** click on the chart background: parent pauses + seeks */
  onSeek: (t: number) => void;
  /** continuous cursor-line drag: parent seeks (already paused via onScrubStart) */
  onScrub: (t: number) => void;
  onScrubStart: () => void;
  onSelectSegment: (seg: GanttSeg) => void;
  onSelectMarker: (events: TraceEvent[]) => void;
  selectedKey: string | null;
}

const MARKER_TYPES = new Set([
  "PREEMPT",
  "CONTEXT_SWITCH",
  "DEADLINE_MISS",
  "DEADLOCK",
  "TASK_COMPLETE",
]);

const STATE_FILL: Record<TaskState, string> = {
  RUNNING: "fill-state-running",
  READY: "fill-state-ready",
  BLOCKED: "fill-state-blocked",
  WAITING: "fill-state-waiting",
  SLEEPING: "fill-state-sleeping",
  TERMINATED: "fill-state-terminated",
};

const STATE_OPACITY: Record<TaskState, number> = {
  RUNNING: 1,
  READY: 0.6,
  BLOCKED: 0.85,
  WAITING: 0.85,
  SLEEPING: 0.85,
  TERMINATED: 1,
};

const segmentKey = (seg: GanttSeg): string => `${seg.task}:${seg.t0}:${seg.t1}`;

interface MarkerItem {
  event: TraceEvent;
  lane: number | null;
}

interface SegItem {
  seg: GanttSeg;
  key: string;
  x: number;
  y: number;
  w: number;
  h: number;
  isCtx: boolean;
}

function clamp(v: number, min: number, max: number): number {
  return Math.min(max, Math.max(min, v));
}

/** choose a 1/2/5×10^k step so ~10 labels fit the visible range */
function niceStep(range: number): number {
  const safe = Math.max(1, range);
  const pow = Math.pow(10, Math.floor(Math.log10(safe)));
  const f = safe / pow;
  if (f <= 1) return pow;
  if (f <= 2) return 2 * pow;
  if (f <= 5) return 5 * pow;
  return 10 * pow;
}

/* ---- memoized render layers ------------------------------------------- */

const GridLayer = React.memo(function GridLayer({
  ticks,
  px,
  contentW,
  height,
  laneCount,
}: {
  ticks: number[];
  px: number;
  contentW: number;
  height: number;
  laneCount: number;
}) {
  return (
    <g aria-hidden>
      {ticks.map((t) => (
        <line
          key={t}
          x1={t * px}
          x2={t * px}
          y1={0}
          y2={height}
          className="stroke-line"
          strokeOpacity={0.55}
          strokeWidth={1}
        />
      ))}
      {Array.from({ length: laneCount }, (_, i) =>
        i > 0 ? (
          <line
            key={i}
            x1={0}
            x2={contentW}
            y1={i * LANE_H}
            y2={i * LANE_H}
            className="stroke-line"
            strokeOpacity={0.7}
            strokeWidth={1}
          />
        ) : null,
      )}
    </g>
  );
});

const SegmentsLayer = React.memo(function SegmentsLayer({
  items,
  selectedKey,
  hatchId,
}: {
  items: SegItem[];
  selectedKey: string | null;
  hatchId: string;
}) {
  return (
    <g>
      {items.map((item) => {
        const selected = selectedKey === item.key;
        const title = `${item.seg.task} ${item.seg.state} ${item.seg.t0}→${item.seg.t1} (${item.seg.t1 - item.seg.t0}t)`;
        return (
          <g key={item.key} data-seg={item.key} className="cursor-pointer">
            <title>{title}</title>
            {item.seg.state === "TERMINATED" ? (
              <rect
                x={item.x}
                y={item.y}
                width={item.w}
                height={item.h}
                rx={2}
                fill={`url(#${hatchId})`}
              />
            ) : (
              <>
                <rect
                  x={item.x}
                  y={item.y}
                  width={item.w}
                  height={item.h}
                  rx={2}
                  className={STATE_FILL[item.seg.state]}
                  fillOpacity={STATE_OPACITY[item.seg.state]}
                />
                {item.isCtx && (
                  <rect
                    x={item.x}
                    y={item.y}
                    width={item.w}
                    height={item.h}
                    rx={2}
                    fill="none"
                    className="stroke-ctx"
                    strokeWidth={1}
                    strokeDasharray="3 2"
                  />
                )}
              </>
            )}
            {selected && (
              <rect
                x={item.x - 1}
                y={item.y - 1}
                width={item.w + 2}
                height={item.h + 2}
                rx={2}
                fill="none"
                className="stroke-accent"
                strokeWidth={1.5}
              />
            )}
          </g>
        );
      })}
    </g>
  );
});

const MarkersLayer = React.memo(function MarkersLayer({
  markers,
  px,
  height,
  selectedKey,
}: {
  markers: MarkerItem[];
  px: number;
  height: number;
  selectedKey: string | null;
}) {
  return (
    <g>
      {markers.map(({ event, lane }) => {
        const x = event.t * px;
        const key = `marker:${event.seq}`;
        const selected = selectedKey === key;
        const cy = lane !== null ? lane * LANE_H + 8 : 6;
        return (
          <g key={key} data-marker={event.seq} className="cursor-pointer">
            <title>
              {`${event.type} t=${event.t}${event.task ? ` ${event.task}` : ""}`}
            </title>
            {event.type === "DEADLOCK" ? (
              <>
                <line
                  x1={x}
                  x2={x}
                  y1={0}
                  y2={height}
                  className="stroke-bad"
                  strokeWidth={1}
                  strokeDasharray="4 3"
                  strokeOpacity={0.85}
                />
                <polygon
                  points={`${x - 4},${6} ${x + 4},${6} ${x},${12}`}
                  className="fill-bad"
                />
              </>
            ) : event.type === "PREEMPT" ? (
              <polygon
                points={`${x},${cy - 4} ${x + 4},${cy} ${x},${cy + 4} ${x - 4},${cy}`}
                className="fill-bad"
              />
            ) : event.type === "CONTEXT_SWITCH" ? (
              <rect x={x - 3} y={cy - 3} width={6} height={6} className="fill-ctx" />
            ) : event.type === "DEADLINE_MISS" ? (
              <polygon
                points={`${x - 4},${cy - 4} ${x + 4},${cy - 4} ${x},${cy + 3}`}
                className="fill-bad"
              />
            ) : (
              <circle cx={x} cy={cy} r={3} className="fill-ok" />
            )}
            {selected && (
              <circle
                cx={x}
                cy={cy}
                r={8}
                fill="none"
                className="stroke-accent"
                strokeWidth={1.5}
              />
            )}
            {/* hit area: narrow band around the marker
                (deadlock lines keep a full-height band) */}
            <rect
              x={x - 6}
              y={event.type === "DEADLOCK" ? 0 : Math.max(0, cy - 9)}
              width={12}
              height={event.type === "DEADLOCK" ? height : 18}
              fill="transparent"
            />
          </g>
        );
      })}
    </g>
  );
});

/* ---- main component ---------------------------------------------------- */

export function GanttTimeline(props: GanttTimelineProps) {
  const {
    result,
    visibleTasks,
    cursorTick,
    playing,
    onSeek,
    onScrub,
    onScrubStart,
    onSelectSegment,
    onSelectMarker,
    selectedKey,
  } = props;

  const horizon = Math.max(0, result.simulatedUntil);
  const bodyH = Math.max(LANE_H, visibleTasks.length * LANE_H);

  const scrollRef = React.useRef<HTMLDivElement | null>(null);
  const svgRef = React.useRef<SVGSVGElement | null>(null);
  const hatchId = React.useId();

  const [px, setPx] = React.useState(8);
  const [viewW, setViewW] = React.useState(0);
  const [panning, setPanning] = React.useState(false);
  const pendingAnchor = React.useRef<{ t: number; offset: number } | null>(null);
  const fittedRef = React.useRef(false);

  const dragRef = React.useRef<{
    mode: "click" | "pan" | "cursor" | null;
    startX: number;
    startScroll: number;
    target: Element | null;
  }>({ mode: null, startX: 0, startScroll: 0, target: null });

  const contentW = Math.max(horizon * px + PAD_R, viewW, 60);
  const totalW = contentW + LABEL_W;

  /* ---- measurement + initial fit ------------------------------------- */

  React.useEffect(() => {
    const el = scrollRef.current;
    if (!el) return;
    const measure = () => {
      const w = Math.max(0, el.clientWidth - LABEL_W);
      setViewW(w);
      if (!fittedRef.current && w > 0 && horizon > 0) {
        fittedRef.current = true;
        setPx(clamp((w - PAD_R) / horizon, MIN_PX, MAX_PX));
      }
    };
    measure();
    const ro = new ResizeObserver(measure);
    ro.observe(el);
    return () => ro.disconnect();
  }, [horizon]);

  /* ---- zoom ----------------------------------------------------------- */

  const zoomAt = React.useCallback(
    (clientX: number, factor: number) => {
      const el = scrollRef.current;
      if (!el || horizon <= 0) return;
      const rect = el.getBoundingClientRect();
      const offset = clientX - rect.left;
      const t = clamp((el.scrollLeft + offset - LABEL_W) / px, 0, horizon);
      const next = clamp(px * factor, MIN_PX, MAX_PX);
      pendingAnchor.current = { t, offset };
      setPx(next);
    },
    [horizon, px],
  );

  // keep the anchored tick under the pointer after a zoom re-render
  React.useLayoutEffect(() => {
    const el = scrollRef.current;
    const anchor = pendingAnchor.current;
    if (el && anchor) {
      pendingAnchor.current = null;
      el.scrollLeft = Math.max(0, anchor.t * px + LABEL_W - anchor.offset);
    }
  }, [px]);

  React.useEffect(() => {
    const el = scrollRef.current;
    if (!el) return;
    const onWheel = (e: WheelEvent) => {
      if (e.shiftKey) return; // shift+wheel = native scroll
      e.preventDefault();
      const factor = e.deltaY < 0 || e.deltaX < 0 ? 1.15 : 1 / 1.15;
      zoomAt(e.clientX, factor);
    };
    el.addEventListener("wheel", onWheel, { passive: false });
    return () => el.removeEventListener("wheel", onWheel);
  }, [zoomAt]);

  const viewCenterClientX = React.useCallback(() => {
    const el = scrollRef.current;
    if (!el) return 0;
    return el.getBoundingClientRect().left + el.clientWidth / 2;
  }, []);

  const zoomIn = React.useCallback(
    () => zoomAt(viewCenterClientX(), 1.5),
    [zoomAt, viewCenterClientX],
  );
  const zoomOut = React.useCallback(
    () => zoomAt(viewCenterClientX(), 1 / 1.5),
    [zoomAt, viewCenterClientX],
  );

  const fit = React.useCallback(() => {
    const el = scrollRef.current;
    if (!el || horizon <= 0) return;
    setPx(clamp((el.clientWidth - LABEL_W - PAD_R) / horizon, MIN_PX, MAX_PX));
    el.scrollLeft = 0;
  }, [horizon]);

  /* ---- auto-follow while playing -------------------------------------- */

  React.useEffect(() => {
    if (!playing) return;
    const el = scrollRef.current;
    if (!el) return;
    const x = cursorTick * px;
    const left = el.scrollLeft;
    const view = el.clientWidth - LABEL_W;
    if (view > 0 && (x < left + view * 0.05 || x > left + view * 0.9)) {
      el.scrollLeft = Math.max(0, x - view * 0.6);
    }
  }, [cursorTick, playing, px]);

  /* ---- layout (memoized: recomputed only on data/zoom/filter changes) - */

  const laneIndex = React.useMemo(() => {
    const map = new Map<string, number>();
    visibleTasks.forEach((task, i) => map.set(task.id, i));
    return map;
  }, [visibleTasks]);

  const segItems = React.useMemo<SegItem[]>(() => {
    const items: SegItem[] = [];
    for (const seg of result.gantt) {
      const lane = laneIndex.get(seg.task);
      if (lane === undefined) continue;
      items.push({
        seg,
        key: segmentKey(seg),
        x: seg.t0 * px + 0.5,
        y: lane * LANE_H + 4,
        w: Math.max(1, (seg.t1 - seg.t0) * px - 1),
        h: LANE_H - 8,
        isCtx: seg.note === "ctx",
      });
    }
    return items;
  }, [result.gantt, laneIndex, px]);

  const markers = React.useMemo<MarkerItem[]>(() => {
    const items: MarkerItem[] = [];
    for (const ev of result.trace) {
      if (!MARKER_TYPES.has(ev.type)) continue;
      if (ev.task === null) {
        items.push({ event: ev, lane: null });
        continue;
      }
      const lane = laneIndex.get(ev.task);
      if (lane === undefined) continue;
      items.push({ event: ev, lane });
    }
    return items;
  }, [result.trace, laneIndex]);

  const segByKey = React.useMemo(() => {
    const map = new Map<string, GanttSeg>();
    for (const item of segItems) map.set(item.key, item.seg);
    return map;
  }, [segItems]);

  const tickStep = React.useMemo(() => {
    const visibleTicks = viewW > 0 ? viewW / px : horizon;
    const step = niceStep(visibleTicks / 10);
    return Math.max(1, Math.min(step, Math.max(1, horizon)));
  }, [viewW, px, horizon]);

  const ticks = React.useMemo(() => {
    const list: number[] = [];
    for (let t = 0; t <= horizon; t += tickStep) list.push(t);
    const last = list.length > 0 ? list[list.length - 1] : -1;
    if (horizon > 0 && last !== horizon && horizon - last >= tickStep / 2) {
      list.push(horizon);
    }
    return list;
  }, [horizon, tickStep]);

  /* ---- pointer interaction: pan / click-select / cursor drag ---------- */

  const tickAtClientX = React.useCallback(
    (clientX: number): number => {
      const el = scrollRef.current;
      if (!el) return 0;
      const rect = el.getBoundingClientRect();
      return clamp((el.scrollLeft + clientX - rect.left - LABEL_W) / px, 0, horizon);
    },
    [px, horizon],
  );

  const onPointerDown = (e: React.PointerEvent<SVGSVGElement>) => {
    if (e.button !== 0) return;
    const el = scrollRef.current;
    const svg = svgRef.current;
    if (!el || !svg) return;
    const target = e.target as Element;
    const mode = target.closest("[data-cursordrag]") ? "cursor" : "click";
    if (mode === "cursor") onScrubStart();
    dragRef.current = { mode, startX: e.clientX, startScroll: el.scrollLeft, target };
    try {
      svg.setPointerCapture(e.pointerId);
    } catch {
      // capture is best-effort
    }
  };

  const onPointerMove = (e: React.PointerEvent<SVGSVGElement>) => {
    const drag = dragRef.current;
    const el = scrollRef.current;
    if (!drag || !el) return;
    if (drag.mode === "cursor") {
      onScrub(tickAtClientX(e.clientX));
      return;
    }
    const dx = e.clientX - drag.startX;
    if (drag.mode === "click" && Math.abs(dx) > 4) {
      drag.mode = "pan";
      setPanning(true);
    }
    if (drag.mode === "pan") {
      el.scrollLeft = Math.max(0, drag.startScroll - dx);
    }
  };

  const onPointerUp = (e: React.PointerEvent<SVGSVGElement>) => {
    const drag = dragRef.current;
    const svg = svgRef.current;
    if (svg && svg.hasPointerCapture(e.pointerId)) {
      svg.releasePointerCapture(e.pointerId);
    }
    if (!drag) return;
    if (drag.mode === "click") {
      const target = drag.target;
      const markerEl = target?.closest("[data-marker]") ?? null;
      if (markerEl) {
        const seq = Number(markerEl.getAttribute("data-marker"));
        const marker = markers.find((m) => m.event.seq === seq);
        if (marker) {
          const ev = marker.event;
          const events = result.trace.filter((c) => c.t === ev.t && c.task === ev.task);
          onSelectMarker(events.length > 0 ? events : [ev]);
          dragRef.current = { mode: null, startX: 0, startScroll: 0, target: null };
          setPanning(false);
          return;
        }
      }
      const segEl = target?.closest("[data-seg]") ?? null;
      if (segEl) {
        const seg = segByKey.get(segEl.getAttribute("data-seg") ?? "");
        if (seg) {
          onSelectSegment(seg);
          dragRef.current = { mode: null, startX: 0, startScroll: 0, target: null };
          setPanning(false);
          return;
        }
      }
      onSeek(tickAtClientX(e.clientX));
    }
    dragRef.current = { mode: null, startX: 0, startScroll: 0, target: null };
    setPanning(false);
  };

  const onPointerCancel = () => {
    dragRef.current = { mode: null, startX: 0, startScroll: 0, target: null };
    setPanning(false);
  };

  /* ---- render ---------------------------------------------------------- */

  const cursorX = clamp(cursorTick, 0, horizon) * px;
  const cursorLabel = String(Math.floor(clamp(cursorTick, 0, horizon))).padStart(
    Math.max(2, String(horizon).length),
    "0",
  );

  return (
    <div className="relative flex h-full min-h-0 flex-col bg-bg">
      {/* zoom cluster */}
      <div className="absolute right-2 top-8 z-40 flex items-center gap-0.5 rounded-md border border-line bg-panel/95 p-0.5 backdrop-blur-sm">
        <span className="px-1.5 font-mono text-[9px] text-ink-dim">
          {px >= 10 ? px.toFixed(0) : px.toFixed(1)}px/t
        </span>
        <Button
          type="button"
          variant="ghost"
          size="icon"
          className="h-7 w-7 cursor-pointer rounded-sm text-ink-dim hover:bg-accent/10 hover:text-ink"
          onClick={zoomOut}
          aria-label="Zoom out"
        >
          <ZoomOut className="h-3.5 w-3.5" />
        </Button>
        <Button
          type="button"
          variant="ghost"
          size="icon"
          className="h-7 w-7 cursor-pointer rounded-sm text-ink-dim hover:bg-accent/10 hover:text-ink"
          onClick={fit}
          aria-label="Fit timeline to view"
        >
          <Maximize className="h-3.5 w-3.5" />
        </Button>
        <Button
          type="button"
          variant="ghost"
          size="icon"
          className="h-7 w-7 cursor-pointer rounded-sm text-ink-dim hover:bg-accent/10 hover:text-ink"
          onClick={zoomIn}
          aria-label="Zoom in"
        >
          <ZoomIn className="h-3.5 w-3.5" />
        </Button>
      </div>

      <div
        ref={scrollRef}
        className="min-h-0 flex-1 overflow-auto"
        style={{ cursor: panning ? "grabbing" : undefined }}
      >
        <div style={{ width: totalW, minWidth: "100%" }}>
          {/* time axis (sticky top) */}
          <div className="sticky top-0 z-20 flex bg-panel">
            <div
              className="sticky left-0 z-30 flex shrink-0 items-center justify-between border-b border-r border-line bg-panel px-2"
              style={{ width: LABEL_W, height: AXIS_H }}
            >
              <span className="font-mono text-[9px] tracking-widest text-ink-dim">
                TASK
              </span>
              <span className="font-mono text-[9px] text-ink-dim">TICK</span>
            </div>
            <svg
              width={contentW}
              height={AXIS_H}
              className="block border-b border-line bg-panel"
              aria-hidden
            >
              {ticks.map((t) => (
                <g key={t}>
                  <line
                    x1={t * px}
                    x2={t * px}
                    y1={AXIS_H - 6}
                    y2={AXIS_H}
                    className="stroke-ink-dim"
                    strokeWidth={1}
                  />
                  <text
                    x={t * px + 3}
                    y={AXIS_H - 9}
                    className="fill-ink-dim font-mono"
                    fontSize={9}
                  >
                    {t}
                  </text>
                </g>
              ))}
              {horizon > 0 && (
                <g>
                  <polygon
                    points={`${cursorX - 4},${AXIS_H - 8} ${cursorX + 4},${AXIS_H - 8} ${cursorX},${AXIS_H - 1}`}
                    className="fill-accent"
                  />
                  <text
                    x={cursorX + 6}
                    y={AXIS_H - 9}
                    className="fill-accent font-mono"
                    fontSize={9}
                  >
                    t={cursorLabel}
                  </text>
                </g>
              )}
            </svg>
          </div>

          {/* body: labels + lanes */}
          <div className="flex">
            <div
              className="sticky left-0 z-10 shrink-0 border-r border-line bg-panel"
              style={{ width: LABEL_W }}
            >
              {visibleTasks.map((task) => (
                <div
                  key={task.id}
                  style={{ height: LANE_H }}
                  className="flex items-center gap-1.5 border-b border-line/40 px-2"
                >
                  <span className="truncate font-mono text-xs text-ink" title={task.name}>
                    {task.id}
                  </span>
                  <span className="ml-auto shrink-0 rounded-sm border border-line px-1 font-mono text-[9px] leading-4 text-ink-dim">
                    p{task.priority}
                  </span>
                </div>
              ))}
            </div>

            {visibleTasks.length === 0 ? (
              <div className="flex h-28 items-center justify-center px-6">
                <p className="font-mono text-xs text-ink-dim">
                  all task lanes hidden — re-enable a chip above
                </p>
              </div>
            ) : (
              <svg
                ref={svgRef}
                width={contentW}
                height={bodyH}
                className={cn(
                  "block touch-pan-y select-none",
                  panning ? "cursor-grabbing" : "cursor-grab",
                )}
                role="img"
                aria-label={`Gantt timeline: ${visibleTasks.length} tasks over ${horizon} ticks. Drag to pan, scroll to zoom, click to set the time cursor.`}
                onPointerDown={onPointerDown}
                onPointerMove={onPointerMove}
                onPointerUp={onPointerUp}
                onPointerCancel={onPointerCancel}
              >
                <defs>
                  <pattern
                    id={hatchId}
                    width="6"
                    height="6"
                    patternUnits="userSpaceOnUse"
                    patternTransform="rotate(45)"
                  >
                    <rect
                      width="6"
                      height="6"
                      className="fill-state-terminated"
                      fillOpacity={0.55}
                    />
                    <line
                      x1="0"
                      y1="0"
                      x2="0"
                      y2="6"
                      className="stroke-ink-dim"
                      strokeWidth={1}
                      strokeOpacity={0.7}
                    />
                  </pattern>
                </defs>

                <GridLayer
                  ticks={ticks}
                  px={px}
                  contentW={contentW}
                  height={bodyH}
                  laneCount={visibleTasks.length}
                />
                <SegmentsLayer
                  items={segItems}
                  selectedKey={selectedKey}
                  hatchId={hatchId}
                />
                <MarkersLayer
                  markers={markers}
                  px={px}
                  height={bodyH}
                  selectedKey={selectedKey}
                />

                {/* time cursor (updates every frame — kept out of memo layers) */}
                {horizon > 0 && (
                  <g>
                    <line
                      x1={cursorX}
                      x2={cursorX}
                      y1={0}
                      y2={bodyH}
                      className="stroke-accent"
                      strokeWidth={1.5}
                      pointerEvents="none"
                    />
                    <rect
                      data-cursordrag="1"
                      x={cursorX - 6}
                      y={0}
                      width={12}
                      height={bodyH}
                      fill="transparent"
                      className="cursor-ew-resize"
                    />
                  </g>
                )}
              </svg>
            )}
          </div>
        </div>
      </div>
    </div>
  );
}
