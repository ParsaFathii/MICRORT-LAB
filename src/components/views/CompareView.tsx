"use client";

/**
 * MicroRT-Lab — Compare view (09): scheduler comparison workbench.
 *
 * Left: one-click presets, a variant creator (base = experiment or pasted
 * config JSON; variants override scheduler/quantum/memory policy/aging) and
 * the comparison history. Right: metric × variant matrix (best/worst
 * highlighted), grouped bar chart for the selected metric, deltas vs the
 * first variant, starved tasks, and a markdown report drawer with download +
 * print.
 */

import * as React from "react";
import { Loader2, Play, Plus, Printer, Trash2 } from "lucide-react";
import { toast } from "sonner";
import {
  Bar,
  BarChart,
  CartesianGrid,
  Cell,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis,
} from "recharts";

import { Button } from "@/components/ui/button";
import { Checkbox } from "@/components/ui/checkbox";
import { Input } from "@/components/ui/input";
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from "@/components/ui/select";
import { Sheet, SheetContent, SheetHeader, SheetTitle } from "@/components/ui/sheet";
import { Skeleton } from "@/components/ui/skeleton";
import { Tabs, TabsList, TabsTrigger } from "@/components/ui/tabs";
import { Textarea } from "@/components/ui/textarea";
import { cn } from "@/lib/utils";
import type {
  ComparisonRecord,
  ComparisonVariantInput,
  CreateComparisonPayload,
} from "@/lib/types";
import { formatApiError } from "@/lib/api";
import { useExperiments } from "@/hooks/useSimulation";
import { useComparison, useComparisonReport, useComparisons, useCreateComparison } from "@/hooks/useComparisons";
import { MiniMarkdown } from "@/components/views/reports/MiniMarkdown";
import {
  downloadBlob,
  EmptyPanel,
  SectionPanel,
  ViewHeader,
} from "@/components/views/shared";

/* ---------------------------------------------------------------------- */
/* constants                                                               */
/* ---------------------------------------------------------------------- */

const SCHEDULERS = ["fifo", "rr", "priority", "priority_p", "sjf", "srtf", "rm", "edf"] as const;
const MEMORY_POLICIES = ["first_fit", "best_fit", "worst_fit"] as const;
const METRIC_ORDER = [
  "avgWaiting",
  "avgTurnaround",
  "avgResponse",
  "throughput",
  "cpuUtilization",
  "ctxSwitches",
  "preemptions",
  "deadlineMisses",
  "completedJobs",
] as const;
const HIGHER_IS_BETTER = new Set(["throughput", "cpuUtilization", "completedJobs"]);

const PRINT_STYLE = `
@media print {
  body * { visibility: hidden !important; }
  .print-target, .print-target * { visibility: visible !important; }
  .print-target {
    position: absolute !important;
    left: 0 !important;
    top: 0 !important;
    width: 100% !important;
    max-height: none !important;
    overflow: visible !important;
    border: none !important;
  }
}`;

interface Preset {
  id: string;
  label: string;
  hint: string;
  payload: CreateComparisonPayload;
}

const PRESETS: Preset[] = [
  {
    id: "rr-quantum",
    label: "RR QUANTUM 2/4/16",
    hint: "same workload, three round-robin quanta",
    payload: {
      name: "rr-quantum-2-4-16",
      experimentId: "rr-quantum-comparison",
      variants: [
        { label: "q=2", scheduler: { type: "rr", quantum: 2 } },
        { label: "q=4", scheduler: { type: "rr", quantum: 4 } },
        { label: "q=16", scheduler: { type: "rr", quantum: 16 } },
      ],
    },
  },
  {
    id: "aging",
    label: "AGING RESCUE",
    hint: "priority starvation: baseline vs aging interval 4 cap 22",
    payload: {
      name: "aging-rescue",
      experimentId: "priority-starvation",
      variants: [{ label: "no-aging" }, { label: "aging", aging: { interval: 4, cap: 22 } }],
    },
  },
  {
    id: "edf",
    label: "EDF VS PRIORITY_P",
    hint: "harmonic deadlines at U≈0.9",
    payload: {
      name: "edf-vs-priority",
      experimentId: "edf-deadlines",
      variants: [
        { label: "edf", scheduler: { type: "edf" } },
        { label: "priority_p", scheduler: { type: "priority_p" } },
      ],
    },
  },
  {
    id: "memfit",
    label: "FIRST VS BEST FIT",
    hint: "memory fragmentation policies",
    payload: {
      name: "first-vs-best-fit",
      experimentId: "memory-fragmentation",
      variants: [
        { label: "first_fit", memory: { policy: "first_fit" } },
        { label: "best_fit", memory: { policy: "best_fit" } },
      ],
    },
  },
];

/* ---------------------------------------------------------------------- */
/* variant editor rows                                                     */
/* ---------------------------------------------------------------------- */

interface VariantRow {
  key: number;
  label: string;
  schedType: string; // "" → inherit base scheduler
  quantum: string;
  memPolicy: string; // "" → no override
  aging: boolean;
  agingInterval: string;
  agingCap: string;
}

let variantSeq = 0;
const newVariant = (): VariantRow => ({
  key: ++variantSeq,
  label: "",
  schedType: "",
  quantum: "4",
  memPolicy: "",
  aging: false,
  agingInterval: "4",
  agingCap: "22",
});

function VariantEditor({
  variants,
  onChange,
  experimentsHaveMemory,
}: {
  variants: VariantRow[];
  onChange: (rows: VariantRow[]) => void;
  /** true/false when a base experiment is selected; null when using inline JSON. */
  experimentsHaveMemory: boolean | null;
}) {
  const update = (key: number, patch: Partial<VariantRow>) => {
    onChange(variants.map((v) => (v.key === key ? { ...v, ...patch } : v)));
  };
  return (
    <div className="space-y-2">
      {variants.map((v, i) => (
        <div
          key={v.key}
          className="rounded-md border border-line bg-bg p-2"
          aria-label={`Variant ${i + 1}`}
        >
          <div className="flex flex-wrap items-center gap-2">
            <span className="font-mono text-[10px] text-ink-dim">V{i + 1}</span>
            <Input
              value={v.label}
              onChange={(e) => update(v.key, { label: e.target.value })}
              placeholder="label"
              aria-label={`Variant ${i + 1} label`}
              className="h-8 w-32 rounded-md font-mono text-[11px]"
            />
            <Select
              value={v.schedType || "inherit"}
              onValueChange={(val) => update(v.key, { schedType: val === "inherit" ? "" : val })}
            >
              <SelectTrigger
                className="h-8 w-36 rounded-md font-mono text-[11px]"
                aria-label={`Variant ${i + 1} scheduler`}
              >
                <SelectValue />
              </SelectTrigger>
              <SelectContent>
                <SelectItem value="inherit">scheduler: inherit</SelectItem>
                {SCHEDULERS.map((s) => (
                  <SelectItem key={s} value={s}>
                    scheduler: {s}
                  </SelectItem>
                ))}
              </SelectContent>
            </Select>
            {v.schedType === "rr" && (
              <Input
                type="number"
                min={1}
                value={v.quantum}
                onChange={(e) => update(v.key, { quantum: e.target.value })}
                aria-label={`Variant ${i + 1} quantum`}
                className="h-8 w-20 rounded-md font-mono text-[11px]"
              />
            )}
            <Select
              value={v.memPolicy || "nomem"}
              onValueChange={(val) => update(v.key, { memPolicy: val === "nomem" ? "" : val })}
            >
              <SelectTrigger
                className="h-8 w-36 rounded-md font-mono text-[11px]"
                aria-label={`Variant ${i + 1} memory policy`}
              >
                <SelectValue />
              </SelectTrigger>
              <SelectContent>
                <SelectItem value="nomem">memory: inherit</SelectItem>
                {MEMORY_POLICIES.map((p) => (
                  <SelectItem key={p} value={p}>
                    memory: {p}
                  </SelectItem>
                ))}
              </SelectContent>
            </Select>
            <Button
              type="button"
              size="icon"
              variant="ghost"
              className="ml-auto h-8 w-8 cursor-pointer rounded-md text-ink-dim hover:text-bad"
              onClick={() => onChange(variants.filter((x) => x.key !== v.key))}
              disabled={variants.length <= 2}
              aria-label={`Remove variant ${i + 1}`}
            >
              <Trash2 className="h-3.5 w-3.5" aria-hidden />
            </Button>
          </div>
          <div className="mt-2 flex flex-wrap items-center gap-2">
            <label className="flex cursor-pointer items-center gap-1.5 font-mono text-[10px] text-ink-dim">
              <Checkbox
                checked={v.aging}
                onCheckedChange={(c) => update(v.key, { aging: c === true })}
                aria-label={`Variant ${i + 1} aging`}
              />
              aging
            </label>
            {v.aging && (
              <>
                <Input
                  type="number"
                  min={1}
                  value={v.agingInterval}
                  onChange={(e) => update(v.key, { agingInterval: e.target.value })}
                  aria-label={`Variant ${i + 1} aging interval`}
                  className="h-7 w-20 rounded-md font-mono text-[10px]"
                />
                <span className="font-mono text-[10px] text-ink-dim">interval</span>
                <Input
                  type="number"
                  min={1}
                  value={v.agingCap}
                  onChange={(e) => update(v.key, { agingCap: e.target.value })}
                  aria-label={`Variant ${i + 1} aging cap`}
                  className="h-7 w-20 rounded-md font-mono text-[10px]"
                />
                <span className="font-mono text-[10px] text-ink-dim">cap</span>
              </>
            )}
          </div>
        </div>
      ))}
      <Button
        type="button"
        size="sm"
        variant="outline"
        className="h-8 cursor-pointer gap-1 rounded-md font-mono text-[10px] hover:bg-accent/10"
        onClick={() => onChange([...variants, newVariant()])}
        aria-label="Add variant"
      >
        <Plus className="h-3.5 w-3.5" aria-hidden />
        ADD VARIANT
      </Button>
      {experimentsHaveMemory === false && (
        <p className="font-mono text-[9px] text-ink-dim/70">
          memory policy overrides need a base config with a memory block
        </p>
      )}
    </div>
  );
}

/* ---------------------------------------------------------------------- */
/* comparison result rendering                                             */
/* ---------------------------------------------------------------------- */

function fmtMetric(v: number | null | undefined): string {
  if (v === null || v === undefined) return "—";
  if (Math.abs(v) >= 1000) return v.toFixed(0);
  return v.toFixed(3).replace(/(\.\d*?)0+$/, "$1").replace(/\.$/, "");
}

function ComparisonResultPanel({ record }: { record: ComparisonRecord }) {
  const results = record.results;
  const metricKeys = React.useMemo<string[]>(() => {
    const present = new Set<string>();
    for (const v of results.variants) {
      for (const k of Object.keys(v.metrics)) {
        if (v.metrics[k] !== null && v.metrics[k] !== undefined) present.add(k);
      }
    }
    return METRIC_ORDER.filter((k) => present.has(k));
  }, [results]);

  const [selectedMetric, setSelectedMetric] = React.useState<string>(metricKeys[0] ?? "avgWaiting");
  const metric = metricKeys.includes(selectedMetric) ? selectedMetric : (metricKeys[0] ?? "avgWaiting");

  const deltaByLabel = React.useMemo(() => {
    const m = new Map<string, Record<string, number>>();
    for (const row of results.deltasVsFirst) m.set(row.label, row.metrics);
    return m;
  }, [results.deltasVsFirst]);

  const chartData = React.useMemo(
    () =>
      results.variants.map((v) => ({
        label: v.label,
        value: v.metrics[metric] ?? 0,
      })),
    [results.variants, metric],
  );
  const bestLabel = results.bestPerMetric[metric]?.label ?? null;

  const runVariant = (label: string) => results.variants.find((v) => v.label === label);

  return (
    <div className="space-y-3">
      {/* variant cards */}
      <div className="grid grid-cols-1 gap-2 md:grid-cols-2 2xl:grid-cols-3">
        {results.variants.map((v) => (
          <div key={v.label} className="rounded-md border border-line bg-bg p-2.5">
            <div className="flex flex-wrap items-center gap-2">
              <span className="font-mono text-xs text-accent">{v.label}</span>
              <span className="rounded-sm border border-line bg-panel px-1.5 py-px font-mono text-[10px] text-ink-dim">
                {v.scheduler}
              </span>
              {v.aging && (
                <span className="rounded-sm border border-ctx/60 bg-ctx/10 px-1.5 py-px font-mono text-[10px] text-ink-dim">
                  aging
                </span>
              )}
              <span
                className={cn(
                  "ml-auto font-mono text-[9px]",
                  v.status === "completed"
                    ? "text-ok"
                    : v.status === "failed"
                      ? "text-bad"
                      : "text-ink-dim",
                )}
              >
                {v.status}
              </span>
            </div>
            <p className="mt-1 font-mono text-[9px] text-ink-dim">
              config {v.configHash ?? "—"}
            </p>
            {v.starvedTaskIds.length > 0 && (
              <p className="mt-1 flex flex-wrap gap-1">
                {v.starvedTaskIds.map((id) => (
                  <span
                    key={id}
                    className="rounded-sm border border-bad/40 bg-bad/10 px-1.5 py-px font-mono text-[9px] text-bad"
                  >
                    {id} STARVED
                  </span>
                ))}
              </p>
            )}
          </div>
        ))}
      </div>

      {/* matrix */}
      <SectionPanel title="METRIC × VARIANT MATRIX · BEST HIGHLIGHTED · Δ VS FIRST">
        <div className="overflow-x-auto">
          <table className="w-full border-collapse font-mono text-[11px]">
            <thead>
              <tr className="text-left">
                <th className="border-b border-line px-2 py-1 text-[9px] font-normal tracking-widest text-ink-dim">
                  METRIC
                </th>
                {results.variants.map((v) => (
                  <th
                    key={v.label}
                    className="border-b border-line px-2 py-1 text-[9px] font-normal tracking-widest text-ink"
                  >
                    {v.label}
                  </th>
                ))}
              </tr>
            </thead>
            <tbody>
              {metricKeys.map((key) => {
                const best = results.bestPerMetric[key] ?? null;
                const direction = best?.direction ?? (HIGHER_IS_BETTER.has(key) ? "higher" : "lower");
                const values = results.variants.map((v) => v.metrics[key] ?? null);
                const valid = values.filter((v): v is number => v !== null);
                let worstVal: number | null = null;
                if (valid.length > 1) {
                  worstVal =
                    direction === "higher" ? Math.min(...valid) : Math.max(...valid);
                }
                return (
                  <tr key={key} className="border-b border-line/40">
                    <td className="px-2 py-1 text-ink-dim">{key}</td>
                    {results.variants.map((v) => {
                      const value = v.metrics[key] ?? null;
                      const isBest = best !== null && v.label === best.label;
                      const isWorst = !isBest && value !== null && worstVal !== null && value === worstVal;
                      const delta = deltaByLabel.get(v.label)?.[key];
                      return (
                        <td key={v.label} className="px-2 py-1">
                          <span
                            className={cn(
                              "inline-block rounded-sm border px-1.5 py-px tabular-nums",
                              isBest && "border-ok/40 bg-ok/10 text-ok",
                              isWorst && "border-bad/40 bg-bad/10 text-bad",
                              !isBest && !isWorst && "border-transparent text-ink",
                            )}
                          >
                            {fmtMetric(value)}
                          </span>
                          {delta !== undefined && results.variants.indexOf(v) > 0 && (
                            <span className="ml-1 text-[9px] text-ink-dim tabular-nums">
                              {delta >= 0 ? "+" : ""}
                              {fmtMetric(delta)}
                            </span>
                          )}
                        </td>
                      );
                    })}
                  </tr>
                );
              })}
            </tbody>
          </table>
        </div>
      </SectionPanel>

      {/* chart */}
      <SectionPanel
        title="SELECTED METRIC BY VARIANT"
        right={
          <Select value={metric} onValueChange={setSelectedMetric}>
            <SelectTrigger className="h-7 w-44 rounded-md font-mono text-[10px]" aria-label="Select metric">
              <SelectValue />
            </SelectTrigger>
            <SelectContent>
              {metricKeys.map((k) => (
                <SelectItem key={k} value={k}>
                  {k}
                </SelectItem>
              ))}
            </SelectContent>
          </Select>
        }
      >
        <div className="h-52">
          <ResponsiveContainer width="100%" height="100%">
            <BarChart data={chartData} margin={{ top: 8, right: 8, bottom: 4, left: 0 }}>
              <CartesianGrid stroke="var(--line)" strokeDasharray="2 4" />
              <XAxis
                dataKey="label"
                tick={{ fill: "var(--ink-dim)", fontSize: 10, fontFamily: "var(--font-mono)" }}
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
                cursor={{ fill: "var(--accent)", fillOpacity: 0.08 }}
              />
              <Bar dataKey="value" name={metric} radius={[3, 3, 0, 0]} isAnimationActive={false}>
                {chartData.map((d) => (
                  <Cell
                    key={d.label}
                    fill={bestLabel === d.label ? "var(--accent)" : "var(--state-ready)"}
                  />
                ))}
              </Bar>
            </BarChart>
          </ResponsiveContainer>
        </div>
        <p className="mt-1 text-center font-mono text-[9px] text-ink-dim">
          best: {bestLabel ?? "—"} · {direction(metric)} is better
        </p>
      </SectionPanel>
    </div>
  );
}

function direction(metric: string): string {
  return HIGHER_IS_BETTER.has(metric) ? "higher" : "lower";
}

/* ---------------------------------------------------------------------- */
/* view                                                                    */
/* ---------------------------------------------------------------------- */

export function CompareView() {
  const experiments = useExperiments();
  const comparisons = useComparisons();
  const createMutation = useCreateComparison();

  const [selectedId, setSelectedId] = React.useState<string | null>(null);
  const comparison = useComparison(selectedId);
  const [reportOpen, setReportOpen] = React.useState(false);
  const report = useComparisonReport(selectedId, reportOpen);

  /* creator state */
  const [name, setName] = React.useState("");
  const [baseMode, setBaseMode] = React.useState<"experiment" | "json">("experiment");
  const [experimentId, setExperimentId] = React.useState("");
  const [configJson, setConfigJson] = React.useState("");
  const [jsonError, setJsonError] = React.useState<string | null>(null);
  const [formError, setFormError] = React.useState<string | null>(null);
  const [variants, setVariants] = React.useState<VariantRow[]>(() => [newVariant(), newVariant()]);
  const [pendingPreset, setPendingPreset] = React.useState<string | null>(null);

  React.useEffect(() => {
    if (!experimentId && experiments.data && experiments.data.items.length > 0) {
      setExperimentId(experiments.data.items[0].id);
    }
  }, [experiments.data, experimentId]);

  const runPayload = (): CreateComparisonPayload | string => {
    if (variants.length < 2) return "Need at least 2 variants.";
    if (variants.some((v) => v.label.trim() === "")) return "Every variant needs a label.";
    let base: { experimentId?: string; config?: Record<string, unknown> };
    if (baseMode === "experiment") {
      if (!experimentId) return "Pick a base experiment.";
      base = { experimentId };
    } else {
      try {
        const parsed: unknown = JSON.parse(configJson);
        if (typeof parsed !== "object" || parsed === null || Array.isArray(parsed)) {
          return "Config JSON must be an object.";
        }
        base = { config: parsed as Record<string, unknown> };
      } catch (err) {
        return `Invalid JSON: ${err instanceof Error ? err.message : "parse error"}`;
      }
    }
    const specVariants: ComparisonVariantInput[] = variants.map((v) => {
      const spec: ComparisonVariantInput = { label: v.label.trim() };
      if (v.schedType) {
        spec.scheduler =
          v.schedType === "rr" && v.quantum !== ""
            ? { type: "rr", quantum: Number(v.quantum) }
            : { type: v.schedType };
      }
      if (v.memPolicy) spec.memory = { policy: v.memPolicy };
      if (v.aging) {
        spec.aging = {
          interval: Number(v.agingInterval) || 4,
          cap: Number(v.agingCap) || 22,
        };
      }
      return spec;
    });
    return {
      name: name.trim() || undefined,
      ...base,
      variants: specVariants,
    };
  };

  const run = (payload: CreateComparisonPayload, presetId: string | null) => {
    setPendingPreset(presetId);
    createMutation.mutate(payload, {
      onSuccess: (record) => {
        toast.success(
          `Comparison "${record.name}" completed — ${record.results.variants.length} variants`,
        );
        setSelectedId(record.id);
      },
      onError: (err) => {
        toast.error(formatApiError(err));
      },
      onSettled: () => setPendingPreset(null),
    });
  };

  const onRunCustom = () => {
    const payload = runPayload();
    if (typeof payload === "string") {
      setFormError(payload);
      return;
    }
    setFormError(null);
    setJsonError(null);
    run(payload, null);
  };

  const record = comparison.data ?? null;
  const selectedSummary = comparisons.data?.items.find((c) => c.id === selectedId) ?? null;

  return (
    <div className="flex h-full min-h-0 flex-col">
      <ViewHeader num="09" label="Compare" hint="scheduler variants deep-merged over one base config">
        <Button
          type="button"
          size="sm"
          variant="outline"
          className="h-8 cursor-pointer rounded-md font-mono text-[11px] hover:bg-accent/10 disabled:opacity-50"
          disabled={!record}
          onClick={() => setReportOpen(true)}
          aria-label="Export comparison report"
        >
          EXPORT REPORT
        </Button>
      </ViewHeader>

      <div className="grid min-h-0 flex-1 grid-cols-1 gap-3 overflow-y-auto p-4 xl:grid-cols-[400px_1fr]">
        {/* left column */}
        <div className="space-y-3">
          <SectionPanel title="PRESETS · ONE CLICK">
            <div className="grid grid-cols-1 gap-2 sm:grid-cols-2 xl:grid-cols-1">
              {PRESETS.map((preset) => (
                <button
                  key={preset.id}
                  type="button"
                  disabled={createMutation.isPending}
                  onClick={() => run(preset.payload, preset.id)}
                  aria-label={`Run preset ${preset.label}`}
                  className="flex cursor-pointer items-center gap-2 rounded-md border border-line bg-bg px-3 py-2 text-left transition-colors hover:border-accent/40 hover:bg-accent/5 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-accent disabled:cursor-not-allowed disabled:opacity-60"
                >
                  {pendingPreset === preset.id && createMutation.isPending ? (
                    <Loader2 className="h-3.5 w-3.5 animate-spin text-accent" aria-hidden />
                  ) : (
                    <Play className="h-3.5 w-3.5 text-accent" aria-hidden />
                  )}
                  <span>
                    <span className="block font-mono text-[11px] text-ink">{preset.label}</span>
                    <span className="block text-[10px] text-ink-dim">{preset.hint}</span>
                  </span>
                </button>
              ))}
            </div>
          </SectionPanel>

          <SectionPanel title="COMPARISON CREATOR">
            <div className="space-y-3">
              <Input
                value={name}
                onChange={(e) => setName(e.target.value)}
                placeholder="comparison name (optional)"
                aria-label="Comparison name"
                className="h-8 rounded-md font-mono text-[11px]"
              />
              <Tabs
                value={baseMode}
                onValueChange={(v) => setBaseMode(v === "json" ? "json" : "experiment")}
              >
                <TabsList className="h-8">
                  <TabsTrigger value="experiment" className="font-mono text-[10px]">
                    EXPERIMENT
                  </TabsTrigger>
                  <TabsTrigger value="json" className="font-mono text-[10px]">
                    CONFIG JSON
                  </TabsTrigger>
                </TabsList>
              </Tabs>
              {baseMode === "experiment" ? (
                experiments.isLoading ? (
                  <Skeleton className="h-8 w-full" />
                ) : experiments.isError ? (
                  <div className="flex items-center gap-2">
                    <p className="font-mono text-[10px] text-bad">experiments unavailable</p>
                    <Button
                      type="button"
                      size="sm"
                      variant="outline"
                      className="h-7 cursor-pointer rounded-md font-mono text-[10px]"
                      onClick={() => void experiments.refetch()}
                    >
                      RETRY
                    </Button>
                  </div>
                ) : (
                  <Select value={experimentId} onValueChange={setExperimentId}>
                    <SelectTrigger className="h-8 w-full rounded-md font-mono text-[11px]" aria-label="Base experiment">
                      <SelectValue placeholder="pick base experiment" />
                    </SelectTrigger>
                    <SelectContent>
                      {(experiments.data?.items ?? []).map((exp) => (
                        <SelectItem key={exp.id} value={exp.id}>
                          {exp.id} {exp.source === "builtin" ? "" : "(custom)"}
                        </SelectItem>
                      ))}
                    </SelectContent>
                  </Select>
                )
              ) : (
                <div>
                  <Textarea
                    value={configJson}
                    onChange={(e) => setConfigJson(e.target.value)}
                    placeholder='{"schema":"micrort-config/1", ...}'
                    aria-label="Base config JSON"
                    className="min-h-28 rounded-md font-mono text-[10px]"
                  />
                  {jsonError && <p className="mt-1 font-mono text-[10px] text-bad">{jsonError}</p>}
                </div>
              )}
              <VariantEditor
                variants={variants}
                onChange={setVariants}
                experimentsHaveMemory={
                  baseMode === "experiment"
                    ? Boolean(
                        experiments.data?.items.find((e) => e.id === experimentId)?.config.memory,
                      )
                    : null
                }
              />
              {formError && <p className="font-mono text-[10px] text-bad">{formError}</p>}
              <Button
                type="button"
                disabled={createMutation.isPending}
                onClick={onRunCustom}
                className="h-9 w-full cursor-pointer rounded-md font-mono text-[11px]"
                aria-label="Run comparison"
              >
                {createMutation.isPending && pendingPreset === null ? (
                  <Loader2 className="mr-1.5 h-3.5 w-3.5 animate-spin" aria-hidden />
                ) : (
                  <Play className="mr-1.5 h-3.5 w-3.5" aria-hidden />
                )}
                RUN COMPARISON ({variants.length} VARIANTS)
              </Button>
            </div>
          </SectionPanel>

          <SectionPanel title={`HISTORY · ${comparisons.data?.total ?? 0}`}>
            {comparisons.isLoading && <Skeleton className="h-16 w-full" />}
            {comparisons.isError && (
              <div className="flex items-center gap-2">
                <p className="font-mono text-[10px] text-bad">
                  {comparisons.error instanceof Error ? comparisons.error.message : "load failed"}
                </p>
                <Button
                  type="button"
                  size="sm"
                  variant="outline"
                  className="ml-auto h-7 cursor-pointer rounded-md font-mono text-[10px]"
                  onClick={() => void comparisons.refetch()}
                >
                  RETRY
                </Button>
              </div>
            )}
            {comparisons.data && comparisons.data.items.length === 0 && (
              <p className="py-2 text-center font-mono text-[10px] text-ink-dim">
                NO COMPARISONS YET — RUN A PRESET
              </p>
            )}
            <ul className="max-h-64 space-y-1 overflow-y-auto">
              {(comparisons.data?.items ?? []).map((c) => (
                <li key={c.id}>
                  <button
                    type="button"
                    onClick={() => setSelectedId(c.id)}
                    aria-pressed={selectedId === c.id}
                    className={cn(
                      "w-full cursor-pointer rounded-md border px-2.5 py-1.5 text-left transition-colors focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-accent",
                      selectedId === c.id
                        ? "border-accent/40 bg-accent/10"
                        : "border-line bg-bg hover:border-accent/30",
                    )}
                  >
                    <span className="flex items-center gap-2">
                      <span className="font-mono text-[11px] text-ink">{c.name}</span>
                      <span className="ml-auto font-mono text-[9px] text-ink-dim">
                        {c.variantCount}v
                      </span>
                    </span>
                    <span className="mt-0.5 block font-mono text-[9px] text-ink-dim">
                      {c.labels.join(" · ")}
                    </span>
                    <span className="mt-0.5 block font-mono text-[9px] text-ink-dim/70">
                      {new Date(c.createdAt).toLocaleString(undefined, {
                        month: "short",
                        day: "2-digit",
                        hour: "2-digit",
                        minute: "2-digit",
                      })}
                    </span>
                  </button>
                </li>
              ))}
            </ul>
          </SectionPanel>
        </div>

        {/* right column */}
        <div className="min-w-0">
          {comparison.isLoading && <Skeleton className="h-64 w-full" />}
          {comparison.isError && (
            <div className="flex h-full items-center justify-center">
              <div className="w-full max-w-md rounded-lg border border-bad/40 bg-panel p-6">
                <p className="font-mono text-[10px] tracking-widest text-bad">COMPARISON ERROR</p>
                <p className="mt-2 font-mono text-xs break-words text-ink-dim">
                  {comparison.error instanceof Error ? comparison.error.message : "load failed"}
                </p>
                <Button
                  type="button"
                  variant="outline"
                  className="mt-4 cursor-pointer rounded-md hover:bg-accent/10"
                  onClick={() => void comparison.refetch()}
                >
                  Retry
                </Button>
              </div>
            </div>
          )}
          {record ? (
            <div className="space-y-3">
              <div className="rounded-lg border border-line bg-panel px-3 py-2">
                <div className="flex flex-wrap items-center gap-x-3 gap-y-1">
                  <span className="font-mono text-sm text-ink">{record.name}</span>
                  <span className="font-mono text-[10px] text-ink-dim">
                    base {record.baseConfig.name}
                  </span>
                  <span className="ml-auto font-mono text-[10px] text-ink-dim">
                    {new Date(record.createdAt).toLocaleString()}
                  </span>
                </div>
              </div>
              <ComparisonResultPanel record={record} />
            </div>
          ) : (
            !comparison.isLoading &&
            !comparison.isError && (
              <EmptyPanel
                label="NO COMPARISON SELECTED"
                hint={
                  selectedSummary
                    ? "Loading comparison…"
                    : "Run a preset or a custom comparison, or pick one from history."
                }
              />
            )
          )}
        </div>
      </div>

      {/* report drawer */}
      <Sheet open={reportOpen} onOpenChange={setReportOpen}>
        <SheetContent side="right" className="flex w-full flex-col overflow-hidden sm:max-w-2xl">
          <style>{PRINT_STYLE}</style>
          <SheetHeader className="shrink-0 border-b border-line">
            <SheetTitle className="flex items-center gap-2 font-mono text-xs">
              COMPARISON REPORT
              <span className="text-ink-dim">{record?.name}</span>
              <span className="ml-auto flex gap-1">
                <Button
                  type="button"
                  size="icon"
                  variant="ghost"
                  className="h-8 w-8 cursor-pointer rounded-md hover:bg-accent/10"
                  onClick={() =>
                    record &&
                    downloadBlob(
                      `micrort-comparison-${record.name.replace(/[^a-zA-Z0-9_-]+/g, "_")}.md`,
                      report.data ?? "",
                      "text/markdown",
                    )
                  }
                  aria-label="Download report as markdown"
                >
                  <span className="font-mono text-[10px]">.MD</span>
                </Button>
                <Button
                  type="button"
                  size="icon"
                  variant="ghost"
                  className="h-8 w-8 cursor-pointer rounded-md hover:bg-accent/10"
                  onClick={() => window.print()}
                  aria-label="Print report"
                >
                  <Printer className="h-4 w-4" aria-hidden />
                </Button>
              </span>
            </SheetTitle>
          </SheetHeader>
          <div className="print-target min-h-0 flex-1 overflow-y-auto p-4">
            {report.isLoading && <Skeleton className="h-40 w-full" />}
            {report.isError && (
              <p className="font-mono text-[10px] text-bad">
                report failed:{" "}
                {report.error instanceof Error ? report.error.message : "unknown error"}
              </p>
            )}
            {report.data && <MiniMarkdown source={report.data} />}
          </div>
        </SheetContent>
      </Sheet>
    </div>
  );
}
