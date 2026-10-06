"use client";

/**
 * MicroRT-Lab — comparison queries/mutations (TanStack Query).
 *
 * Query keys follow the workstation convention:
 *   ['comparisons']            — history list
 *   ['comparison', id]         — full comparison record
 *   ['comparison-report', id]  — markdown report (fetched on demand)
 */

import { useMutation, useQuery, useQueryClient, type UseQueryResult } from "@tanstack/react-query";

import {
  createComparison,
  getComparison,
  getComparisonReport,
  listComparisons,
} from "@/lib/api";
import type {
  ComparisonRecord,
  ComparisonSummary,
  CreateComparisonPayload,
  ListResponse,
} from "@/lib/types";

export const comparisonKeys = {
  list: ["comparisons"] as const,
  detail: (id: string) => ["comparison", id] as const,
  report: (id: string) => ["comparison-report", id] as const,
};

const STALE = {
  list: 15 * 1000,
  detail: 30 * 1000,
};

export function useComparisons(limit = 50): UseQueryResult<ListResponse<ComparisonSummary>> {
  return useQuery({
    queryKey: comparisonKeys.list,
    queryFn: () => listComparisons(limit),
    staleTime: STALE.list,
    refetchOnWindowFocus: false,
  });
}

export function useComparison(id: string | null): UseQueryResult<ComparisonRecord> {
  return useQuery({
    queryKey: id ? comparisonKeys.detail(id) : ["comparison", "none"],
    queryFn: () => getComparison(id as string),
    enabled: id !== null,
    staleTime: STALE.detail,
    refetchOnWindowFocus: false,
  });
}

export function useComparisonReport(id: string | null, enabled: boolean) {
  return useQuery({
    queryKey: id ? comparisonKeys.report(id) : ["comparison-report", "none"],
    queryFn: () => getComparisonReport(id as string),
    enabled: enabled && id !== null,
    staleTime: 5 * 60 * 1000,
    refetchOnWindowFocus: false,
  });
}

export function useCreateComparison() {
  const qc = useQueryClient();
  return useMutation({
    mutationFn: (payload: CreateComparisonPayload) => createComparison(payload),
    onSuccess: (record: ComparisonRecord) => {
      qc.setQueryData(comparisonKeys.detail(record.id), record);
      void qc.invalidateQueries({ queryKey: comparisonKeys.list });
    },
  });
}
