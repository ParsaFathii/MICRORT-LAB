"use client";

/**
 * MicroRT-Lab — Workbench view (01): experiment library, detail +
 * run + editable clone-to-simulation, and simulation history.
 */

import * as React from "react";

import { formatDistanceToNow } from "date-fns";
import {
  ChevronRight,
  Loader2,
  Play,
  Trash2,
} from "lucide-react";
import { toast } from "sonner";

import {
  AlertDialog,
  AlertDialogAction,
  AlertDialogCancel,
  AlertDialogContent,
  AlertDialogDescription,
  AlertDialogFooter,
  AlertDialogHeader,
  AlertDialogTitle,
} from "@/components/ui/alert-dialog";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import {
  Collapsible,
  CollapsibleContent,
  CollapsibleTrigger,
} from "@/components/ui/collapsible";
import { Input } from "@/components/ui/input";
import { Skeleton } from "@/components/ui/skeleton";
import { Textarea } from "@/components/ui/textarea";
import { cn } from "@/lib/utils";
import { formatApiError } from "@/lib/api";
import type { ExperimentRecord, SimulationRecord } from "@/lib/types";
import {
  useCreateSimulation,
  useDeleteSimulation,
  useExperiments,
  useRunExperiment,
  useSimulations,
} from "@/hooks/useSimulation";
import { useWorkstation } from "@/store/workstation";

/* ------------------------------------------------------------------------ */

function statusDotClass(record: SimulationRecord): string {
  if (record.status === "running") return "bg-accent animate-pulse";
  if (record.status === "completed") {
    return record.resultStatus === "stopped" ? "bg-accent" : "bg-ok";
  }
  if (record.status === "failed" || record.status === "stopped") return "bg-bad";
  return "bg-ink-dim";
}

function PanelHeader({ label, count }: { label: string; count?: number }) {
  return (
    <div className="flex h-9 shrink-0 items-center gap-2 border-b border-line px-3">
      <span className="font-mono text-[10px] tracking-widest text-ink-dim">
        {label}
      </span>
      {typeof count === "number" && (
        <span className="font-mono text-[10px] text-ink-dim/60">{count}</span>
      )}
    </div>
  );
}

/* ---- experiment library ------------------------------------------------- */

function ExperimentRow({
  experiment,
  active,
  onSelect,
}: {
  experiment: ExperimentRecord;
  active: boolean;
  onSelect: () => void;
}) {
  return (
    <button
      type="button"
      onClick={onSelect}
      aria-pressed={active}
      className={cn(
        "flex w-full cursor-pointer flex-col gap-1 border-b border-line/60 px-3 py-2.5 text-left transition-colors focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-accent",
        active ? "bg-accent/10" : "hover:bg-accent/5",
      )}
    >
      <span className="flex items-center gap-2">
        <span className="truncate font-mono text-xs text-ink">{experiment.name}</span>
        <span
          className={cn(
            "ml-auto shrink-0 rounded-sm border px-1 font-mono text-[9px] uppercase",
            experiment.source === "builtin"
              ? "border-line text-ink-dim"
              : "border-ok/50 text-ok",
          )}
        >
          {experiment.source}
        </span>
      </span>
      {experiment.description && (
        <span className="line-clamp-2 text-[11px] leading-4 text-ink-dim">
          {experiment.description}
        </span>
      )}
    </button>
  );
}

/* ---- simulation history row --------------------------------------------- */

function SimulationRow({
  record,
  active,
  onSelect,
  onDelete,
}: {
  record: SimulationRecord;
  active: boolean;
  onSelect: () => void;
  onDelete: () => void;
}) {
  const rel = React.useMemo(
    () => formatDistanceToNow(new Date(record.createdAt), { addSuffix: true }),
    [record.createdAt],
  );
  return (
    <div
      className={cn(
        "group flex items-center gap-2 border-b border-line/60 px-3 py-2 transition-colors",
        active ? "bg-accent/10" : "hover:bg-accent/5",
      )}
    >
      <button
        type="button"
        onClick={onSelect}
        className="flex min-w-0 flex-1 cursor-pointer items-center gap-2 text-left focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-accent"
        aria-label={`Load simulation ${record.name}`}
      >
        <span aria-hidden className={cn("h-2 w-2 shrink-0 rounded-full", statusDotClass(record))} />
        <span className="min-w-0 flex-1">
          <span className="block truncate font-mono text-xs text-ink">{record.name}</span>
          <span className="block truncate font-mono text-[10px] text-ink-dim">
            {record.scheduler} · {record.completedJobs ?? "—"} jobs ·{" "}
            {record.totalEvents ?? "—"} ev · {rel}
          </span>
        </span>
      </button>
      <Button
        type="button"
        variant="ghost"
        size="icon"
        className="h-7 w-7 shrink-0 cursor-pointer rounded-sm text-ink-dim opacity-0 transition-opacity hover:bg-bad/10 hover:text-bad focus-visible:opacity-100 group-hover:opacity-100"
        onClick={(e) => {
          e.stopPropagation();
          onDelete();
        }}
        aria-label={`Delete simulation ${record.name}`}
      >
        <Trash2 className="h-3.5 w-3.5" />
      </Button>
    </div>
  );
}

/* ---- detail: summary chips ---------------------------------------------- */

function SummaryChips({ experiment }: { experiment: ExperimentRecord }) {
  const cfg = experiment.config;
  const chips: string[] = [
    `sched ${cfg.scheduler?.type ?? "?"}${cfg.scheduler?.quantum != null ? ` q=${cfg.scheduler.quantum}` : ""}`,
    `tasks ${cfg.tasks?.length ?? 0}`,
    `res ${cfg.resources?.length ?? 0}`,
    `dur ${cfg.duration}t`,
    `seed ${cfg.seed ?? 0}`,
  ];
  if (cfg.memory) {
    chips.push(`mem ${cfg.memory.model}${cfg.memory.policy ? ` ${cfg.memory.policy}` : ""}`);
  }
  if (cfg.aging) chips.push(`aging ${cfg.aging.interval}/${cfg.aging.cap}`);
  if (cfg.contextSwitchCost) chips.push(`ctx ${cfg.contextSwitchCost}t`);
  return (
    <div className="flex flex-wrap gap-1.5">
      {chips.map((chip) => (
        <span
          key={chip}
          className="rounded-sm border border-line bg-bg px-2 py-1 font-mono text-[10px] uppercase tracking-wide text-ink-dim"
        >
          {chip}
        </span>
      ))}
    </div>
  );
}

/* ---- detail: editable clone panel ---------------------------------------- */

function ClonePanel({
  experiment,
  onCreated,
}: {
  experiment: ExperimentRecord;
  onCreated: (record: SimulationRecord) => void;
}) {
  const [name, setName] = React.useState(`${experiment.config.name} (clone)`);
  const [json, setJson] = React.useState(() =>
    JSON.stringify(experiment.config, null, 2),
  );
  const [parseError, setParseError] = React.useState<string | null>(null);
  const createMut = useCreateSimulation();

  // re-seed the editor when a different experiment is selected
  React.useEffect(() => {
    setName(`${experiment.config.name} (clone)`);
    setJson(JSON.stringify(experiment.config, null, 2));
    setParseError(null);
  }, [experiment.id, experiment.config]);

  const submit = () => {
    let parsed: unknown;
    try {
      parsed = JSON.parse(json);
    } catch (err) {
      setParseError(err instanceof Error ? err.message : "invalid JSON");
      return;
    }
    setParseError(null);
    createMut.mutate(
      { name: name.trim() || undefined, config: parsed },
      {
        onSuccess: (record) => {
          toast.success("Simulation created", {
            description: `${record.id} — run it or switch to the timeline.`,
          });
          onCreated(record);
        },
        onError: (err) => {
          toast.error("Create failed", { description: formatApiError(err) });
        },
      },
    );
  };

  return (
    <Collapsible className="rounded-md border border-line">
      <CollapsibleTrigger className="group flex w-full cursor-pointer items-center gap-2 px-3 py-2 text-left hover:bg-accent/5 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-accent">
        <ChevronRight className="h-3.5 w-3.5 text-ink-dim transition-transform group-data-[state=open]:rotate-90" />
        <span className="font-mono text-[10px] tracking-widest text-ink-dim">
          CLONE → NEW SIMULATION (EDITABLE)
        </span>
      </CollapsibleTrigger>
      <CollapsibleContent className="space-y-2 border-t border-line p-3">
        <Input
          value={name}
          onChange={(e) => setName(e.target.value)}
          placeholder="simulation name"
          aria-label="Clone simulation name"
          className="h-8 rounded-sm border-line bg-bg font-mono text-xs"
        />
        <Textarea
          value={json}
          onChange={(e) => setJson(e.target.value)}
          spellCheck={false}
          aria-label="Experiment configuration JSON"
          className="min-h-56 rounded-sm border-line bg-bg p-3 font-mono text-[11px] leading-5"
        />
        {parseError && (
          <p className="font-mono text-[11px] text-bad">{parseError}</p>
        )}
        <div className="flex items-center gap-2">
          <Button
            type="button"
            variant="outline"
            className="h-8 cursor-pointer rounded-sm border-line bg-bg font-mono text-xs hover:bg-accent/10"
            onClick={submit}
            disabled={createMut.isPending}
          >
            {createMut.isPending && <Loader2 className="h-3.5 w-3.5 animate-spin" />}
            Create Simulation
          </Button>
          <span className="font-mono text-[10px] text-ink-dim">
            validates server-side; 422 returns details
          </span>
        </div>
      </CollapsibleContent>
    </Collapsible>
  );
}

/* ---- detail panel --------------------------------------------------------- */

function ExperimentDetail({ experiment }: { experiment: ExperimentRecord }) {
  const runMut = useRunExperiment();
  const setSimulation = useWorkstation((s) => s.setSimulation);
  const setActiveView = useWorkstation((s) => s.setActiveView);

  const run = () => {
    runMut.mutate(experiment.id, {
      onSuccess: (record) => {
        if (typeof record?.id !== "string") {
          toast.error("Run failed", { description: "malformed record from the analysis layer" });
          return;
        }
        setSimulation(record.id);
        const events = record.result?.metrics.totalEvents;
        toast.success("Simulation completed", {
          description:
            events !== undefined ? `${events} events recorded` : record.id,
        });
        setActiveView("timeline");
      },
      onError: (err) => {
        toast.error("Run failed", { description: formatApiError(err) });
      },
    });
  };

  return (
    <div className="mx-auto max-w-3xl space-y-5">
      <div className="flex flex-wrap items-center gap-2">
        <h2 className="font-mono text-base text-ink">{experiment.name}</h2>
        <Badge
          variant="outline"
          className="rounded-sm border-line font-mono text-[10px] text-ink-dim"
        >
          {experiment.source}
        </Badge>
      </div>

      {experiment.description && (
        <p className="text-sm leading-6 text-ink-dim">{experiment.description}</p>
      )}

      <SummaryChips experiment={experiment} />

      <div className="flex flex-wrap items-center gap-3">
        <Button
          type="button"
          className="h-9 cursor-pointer rounded-md bg-accent font-mono text-xs text-accent-foreground hover:bg-accent/90 focus-visible:ring-accent"
          onClick={run}
          disabled={runMut.isPending}
        >
          {runMut.isPending ? (
            <Loader2 className="h-4 w-4 animate-spin" />
          ) : (
            <Play className="h-4 w-4" />
          )}
          {runMut.isPending ? "Running…" : "Run Experiment"}
        </Button>
        <span className="font-mono text-[10px] text-ink-dim">
          synchronous engine run → loads timeline
        </span>
      </div>

      <Collapsible className="rounded-md border border-line">
        <CollapsibleTrigger className="group flex w-full cursor-pointer items-center gap-2 px-3 py-2 text-left hover:bg-accent/5 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-accent">
          <ChevronRight className="h-3.5 w-3.5 text-ink-dim transition-transform group-data-[state=open]:rotate-90" />
          <span className="font-mono text-[10px] tracking-widest text-ink-dim">
            CONFIG JSON
          </span>
        </CollapsibleTrigger>
        <CollapsibleContent className="border-t border-line">
          <pre className="max-h-64 overflow-auto p-3 font-mono text-[11px] leading-5 text-ink-dim">
            {JSON.stringify(experiment.config, null, 2)}
          </pre>
        </CollapsibleContent>
      </Collapsible>

      <ClonePanel
        experiment={experiment}
        onCreated={(record) => {
          if (typeof record?.id !== "string") return;
          setSimulation(record.id);
          setActiveView("timeline");
        }}
      />
    </div>
  );
}

/* ---- workbench ------------------------------------------------------------ */

export function WorkbenchView() {
  const experiments = useExperiments();
  const sims = useSimulations();

  const [selectedId, setSelectedId] = React.useState<string | null>(null);
  const [pendingDelete, setPendingDelete] = React.useState<string | null>(null);

  const simId = useWorkstation((s) => s.simId);
  const setSimulation = useWorkstation((s) => s.setSimulation);
  const setActiveView = useWorkstation((s) => s.setActiveView);
  const deleteMut = useDeleteSimulation();

  const items = experiments.data?.items ?? [];
  const selected =
    items.find((e) => e.id === selectedId) ?? (items.length > 0 ? items[0] : null);
  const simItems = sims.data?.items ?? [];

  const confirmDelete = () => {
    const id = pendingDelete;
    setPendingDelete(null);
    if (id === null) return;
    deleteMut.mutate(id, {
      onSuccess: () => {
        toast.success("Simulation deleted");
        if (simId === id) setSimulation(null);
      },
      onError: (err) => {
        toast.error("Delete failed", { description: formatApiError(err) });
      },
    });
  };

  return (
    <div className="h-full overflow-y-auto lg:overflow-hidden">
      <div className="grid lg:h-full lg:grid-cols-[minmax(300px,360px)_1fr]">
        {/* left: library + history */}
        <aside className="flex min-h-0 flex-col border-b border-line bg-panel lg:border-b-0 lg:border-r">
          <PanelHeader label="EXPERIMENTS" count={items.length} />
          <div className="max-h-80 overflow-y-auto lg:max-h-none lg:min-h-0 lg:flex-1">
            {experiments.isLoading && (
              <div className="space-y-2 p-3">
                {Array.from({ length: 5 }, (_, i) => (
                  <Skeleton key={i} className="h-12 w-full" />
                ))}
              </div>
            )}
            {experiments.isError && (
              <div className="p-3">
                <p className="font-mono text-[11px] text-bad">
                  {experiments.error instanceof Error
                    ? experiments.error.message
                    : "failed to load experiments"}
                </p>
                <Button
                  type="button"
                  variant="outline"
                  className="mt-2 h-7 cursor-pointer rounded-sm text-xs hover:bg-accent/10"
                  onClick={() => void experiments.refetch()}
                >
                  Retry
                </Button>
              </div>
            )}
            {experiments.isSuccess &&
              (items.length === 0 ? (
                <p className="p-3 font-mono text-[11px] text-ink-dim">
                  no experiments available
                </p>
              ) : (
                items.map((experiment) => (
                  <ExperimentRow
                    key={experiment.id}
                    experiment={experiment}
                    active={selected?.id === experiment.id}
                    onSelect={() => setSelectedId(experiment.id)}
                  />
                ))
              ))}
          </div>

          <PanelHeader label="SIMULATIONS" count={simItems.length} />
          <div className="max-h-72 overflow-y-auto">
            {sims.isLoading && (
              <div className="space-y-2 p-3">
                {Array.from({ length: 3 }, (_, i) => (
                  <Skeleton key={i} className="h-10 w-full" />
                ))}
              </div>
            )}
            {sims.isError && (
              <div className="p-3">
                <p className="font-mono text-[11px] text-bad">
                  {sims.error instanceof Error ? sims.error.message : "load failed"}
                </p>
                <Button
                  type="button"
                  variant="outline"
                  className="mt-2 h-7 cursor-pointer rounded-sm text-xs hover:bg-accent/10"
                  onClick={() => void sims.refetch()}
                >
                  Retry
                </Button>
              </div>
            )}
            {sims.isSuccess &&
              (simItems.length === 0 ? (
                <p className="p-3 font-mono text-[11px] text-ink-dim">
                  no simulations yet — run an experiment
                </p>
              ) : (
                simItems.map((record) => (
                  <SimulationRow
                    key={record.id}
                    record={record}
                    active={simId === record.id}
                    onSelect={() => {
                      setSimulation(record.id);
                      setActiveView("timeline");
                    }}
                    onDelete={() => setPendingDelete(record.id)}
                  />
                ))
              ))}
          </div>
        </aside>

        {/* right: detail */}
        <section className="min-h-0 overflow-y-auto p-4 lg:p-6" aria-label="Experiment detail">
          {selected ? (
            <ExperimentDetail experiment={selected} />
          ) : (
            <div className="flex h-full min-h-64 items-center justify-center">
              <p className="font-mono text-xs text-ink-dim">
                select an experiment from the library
              </p>
            </div>
          )}
        </section>
      </div>

      <AlertDialog
        open={pendingDelete !== null}
        onOpenChange={(open) => {
          if (!open) setPendingDelete(null);
        }}
      >
        <AlertDialogContent className="rounded-lg border-line bg-panel">
          <AlertDialogHeader>
            <AlertDialogTitle className="font-mono text-sm text-ink">
              Delete simulation
            </AlertDialogTitle>
            <AlertDialogDescription className="text-xs text-ink-dim">
              Permanently remove this simulation record and its result document
              from the analysis store. This cannot be undone.
            </AlertDialogDescription>
          </AlertDialogHeader>
          <AlertDialogFooter>
            <AlertDialogCancel className="cursor-pointer rounded-md font-mono text-xs">
              Cancel
            </AlertDialogCancel>
            <AlertDialogAction
              className="cursor-pointer rounded-md bg-bad font-mono text-xs text-white hover:bg-bad/90"
              onClick={confirmDelete}
            >
              Delete
            </AlertDialogAction>
          </AlertDialogFooter>
        </AlertDialogContent>
      </AlertDialog>
    </div>
  );
}
