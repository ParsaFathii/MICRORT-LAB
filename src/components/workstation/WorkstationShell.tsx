"use client";

/**
 * MicroRT-Lab — workstation shell: the single application frame.
 *
 * Layout (page never scrolls — the main area scrolls independently):
 *   ┌──────────────── header (h-12) ────────────────────┐
 *   │ rail │  main view (fade/slide 150ms, scrolls)     │
 *   └──────────────── transport bar (h-12, sticky) ─────┘
 *
 * WorkstationProviders is consumed by src/app/layout.tsx (theme, query
 * client, toaster). Everything below is client-side only.
 */

import * as React from "react";

import { QueryClient, QueryClientProvider } from "@tanstack/react-query";
import { AnimatePresence, motion } from "framer-motion";
import { ThemeProvider, useTheme } from "next-themes";

import {
  BookOpen,
  Boxes,
  ChevronLeft,
  ChevronRight,
  FlaskConical,
  FileText,
  GanttChartSquare,
  Gauge,
  GitCompareArrows,
  ListTree,
  Loader2,
  MemoryStick,
  Network,
  Pause,
  Play,
} from "lucide-react";

import { Toaster } from "@/components/ui/sonner";
import { Button } from "@/components/ui/button";
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import { Separator } from "@/components/ui/separator";
import { Tooltip, TooltipContent, TooltipTrigger } from "@/components/ui/tooltip";

import { usePlaybackClock } from "@/hooks/usePlaybackClock";
import { useHealth, useSimulation } from "@/hooks/useSimulation";
import { cn } from "@/lib/utils";
import {
  PLAYBACK_SPEEDS,
  VIEW_IDS,
  useWorkstation,
  type ViewId,
} from "@/store/workstation";
import { VIEWS, VIEW_META } from "@/components/views/registry";

/* ---------------------------------------------------------------------------
 * Providers (consumed by the server layout)
 * ------------------------------------------------------------------------- */

function ThemedToaster() {
  const { resolvedTheme } = useTheme();
  return (
    <Toaster
      theme={(resolvedTheme ?? "dark") as "light" | "dark"}
      position="top-right"
      offset={56}
    />
  );
}

export function WorkstationProviders({ children }: { children: React.ReactNode }) {
  const [queryClient] = React.useState(
    () =>
      new QueryClient({
        defaultOptions: {
          queries: { retry: 1, refetchOnWindowFocus: false },
        },
      }),
  );
  return (
    <QueryClientProvider client={queryClient}>
      <ThemeProvider attribute="class" defaultTheme="dark" enableSystem={false}>
        {children}
        <ThemedToaster />
      </ThemeProvider>
    </QueryClientProvider>
  );
}

/* ---------------------------------------------------------------------------
 * Rail
 * ------------------------------------------------------------------------- */

const RAIL_ICONS: Record<ViewId, React.ComponentType<React.SVGProps<SVGSVGElement>>> = {
  workbench: FlaskConical,
  timeline: GanttChartSquare,
  live: Play,
  trace: ListTree,
  resources: Boxes,
  deadlock: Network,
  memory: MemoryStick,
  metrics: Gauge,
  compare: GitCompareArrows,
  reports: FileText,
  docs: BookOpen,
};

const KEY_HINTS = ["1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-"];

function Rail() {
  const activeView = useWorkstation((s) => s.activeView);
  const setActiveView = useWorkstation((s) => s.setActiveView);

  return (
    <nav
      aria-label="Views"
      className="flex w-14 shrink-0 flex-col items-center gap-1 border-r border-line bg-panel py-2"
    >
      {VIEW_IDS.map((id, index) => {
        const meta = VIEW_META[id];
        const Icon = RAIL_ICONS[id];
        const active = activeView === id;
        return (
          <Tooltip key={id}>
            <TooltipTrigger asChild>
              <button
                type="button"
                onClick={() => setActiveView(id)}
                aria-label={`${meta.label} view`}
                aria-current={active ? "page" : undefined}
                className={cn(
                  "relative flex h-11 w-11 cursor-pointer items-center justify-center rounded-md transition-colors",
                  "outline-none focus-visible:ring-2 focus-visible:ring-accent",
                  active
                    ? "bg-accent/15 text-accent"
                    : "text-ink-dim hover:bg-accent/10 hover:text-ink",
                )}
              >
                <Icon className="h-[18px] w-[18px]" strokeWidth={1.75} />
                <span
                  aria-hidden
                  className="absolute bottom-0 right-1 font-mono text-[8px] leading-none"
                >
                  {meta.num}
                </span>
              </button>
            </TooltipTrigger>
            <TooltipContent
              side="right"
              className="border border-line bg-panel font-mono text-[11px] text-ink"
            >
              {meta.num} · {meta.label}
              <span className="ml-2 text-ink-dim">[{KEY_HINTS[index]}]</span>
            </TooltipContent>
          </Tooltip>
        );
      })}
    </nav>
  );
}

/* ---------------------------------------------------------------------------
 * Header
 * ------------------------------------------------------------------------- */

function statusColor(status: string | undefined, resultStatus: string | undefined): string {
  if (status === "running") return "bg-accent animate-pulse";
  if (status === "completed") return resultStatus === "stopped" ? "bg-accent" : "bg-ok";
  if (status === "failed" || status === "stopped") return "bg-bad";
  return "bg-ink-dim";
}

function Header() {
  const simId = useWorkstation((s) => s.simId);
  const simQuery = useSimulation(simId);
  const health = useHealth();
  const record = simQuery.data;

  const schedulerLabel = record
    ? record.config?.scheduler?.quantum != null
      ? `${record.scheduler} q=${record.config.scheduler.quantum}`
      : record.scheduler
    : null;

  const engineOk = health.data?.engine.available === true;

  return (
    <header className="flex h-12 shrink-0 items-center gap-3 border-b border-line bg-panel px-3">
      <div className="flex select-none items-baseline gap-0.5 font-mono text-sm font-semibold tracking-tight">
        <span className="text-ink">MicroRT</span>
        <span className="text-accent">·Lab</span>
      </div>

      <Separator orientation="vertical" className="h-5" />

      <span
        className="hidden items-center gap-1.5 rounded-sm border border-line px-1.5 py-0.5 font-mono text-[10px] uppercase tracking-widest text-ink-dim sm:inline-flex"
        title="Active scheduler"
      >
        {schedulerLabel ?? "no scheduler"}
      </span>

      <span className="flex min-w-0 items-center gap-2">
        <span
          aria-hidden
          className={cn(
            "h-2 w-2 shrink-0 rounded-full",
            record ? statusColor(record.status, record.result?.status) : "bg-line",
          )}
        />
        <span className="truncate font-mono text-xs text-ink-dim" title={record?.name ?? ""}>
          {record?.name ?? "no simulation"}
        </span>
      </span>

      <div className="ml-auto flex items-center gap-1.5">
        <span
          aria-hidden
          className={cn(
            "h-2 w-2 rounded-full",
            health.isFetching && !health.data
              ? "bg-ink-dim animate-pulse"
              : engineOk
                ? "bg-ok"
                : "bg-bad",
          )}
        />
        <span className="font-mono text-[10px] tracking-widest text-ink-dim">
          ENGINE
        </span>
        <span className="sr-only">
          {engineOk ? "Analysis engine online" : "Analysis engine unavailable"}
        </span>
      </div>
    </header>
  );
}

/* ---------------------------------------------------------------------------
 * Transport bar (global playback)
 * ------------------------------------------------------------------------- */

function TickReadout({ horizon }: { horizon: number }) {
  const cursor = useWorkstation((s) => Math.floor(s.playback.cursorTick));
  const width = Math.max(1, String(horizon).length);
  return (
    <span className="whitespace-nowrap font-mono text-xs" aria-live="off">
      <span className="text-accent">t={String(cursor).padStart(width, "0")}</span>
      <span className="text-ink-dim">/{horizon || "—"}</span>
    </span>
  );
}

function Scrubber({ horizon }: { horizon: number }) {
  const cursor = useWorkstation((s) => Math.floor(s.playback.cursorTick));
  const setCursor = useWorkstation((s) => s.setCursor);
  return (
    <input
      type="range"
      min={0}
      max={horizon}
      step={1}
      value={Math.min(cursor, horizon)}
      disabled={horizon <= 0}
      onChange={(e) => setCursor(Number(e.target.value))}
      aria-label="Time cursor"
      className="h-1.5 min-w-16 flex-1 cursor-pointer accent-accent"
    />
  );
}

function TransportBar() {
  const simId = useWorkstation((s) => s.simId);
  const playing = useWorkstation((s) => s.playback.playing);
  const speed = useWorkstation((s) => s.playback.speed);
  const horizon = useWorkstation((s) => s.horizon);
  const togglePlay = useWorkstation((s) => s.togglePlay);
  const setSpeed = useWorkstation((s) => s.setSpeed);
  const stepForward = useWorkstation((s) => s.stepForward);
  const stepBackward = useWorkstation((s) => s.stepBackward);

  const disabled = simId === null;

  return (
    <footer
      role="contentinfo"
      className="mt-auto flex h-12 shrink-0 items-center gap-2 border-t border-line bg-panel px-3"
    >
      <Button
        type="button"
        variant="outline"
        size="icon"
        className="h-9 w-9 cursor-pointer rounded-md hover:bg-accent/10 hover:text-accent"
        onClick={togglePlay}
        disabled={disabled}
        aria-label={playing ? "Pause playback" : "Play playback"}
      >
        {playing ? <Pause className="h-4 w-4" /> : <Play className="h-4 w-4" />}
      </Button>
      <Button
        type="button"
        variant="outline"
        size="icon"
        className="h-9 w-9 cursor-pointer rounded-md hover:bg-accent/10 hover:text-ink"
        onClick={stepBackward}
        disabled={disabled}
        aria-label="Step back one tick"
      >
        <ChevronLeft className="h-4 w-4" />
      </Button>
      <Button
        type="button"
        variant="outline"
        size="icon"
        className="h-9 w-9 cursor-pointer rounded-md hover:bg-accent/10 hover:text-ink"
        onClick={stepForward}
        disabled={disabled}
        aria-label="Step forward one tick"
      >
        <ChevronRight className="h-4 w-4" />
      </Button>

      <Select
        value={String(speed)}
        onValueChange={(v) => setSpeed(Number(v))}
        disabled={disabled}
      >
        <SelectTrigger
          className="h-9 w-[4.75rem] cursor-pointer rounded-md font-mono text-xs"
          aria-label="Playback speed"
        >
          <SelectValue />
        </SelectTrigger>
        <SelectContent>
          {PLAYBACK_SPEEDS.map((s) => (
            <SelectItem key={s} value={String(s)} className="font-mono text-xs">
              {s} t/s
            </SelectItem>
          ))}
        </SelectContent>
      </Select>

      <TickReadout horizon={horizon} />
      <Scrubber horizon={horizon} />

      <span className="hidden max-w-56 truncate font-mono text-xs text-ink-dim md:inline">
        {simId === null ? <span className="text-ink-dim/60">no simulation</span> : simId}
      </span>
    </footer>
  );
}

/* ---------------------------------------------------------------------------
 * View area (framer-motion switch + error boundary)
 * ------------------------------------------------------------------------- */

class ViewErrorBoundary extends React.Component<
  { children: React.ReactNode },
  { error: Error | null }
> {
  constructor(props: { children: React.ReactNode }) {
    super(props);
    this.state = { error: null };
  }

  static getDerivedStateFromError(error: Error): { error: Error | null } {
    return { error };
  }

  render() {
    if (this.state.error) {
      return (
        <div className="flex h-full items-center justify-center p-6">
          <div className="w-full max-w-md rounded-lg border border-bad/40 bg-panel p-6">
            <p className="font-mono text-[10px] tracking-widest text-bad">
              VIEW ERROR
            </p>
            <p className="mt-2 font-mono text-xs break-words text-ink-dim">
              {this.state.error.message}
            </p>
            <Button
              type="button"
              variant="outline"
              className="mt-4 cursor-pointer rounded-md hover:bg-accent/10"
              onClick={() => this.setState({ error: null })}
            >
              Retry
            </Button>
          </div>
        </div>
      );
    }
    return this.props.children;
  }
}

function ViewArea() {
  const activeView = useWorkstation((s) => s.activeView);
  const ActiveView = VIEWS[activeView];

  return (
    <ViewErrorBoundary>
      <AnimatePresence mode="wait" initial={false}>
        <motion.div
          key={activeView}
          className="h-full"
          initial={{ opacity: 0, y: 6 }}
          animate={{ opacity: 1, y: 0 }}
          exit={{ opacity: 0, y: -6 }}
          transition={{ duration: 0.15, ease: "easeOut" }}
        >
          <ActiveView />
        </motion.div>
      </AnimatePresence>
    </ViewErrorBoundary>
  );
}

/* ---------------------------------------------------------------------------
 * Global keyboard: 1-9,0,- switch views · space play/pause · ←/→ step ±1
 * ------------------------------------------------------------------------- */

function useWorkstationKeys() {
  const setActiveView = useWorkstation((s) => s.setActiveView);
  const togglePlay = useWorkstation((s) => s.togglePlay);
  const stepForward = useWorkstation((s) => s.stepForward);
  const stepBackward = useWorkstation((s) => s.stepBackward);

  React.useEffect(() => {
    const onKeyDown = (e: KeyboardEvent) => {
      if (e.metaKey || e.ctrlKey || e.altKey) return;
      const target = e.target as HTMLElement | null;
      const interactive =
        target !== null &&
        (target.tagName === "INPUT" ||
          target.tagName === "TEXTAREA" ||
          target.tagName === "SELECT" ||
          target.isContentEditable);
      if (interactive) return;

      if (e.key === " ") {
        e.preventDefault();
        togglePlay();
        return;
      }
      if (e.key === "ArrowRight") {
        e.preventDefault();
        stepForward();
        return;
      }
      if (e.key === "ArrowLeft") {
        e.preventDefault();
        stepBackward();
        return;
      }
      const index = "1234567890-".indexOf(e.key);
      if (index >= 0 && VIEW_IDS[index]) {
        setActiveView(VIEW_IDS[index]);
      }
    };
    window.addEventListener("keydown", onKeyDown);
    return () => window.removeEventListener("keydown", onKeyDown);
  }, [setActiveView, togglePlay, stepForward, stepBackward]);
}

/* ---------------------------------------------------------------------------
 * Shell
 * ------------------------------------------------------------------------- */

function useSyncHorizon() {
  const simId = useWorkstation((s) => s.simId);
  const simQuery = useSimulation(simId);
  const setHorizon = useWorkstation((s) => s.setHorizon);
  React.useEffect(() => {
    const record = simQuery.data;
    if (record?.result) {
      setHorizon(record.result.simulatedUntil);
    } else if (record && typeof record.simulatedUntil === "number") {
      setHorizon(record.simulatedUntil);
    }
  }, [simQuery.data, setHorizon]);
}

function ShellFrame() {
  usePlaybackClock();
  useWorkstationKeys();
  useSyncHorizon();

  return (
    <div className="flex h-dvh flex-col overflow-hidden bg-bg text-ink">
      <Header />
      <div className="flex min-h-0 flex-1">
        <Rail />
        <main className="min-w-0 flex-1 overflow-y-auto">
          <ViewArea />
        </main>
      </div>
      <TransportBar />
    </div>
  );
}

export function WorkstationShell() {
  return (
    <React.Suspense
      fallback={
        <div className="flex h-dvh items-center justify-center bg-bg text-ink">
          <Loader2 className="h-5 w-5 animate-spin text-accent" aria-label="Loading" />
        </div>
      }
    >
      <ShellFrame />
    </React.Suspense>
  );
}
