"use client";

/**
 * MicroRT-Lab — report queries (TanStack Query).
 *
 * Query keys:
 *   ['reports', simId]                      — stored reports of one simulation
 *   ['reports', 'all']                      — whole report library
 *   ['report', id]                          — one stored report + content
 *   ['sim-report', simId, format]           — generated report for the active sim
 *   ['sim-analysis', simId] / ['sim-metrics', simId] — analysis endpoints
 */

import { useQuery, type UseQueryResult } from "@tanstack/react-query";

import {
  getReport,
  getSimulationAnalysis,
  getSimulationMetrics,
  getSimulationReport,
  getSimulationReportJson,
  listReports,
} from "@/lib/api";
import type {
  AnalysisResult,
  ListResponse,
  MetricsCheck,
  ReportRecord,
  ReportRecordWithContent,
  SimulationReportJson,
} from "@/lib/types";

export const reportKeys = {
  forSim: (simId: string) => ["reports", simId] as const,
  all: ["reports", "all"] as const,
  detail: (id: string) => ["report", id] as const,
  simReport: (simId: string, format: string) => ["sim-report", simId, format] as const,
  simAnalysis: (simId: string) => ["sim-analysis", simId] as const,
  simMetrics: (simId: string) => ["sim-metrics", simId] as const,
};

const STALE = { list: 15 * 1000, detail: 60 * 1000, analysis: 30 * 1000 };

/** Stored reports of one simulation (undefined → whole library). */
export function useReports(
  simId: string | null,
): UseQueryResult<ListResponse<ReportRecord>> {
  return useQuery({
    queryKey: simId ? reportKeys.forSim(simId) : reportKeys.all,
    queryFn: () => listReports(simId ?? undefined),
    staleTime: STALE.list,
    refetchOnWindowFocus: false,
  });
}

export function useReport(id: string | null): UseQueryResult<ReportRecordWithContent> {
  return useQuery({
    queryKey: id ? reportKeys.detail(id) : ["report", "none"],
    queryFn: () => getReport(id as string),
    enabled: id !== null,
    staleTime: STALE.detail,
    refetchOnWindowFocus: false,
  });
}

/** Generated report of the active simulation (markdown/csv text body). */
export function useSimulationReport(
  simId: string | null,
  format: "markdown" | "csv",
): UseQueryResult<string> {
  return useQuery({
    queryKey: simId ? reportKeys.simReport(simId, format) : ["sim-report", "none", format],
    queryFn: () => getSimulationReport(simId as string, format),
    enabled: simId !== null,
    staleTime: STALE.detail,
    refetchOnWindowFocus: false,
    retry: 1,
  });
}

/** Generated JSON export of the active simulation. */
export function useSimulationReportJson(
  simId: string | null,
): UseQueryResult<SimulationReportJson> {
  return useQuery({
    queryKey: simId ? ["sim-report", simId, "json"] : ["sim-report", "none", "json"],
    queryFn: () => getSimulationReportJson(simId as string),
    enabled: simId !== null,
    staleTime: STALE.detail,
    refetchOnWindowFocus: false,
    retry: 1,
  });
}

export function useSimulationAnalysis(simId: string | null): UseQueryResult<AnalysisResult> {
  return useQuery({
    queryKey: simId ? reportKeys.simAnalysis(simId) : ["sim-analysis", "none"],
    queryFn: () => getSimulationAnalysis(simId as string),
    enabled: simId !== null,
    staleTime: STALE.analysis,
    refetchOnWindowFocus: false,
    retry: 1,
  });
}

export function useSimulationMetricsCheck(simId: string | null): UseQueryResult<MetricsCheck> {
  return useQuery({
    queryKey: simId ? reportKeys.simMetrics(simId) : ["sim-metrics", "none"],
    queryFn: () => getSimulationMetrics(simId as string),
    enabled: simId !== null,
    staleTime: STALE.analysis,
    refetchOnWindowFocus: false,
    retry: 1,
  });
}
