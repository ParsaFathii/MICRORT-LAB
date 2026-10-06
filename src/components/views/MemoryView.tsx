"use client";

/**
 * MicroRT-Lab — Memory view (07): memory layout over time.
 *
 * The byte-address map replays memory.events up to the playback cursor, so
 * scrubbing/playing visibly carves and releases memory. Below: fragmentation
 * series (used + frag), failure and leak tables, and a block inspector.
 */

import * as React from "react";
import {
  Area,
  AreaChart,
  CartesianGrid,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis,
} from "recharts";

import { cn } from "@/lib/utils";
import type { MemoryBlock, MemoryEvent, MemoryResult, SimulationResult } from "@/lib/types";
import { useWorkstation } from "@/store/workstation";
import {
  EmptyPanel,
  SectionPanel,
  type SimDoc,
  SimGate,
  StatTile,
  taskHueMap,
  useTick,
  ViewHeader,
} from "@/components/views/shared";

/* ---------------------------------------------------------------------- */
/* layout replay                                                           */
/* ---------------------------------------------------------------------- */

interface LiveBlock {
  offset: number;
  size: number;
  owner: string;
  tag: string;
}

/**
 * Replay memory events with t <= tick. Region blocks use the event's offset;
 * pool blocks get synthetic slot offsets (blockSize each, lowest free slot).
 */
export function replayLayout(
  events: readonly MemoryEvent[],
  t: number,
  model: string,
  blockSize: number,
): LiveBlock[] {
  const blocks: LiveBlock[] = [];
  const freeSlots: number[] = [];
  let nextSlot = 0;
  for (const e of events) {
    if (e.t > t) break;
    if (e.op === "alloc" && e.result === "ok") {
      if (model === "pool") {
        const slot = freeSlots.length > 0 ? (freeSlots.shift() as number) : nextSlot++;
        blocks.push({
          offset: slot * blockSize,
          size: blockSize,
          owner: e.task,
          tag: e.tag ?? `slot-${slot}`,
        });
      } else {
        blocks.push({
          offset: e.offset ?? 0,
          size: e.size ?? 0,
          owner: e.task,
          tag: e.tag ?? "untagged",
        });
      }
    } else if (e.op === "free" && e.result === "ok") {
      const idx = blocks.findIndex((b) => b.tag === (e.tag ?? "untagged"));
      if (idx >= 0) {
        const [removed] = blocks.splice(idx, 1);
        if (model === "pool") freeSlots.push(Math.floor(removed.offset / blockSize));
      }
    }
  }
  blocks.sort((a, b) => a.offset - b.offset);
  return blocks;
}

/* ---------------------------------------------------------------------- */
/* memory map SVG                                                          */
/* ---------------------------------------------------------------------- */

const MAP_W = 1000;
const MAP_H = 64;

function MemoryMap({
  total,
  blocks,
  hues,
  selectedTag,
  onSelect,
}: {
  total: number;
  blocks: LiveBlock[];
  hues: Map<string, string>;
  selectedTag: string | null;
  onSelect: (block: LiveBlock) => void;
}) {
  const scale = (bytes: number): number => (bytes / Math.max(1, total)) * MAP_W;
  return (
    <svg
      viewBox={`0 0 ${MAP_W} ${MAP_H}`}
      preserveAspectRatio="none"
      className="block h-16 w-full cursor-crosshair"
      role="img"
      aria-label="Memory map at the playback cursor"
    >
      <defs>
        <pattern id="mem-free-hatch" width="6" height="6" patternTransform="rotate(45)" patternUnits="userSpaceOnUse">
          <rect width="6" height="6" fill="var(--bg)" />
          <line x1="0" y1="0" x2="0" y2="6" stroke="var(--line)" strokeWidth="2.5" />
        </pattern>
      </defs>
      {/* free space base */}
      <rect x="0" y="0" width={MAP_W} height={MAP_H} fill="url(#mem-free-hatch)" stroke="var(--line)" />
      {blocks.map((b, i) => {
        const w = Math.max(1, scale(b.size));
        const color = hues.get(b.owner) ?? "#8a8271";
        const selected = selectedTag !== null && b.tag === selectedTag;
        return (
          <rect
            key={`${b.tag}-${i}`}
            x={scale(b.offset)}
            y={selected ? 1 : 3}
            width={w}
            height={selected ? MAP_H - 2 : MAP_H - 6}
            fill={color}
            fillOpacity={selected ? 0.95 : 0.8}
            stroke={selected ? "var(--accent)" : color}
            strokeWidth={selected ? 2 : 1}
            tabIndex={0}
            role="button"
            aria-label={`Block ${b.tag}: ${b.size} B at offset ${b.offset}, owner ${b.owner}`}
            onClick={() => onSelect(b)}
            onKeyDown={(e) => {
              if (e.key === "Enter" || e.key === " ") {
                e.preventDefault();
                onSelect(b);
              }
            }}
            className="focus-visible:outline-none"
          >
            <title>{`${b.tag} · ${b.owner} · ${b.size} B @ ${b.offset}`}</title>
          </rect>
        );
      })}
    </svg>
  );
}

/* ---------------------------------------------------------------------- */
/* tables                                                                  */
/* ---------------------------------------------------------------------- */

function MemoryEventTable({ events }: { events: MemoryEvent[] }) {
  return (
    <div className="max-h-56 overflow-y-auto">
      <table className="w-full border-collapse font-mono text-[11px]">
        <thead>
          <tr className="text-left">
            {["T", "OP", "TASK", "TAG", "SIZE", "RESULT"].map((h) => (
              <th
                key={h}
                className="border-b border-line px-2 py-1 text-[9px] font-normal tracking-widest text-ink-dim"
              >
                {h}
              </th>
            ))}
          </tr>
        </thead>
        <tbody>
          {events.map((e, i) => (
            <tr key={i} className="border-b border-line/40">
              <td className="px-2 py-1 text-accent tabular-nums">{e.t}</td>
              <td className={cn("px-2 py-1", e.op === "alloc" ? "text-ink" : "text-ink-dim")}>{e.op}</td>
              <td className="px-2 py-1 text-ink">{e.task}</td>
              <td className="px-2 py-1 text-ink">{e.tag ?? "—"}</td>
              <td className="px-2 py-1 text-right text-ink tabular-nums">{e.size ?? "—"}</td>
              <td className={cn("px-2 py-1", e.result === "ok" ? "text-ok" : "text-bad")}>
                {e.result ?? "—"}
              </td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}

function LeakTable({
  leaks,
  hues,
  onSelect,
}: {
  leaks: MemoryBlock[];
  hues: Map<string, string>;
  onSelect: (block: MemoryBlock) => void;
}) {
  return (
    <div className="max-h-56 overflow-y-auto">
      <table className="w-full border-collapse font-mono text-[11px]">
        <thead>
          <tr className="text-left">
            {["OFFSET", "SIZE", "OWNER", "TAG"].map((h) => (
              <th
                key={h}
                className="border-b border-line px-2 py-1 text-[9px] font-normal tracking-widest text-ink-dim"
              >
                {h}
              </th>
            ))}
          </tr>
        </thead>
        <tbody>
          {leaks.map((b, i) => (
            <tr
              key={i}
              onClick={() => onSelect(b)}
              tabIndex={0}
              onKeyDown={(e) => {
                if (e.key === "Enter") onSelect(b);
              }}
              className="cursor-pointer border-b border-line/40 hover:bg-accent/5 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-inset focus-visible:ring-accent"
              aria-label={`Leaked block ${b.tag}, ${b.size} B`}
            >
              <td className="px-2 py-1 text-ink-dim tabular-nums">{b.offset}</td>
              <td className="px-2 py-1 text-right text-ink tabular-nums">{b.size}</td>
              <td className="px-2 py-1">
                <span className="flex items-center gap-1.5 text-ink">
                  <span
                    aria-hidden
                    className="h-2 w-2 rounded-sm"
                    style={{ background: b.owner ? hues.get(b.owner) ?? "#8a8271" : "#8a8271" }}
                  />
                  {b.owner ?? "—"}
                </span>
              </td>
              <td className="px-2 py-1 text-ink">{b.tag ?? "—"}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}

/* ---------------------------------------------------------------------- */
/* view                                                                    */
/* ---------------------------------------------------------------------- */

export function MemoryView() {
  return <SimGate render={(doc: SimDoc) => <MemoryBody key={doc.simId} result={doc.result} />} />;
}

function MemoryBody({ result }: { result: SimulationResult }) {
  const horizon = result.simulatedUntil;
  const tick = useTick(horizon);

  const memory: MemoryResult | null = result.memory;
  const hues = React.useMemo(() => taskHueMap(result.tasks), [result.tasks]);

  const [selected, setSelected] = React.useState<{ tag: string; block: LiveBlock | MemoryBlock } | null>(
    null,
  );

  const blockSize = result.config.memory?.blockSize ?? 16;
  const liveBlocks = React.useMemo(
    () =>
      memory
        ? replayLayout(memory.events, tick, memory.model, blockSize)
        : [],
    [memory, tick, blockSize],
  );

  const usedBytes = liveBlocks.reduce((sum, b) => sum + b.size, 0);

  if (!memory) {
    return (
      <div className="flex h-full min-h-0 flex-col">
        <ViewHeader num="07" label="Memory" hint="memory model over time" />
        <div className="p-4">
          <EmptyPanel
            label="MEMORY MODEL DISABLED"
            hint="This configuration has no memory block — add model region|pool to explore allocation."
          />
        </div>
      </div>
    );
  }

  const atEnd = tick >= horizon;
  const fragData = memory.fragSeries;

  return (
    <div className="flex h-full min-h-0 flex-col">
      <ViewHeader
        num="07"
        label="Memory"
        hint="scrub the playback cursor — allocation and free carve the map live"
      >
        <span className="font-mono text-[10px] text-ink-dim">
          t={tick}
          {atEnd ? " · FINAL" : ""}
        </span>
        <span className="font-mono text-[10px] text-accent tabular-nums">
          USED {usedBytes}/{memory.total} B
        </span>
      </ViewHeader>

      <div className="min-h-0 flex-1 space-y-3 overflow-y-auto p-4">
        {/* stat tiles */}
        <div className="grid grid-cols-2 gap-2 sm:grid-cols-4 xl:grid-cols-7">
          <StatTile label="model" value={memory.model} />
          <StatTile label="policy" value={memory.policy ?? "—"} />
          <StatTile label="total" value={`${memory.total} B`} />
          <StatTile label="peak usage" value={`${memory.peakUsage} B`} tone="accent" />
          <StatTile
            label="frag max"
            value={result.metrics.fragmentationMax !== null ? `${result.metrics.fragmentationMax} B` : "—"}
          />
          <StatTile
            label="failures"
            value={memory.failures.length}
            tone={memory.failures.length > 0 ? "bad" : "ink"}
          />
          <StatTile
            label="leaks"
            value={memory.leaks.length}
            tone={memory.leaks.length > 0 ? "bad" : "ink"}
          />
        </div>

        {/* memory map */}
        <SectionPanel
          title="MEMORY MAP · AT CURSOR"
          right={
            <span className="font-mono text-[9px] text-ink-dim">
              click a block to inspect · hatched = free
            </span>
          }
        >
          <MemoryMap
            total={memory.total}
            blocks={liveBlocks}
            hues={hues}
            selectedTag={selected?.tag ?? null}
            onSelect={(b) => setSelected({ tag: b.tag, block: b })}
          />
          <div className="mt-1 flex justify-between font-mono text-[9px] text-ink-dim">
            <span>0 B</span>
            <span>{Math.round(memory.total / 2)} B</span>
            <span>{memory.total} B</span>
          </div>
          {selected && (
            <div className="mt-3 rounded-md border border-line bg-bg p-3">
              <div className="flex flex-wrap items-center gap-x-4 gap-y-1 font-mono text-[11px]">
                <span className="text-accent">{selected.tag}</span>
                <span className="text-ink-dim">
                  owner <span className="text-ink">{ownerOf(selected.block)}</span>
                </span>
                <span className="text-ink-dim">
                  size <span className="text-ink">{sizeOf(selected.block)} B</span>
                </span>
                <span className="text-ink-dim">
                  offset <span className="text-ink">{offsetOf(selected.block)}</span>
                </span>
              </div>
              <p className="mt-2 mb-1 font-mono text-[9px] tracking-widest text-ink-dim">
                EVENTS FOR THIS TAG
              </p>
              <MemoryEventTable events={memory.events.filter((e) => e.tag === selected.tag)} />
            </div>
          )}
        </SectionPanel>

        {/* fragmentation series */}
        <SectionPanel title="USED BYTES + FRAGMENTATION OVER EVENT TIME">
          <div className="h-48">
            <ResponsiveContainer width="100%" height="100%">
              <AreaChart data={fragData} margin={{ top: 8, right: 12, bottom: 4, left: 0 }}>
                <defs>
                  <linearGradient id="mem-used-fill" x1="0" y1="0" x2="0" y2="1">
                    <stop offset="0%" stopColor="var(--accent)" stopOpacity={0.35} />
                    <stop offset="100%" stopColor="var(--accent)" stopOpacity={0.05} />
                  </linearGradient>
                  <linearGradient id="mem-frag-fill" x1="0" y1="0" x2="0" y2="1">
                    <stop offset="0%" stopColor="var(--bad)" stopOpacity={0.3} />
                    <stop offset="100%" stopColor="var(--bad)" stopOpacity={0.03} />
                  </linearGradient>
                </defs>
                <CartesianGrid stroke="var(--line)" strokeDasharray="2 4" />
                <XAxis
                  dataKey="t"
                  type="number"
                  domain={["dataMin", "dataMax"]}
                  tick={{ fill: "var(--ink-dim)", fontSize: 9, fontFamily: "var(--font-mono)" }}
                  stroke="var(--line)"
                  tickLine={false}
                />
                <YAxis
                  tick={{ fill: "var(--ink-dim)", fontSize: 9, fontFamily: "var(--font-mono)" }}
                  stroke="var(--line)"
                  tickLine={false}
                  width={44}
                />
                <Tooltip
                  contentStyle={{
                    background: "var(--panel)",
                    border: "1px solid var(--line)",
                    borderRadius: 6,
                    fontSize: 10,
                    fontFamily: "var(--font-mono)",
                    color: "var(--ink)",
                  }}
                  labelFormatter={(t) => `t=${String(t)}`}
                />
                <Area
                  type="stepAfter"
                  dataKey="used"
                  name="used B"
                  stroke="var(--accent)"
                  strokeWidth={1.5}
                  fill="url(#mem-used-fill)"
                  isAnimationActive={false}
                />
                <Area
                  type="stepAfter"
                  dataKey="frag"
                  name="frag B"
                  stroke="var(--bad)"
                  strokeWidth={1.5}
                  fill="url(#mem-frag-fill)"
                  isAnimationActive={false}
                />
              </AreaChart>
            </ResponsiveContainer>
          </div>
        </SectionPanel>

        {/* failures + leaks */}
        <div className="grid grid-cols-1 gap-3 xl:grid-cols-2">
          <SectionPanel title={`ALLOC FAILURES · ${memory.failures.length}`}>
            {memory.failures.length === 0 ? (
              <p className="py-2 text-center font-mono text-[10px] text-ink-dim">
                NO ALLOCATION FAILURES
              </p>
            ) : (
              <MemoryEventTable events={memory.failures} />
            )}
          </SectionPanel>
          <SectionPanel title={`LEAKED BLOCKS · ${memory.leaks.length}`}>
            {memory.leaks.length === 0 ? (
              <p className="py-2 text-center font-mono text-[10px] text-ink-dim">
                NO LEAKS — EVERY ALLOCATION WAS FREED
              </p>
            ) : (
              <LeakTable
                leaks={memory.leaks}
                hues={hues}
                onSelect={(b) => setSelected({ tag: b.tag ?? "untagged", block: b })}
              />
            )}
          </SectionPanel>
        </div>
      </div>
    </div>
  );
}

function ownerOf(block: LiveBlock | MemoryBlock): string {
  return block.owner ?? "—";
}
function sizeOf(block: LiveBlock | MemoryBlock): number {
  return block.size;
}
function offsetOf(block: LiveBlock | MemoryBlock): number {
  return block.offset;
}
