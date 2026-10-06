"use client";

/**
 * MicroRT-Lab — Reports view (10): report browser + generator.
 *
 * Left: the current simulation's report (markdown rendered with the
 * dependency-free MiniMarkdown renderer, CSV as a table, JSON syntax
 * highlighted) with raw toggle, download and print. Right: the stored report
 * library (filtered to the current simulation or all), click to open.
 */

import * as React from "react";
import { Download, Printer, RefreshCw } from "lucide-react";
import { toast } from "sonner";
import { useQueryClient } from "@tanstack/react-query";

import { Button } from "@/components/ui/button";
import { Skeleton } from "@/components/ui/skeleton";
import { Tabs, TabsList, TabsTrigger } from "@/components/ui/tabs";
import { cn } from "@/lib/utils";
import type { ReportFormat } from "@/lib/types";
import { formatApiError } from "@/lib/api";
import { useSimulation } from "@/hooks/useSimulation";
import {
  reportKeys,
  useReport,
  useReports,
  useSimulationReport,
  useSimulationReportJson,
} from "@/hooks/useReports";
import { MiniMarkdown } from "@/components/views/reports/MiniMarkdown";
import { CsvTable, JsonHighlight } from "@/components/views/reports/reportFormat";
import { downloadBlob, SectionPanel, ViewHeader } from "@/components/views/shared";
import { useWorkstation } from "@/store/workstation";

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
  }
}`;

function shortTime(iso: string): string {
  try {
    return new Date(iso).toLocaleString(undefined, {
      month: "short",
      day: "2-digit",
      hour: "2-digit",
      minute: "2-digit",
    });
  } catch {
    return iso.slice(0, 16);
  }
}

/* report body renderer ---------------------------------------------------- */

function ReportBody({
  format,
  content,
  raw,
}: {
  format: string;
  content: string;
  raw: boolean;
}) {
  if (raw || format === "json") {
    return format === "json" ? (
      <JsonHighlight source={content} className="max-h-[60vh]" />
    ) : (
      <pre className="max-h-[60vh] overflow-auto rounded-md border border-line bg-bg p-2 font-mono text-[10px] leading-4 text-ink-dim whitespace-pre-wrap">
        {content}
      </pre>
    );
  }
  if (format === "csv") return <CsvTable csv={content} />;
  return <MiniMarkdown source={content} />;
}

/* ---------------------------------------------------------------------- */
/* view                                                                    */
/* ---------------------------------------------------------------------- */

export function ReportsView() {
  const simId = useWorkstation((s) => s.simId);
  const simQuery = useSimulation(simId);
  const qc = useQueryClient();

  const [format, setFormat] = React.useState<ReportFormat>("markdown");
  const [raw, setRaw] = React.useState(false);
  const [scope, setScope] = React.useState<"sim" | "all">("sim");
  const [selectedReportId, setSelectedReportId] = React.useState<string | null>(null);
  const [source, setSource] = React.useState<"sim" | "stored">("sim");

  const mdQuery = useSimulationReport(simId, "markdown");
  const csvQuery = useSimulationReport(simId, "csv");
  const jsonQuery = useSimulationReportJson(simId);
  const reports = useReports(scope === "sim" && simId ? simId : null);
  const stored = useReport(selectedReportId);

  const simName = simQuery.data?.name ?? "simulation";

  const activeQuery =
    format === "markdown" ? mdQuery : format === "csv" ? csvQuery : jsonQuery;
  const simContent =
    format === "json" && jsonQuery.data
      ? JSON.stringify(jsonQuery.data, null, 2)
      : format === "markdown"
        ? mdQuery.data
        : csvQuery.data;

  const generate = async () => {
    const res = await activeQuery.refetch();
    if (res.error) {
      toast.error(formatApiError(res.error));
      return;
    }
    if (simId) void qc.invalidateQueries({ queryKey: reportKeys.forSim(simId) });
    void qc.invalidateQueries({ queryKey: reportKeys.all });
    toast.success("Report fetched from the analysis layer (stored in the library)");
  };

  const download = () => {
    if (source === "stored" && stored.data) {
      const ext = stored.data.format === "markdown" ? "md" : stored.data.format;
      downloadBlob(`micrort-report-${stored.data.id}.${ext}`, stored.data.content, "text/plain");
      return;
    }
    if (simContent !== undefined && simId) {
      const ext = format === "markdown" ? "md" : format;
      downloadBlob(`micrort-report-${simName.replace(/[^a-zA-Z0-9_-]+/g, "_")}.${ext}`, simContent, "text/plain");
    }
  };

  const shown =
    source === "stored" && stored.data
      ? { format: stored.data.format, content: stored.data.content }
      : simContent !== undefined
        ? { format, content: simContent }
        : null;

  return (
    <div className="flex h-full min-h-0 flex-col">
      <ViewHeader num="10" label="Reports" hint="markdown · csv · json exports from the analysis layer">
        <Button
          type="button"
          size="sm"
          variant="outline"
          className="h-8 cursor-pointer gap-1.5 rounded-md font-mono text-[11px] hover:bg-accent/10 disabled:opacity-50"
          onClick={download}
          disabled={!shown}
          aria-label="Download report"
        >
          <Download className="h-3.5 w-3.5" aria-hidden />
          DOWNLOAD
        </Button>
        <Button
          type="button"
          size="sm"
          variant="outline"
          className="h-8 cursor-pointer gap-1.5 rounded-md font-mono text-[11px] hover:bg-accent/10"
          onClick={() => window.print()}
          aria-label="Print report"
        >
          <Printer className="h-3.5 w-3.5" aria-hidden />
          PRINT
        </Button>
      </ViewHeader>

      <style>{PRINT_STYLE}</style>

      <div className="grid min-h-0 flex-1 grid-cols-1 gap-3 overflow-y-auto p-4 xl:grid-cols-[1fr_340px]">
        {/* report viewer */}
        <div className="flex min-w-0 flex-col gap-3">
          <Tabs value={source} onValueChange={(v) => setSource(v === "stored" ? "stored" : "sim")}>
            <TabsList className="h-8">
              <TabsTrigger value="sim" className="font-mono text-[10px]">
                CURRENT SIMULATION
              </TabsTrigger>
              <TabsTrigger
                value="stored"
                className="font-mono text-[10px] data-[state=disabled]:opacity-40"
                disabled={!selectedReportId}
              >
                STORED REPORT
              </TabsTrigger>
            </TabsList>
          </Tabs>

          {source === "sim" ? (
            <SectionPanel
              title={`REPORT · ${simName}`}
              right={
                <div className="flex items-center gap-1.5">
                  <Tabs value={format} onValueChange={(v) => setFormat(v as ReportFormat)}>
                    <TabsList className="h-7">
                      {(["markdown", "csv", "json"] as const).map((f) => (
                        <TabsTrigger key={f} value={f} className="px-2 font-mono text-[10px]">
                          {f.toUpperCase()}
                        </TabsTrigger>
                      ))}
                    </TabsList>
                  </Tabs>
                  {format !== "json" && (
                    <Button
                      type="button"
                      size="sm"
                      variant={raw ? "default" : "outline"}
                      aria-pressed={raw}
                      className="h-7 cursor-pointer rounded-md font-mono text-[10px]"
                      onClick={() => setRaw((r) => !r)}
                    >
                      RAW
                    </Button>
                  )}
                  <Button
                    type="button"
                    size="sm"
                    variant="outline"
                    className="h-7 cursor-pointer gap-1 rounded-md font-mono text-[10px] hover:bg-accent/10"
                    onClick={() => void generate()}
                    disabled={!simId || activeQuery.isRefetching}
                    aria-label="Fetch report"
                  >
                    <RefreshCw
                      className={cn("h-3 w-3", activeQuery.isRefetching && "animate-spin")}
                      aria-hidden
                    />
                    FETCH
                  </Button>
                </div>
              }
            >
              {!simId ? (
                <p className="py-4 text-center font-mono text-[10px] text-ink-dim">
                  LOAD A SIMULATION IN THE WORKBENCH TO GENERATE ITS REPORT
                </p>
              ) : activeQuery.isLoading ? (
                <Skeleton className="h-48 w-full" />
              ) : activeQuery.isError ? (
                <div className="space-y-2">
                  <p className="font-mono text-[10px] text-bad">
                    {activeQuery.error instanceof Error
                      ? activeQuery.error.message
                      : "report failed"}
                  </p>
                  <Button
                    type="button"
                    size="sm"
                    variant="outline"
                    className="h-7 cursor-pointer rounded-md font-mono text-[10px] hover:bg-accent/10"
                    onClick={() => void activeQuery.refetch()}
                  >
                    RETRY
                  </Button>
                </div>
              ) : shown ? (
                <div className="print-target">
                  <ReportBody format={shown.format} content={shown.content} raw={raw} />
                </div>
              ) : (
                <p className="py-4 text-center font-mono text-[10px] text-ink-dim">
                  PRESS FETCH TO GENERATE
                </p>
              )}
            </SectionPanel>
          ) : (
            <SectionPanel
              title={`STORED REPORT · ${stored.data?.id ?? ""}`}
              right={
                stored.data ? (
                  <span className="font-mono text-[9px] text-ink-dim">
                    {stored.data.kind} · {stored.data.refId} · {stored.data.format} ·{" "}
                    {stored.data.size} B · {shortTime(stored.data.createdAt)}
                  </span>
                ) : undefined
              }
            >
              {stored.isLoading && <Skeleton className="h-48 w-full" />}
              {stored.isError && (
                <p className="font-mono text-[10px] text-bad">
                  {stored.error instanceof Error ? stored.error.message : "load failed"}
                </p>
              )}
              {stored.data && (
                <div className="print-target">
                  <ReportBody format={stored.data.format} content={stored.data.content} raw={false} />
                </div>
              )}
            </SectionPanel>
          )}
        </div>

        {/* library */}
        <SectionPanel
          title="REPORT LIBRARY"
          right={
            <div className="flex items-center gap-1">
              <Button
                type="button"
                size="sm"
                variant={scope === "sim" ? "default" : "outline"}
                className="h-6 cursor-pointer rounded-md px-2 font-mono text-[9px]"
                onClick={() => setScope("sim")}
                disabled={!simId}
                aria-label="Show reports of the current simulation"
              >
                SIM
              </Button>
              <Button
                type="button"
                size="sm"
                variant={scope === "all" ? "default" : "outline"}
                className="h-6 cursor-pointer rounded-md px-2 font-mono text-[9px]"
                onClick={() => setScope("all")}
                aria-label="Show all reports"
              >
                ALL
              </Button>
            </div>
          }
        >
          {reports.isLoading && <Skeleton className="h-24 w-full" />}
          {reports.isError && (
            <div className="space-y-2">
              <p className="font-mono text-[10px] text-bad">
                {reports.error instanceof Error ? reports.error.message : "load failed"}
              </p>
              <Button
                type="button"
                size="sm"
                variant="outline"
                className="h-7 cursor-pointer rounded-md font-mono text-[10px] hover:bg-accent/10"
                onClick={() => void reports.refetch()}
              >
                RETRY
              </Button>
            </div>
          )}
          {reports.data && reports.data.items.length === 0 && (
            <p className="py-3 text-center font-mono text-[10px] text-ink-dim">
              {scope === "sim" && simId
                ? "NO REPORTS FOR THIS SIMULATION — FETCH ONE"
                : "THE LIBRARY IS EMPTY — FETCH A REPORT"}
            </p>
          )}
          <ul className="max-h-[60vh] space-y-1 overflow-y-auto">
            {(reports.data?.items ?? []).map((r) => (
              <li key={r.id}>
                <button
                  type="button"
                  onClick={() => {
                    setSelectedReportId(r.id);
                    setSource("stored");
                  }}
                  aria-pressed={selectedReportId === r.id}
                  className={cn(
                    "w-full cursor-pointer rounded-md border px-2.5 py-1.5 text-left transition-colors focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-accent",
                    selectedReportId === r.id
                      ? "border-accent/40 bg-accent/10"
                      : "border-line bg-bg hover:border-accent/30",
                  )}
                >
                  <span className="flex items-center gap-2">
                    <span
                      className={cn(
                        "rounded-sm border px-1 py-px font-mono text-[9px]",
                        r.kind === "simulation"
                          ? "border-accent/40 bg-accent/10 text-accent"
                          : "border-state-sleeping/40 bg-state-sleeping/10 text-state-sleeping",
                      )}
                    >
                      {r.kind}
                    </span>
                    <span className="font-mono text-[10px] text-ink">{r.refId}</span>
                    <span className="ml-auto font-mono text-[9px] uppercase text-ink-dim">
                      {r.format}
                    </span>
                  </span>
                  <span className="mt-0.5 block font-mono text-[9px] text-ink-dim">
                    {r.size} B · {shortTime(r.createdAt)}
                  </span>
                </button>
              </li>
            ))}
          </ul>
        </SectionPanel>
      </div>
    </div>
  );
}
