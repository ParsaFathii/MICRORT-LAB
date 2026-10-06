/**
 * MicroRT-Lab — TanStack Query wrappers over the analysis API.
 *
 * Query keys follow docs/spec/FRONTEND_ARCHITECTURE.md:
 *   ['simulations'] · ['sim', id] · ['experiments'] · ['health']
 * (['comparison', id] and ['reports', simId] are added by stage-2 views.)
 */

import {
  useMutation,
  useQuery,
  useQueryClient,
  type UseQueryResult,
} from "@tanstack/react-query";

import {
  createSimulation,
  deleteSimulation,
  fetchHealth,
  listExperiments,
  listSimulations,
  runExperiment,
  runSimulation,
  getSimulation,
} from "@/lib/api";
import type {
  ExperimentRecord,
  HealthResponse,
  ListResponse,
  SimulationRecord,
} from "@/lib/types";

export const queryKeys = {
  health: ["health"] as const,
  experiments: ["experiments"] as const,
  simulations: ["simulations"] as const,
  sim: (id: string) => ["sim", id] as const,
};

const STALE = {
  experiments: 5 * 60 * 1000,
  simulations: 15 * 1000,
  sim: 5 * 1000,
  health: 30 * 1000,
};

export function useHealth(): UseQueryResult<HealthResponse> {
  return useQuery({
    queryKey: queryKeys.health,
    queryFn: fetchHealth,
    staleTime: STALE.health,
    refetchInterval: 30 * 1000,
    refetchOnWindowFocus: false,
    retry: 1,
  });
}

export function useExperiments(limit = 50): UseQueryResult<ListResponse<ExperimentRecord>> {
  return useQuery({
    queryKey: queryKeys.experiments,
    queryFn: () => listExperiments(limit),
    staleTime: STALE.experiments,
    refetchOnWindowFocus: false,
  });
}

export function useSimulations(limit = 50): UseQueryResult<ListResponse<SimulationRecord>> {
  return useQuery({
    queryKey: queryKeys.simulations,
    queryFn: () => listSimulations(limit),
    staleTime: STALE.simulations,
    refetchOnWindowFocus: false,
  });
}

/**
 * Full simulation record (config + result document). Pass `null` when no
 * simulation is loaded — the query is disabled and returns idle state.
 */
export function useSimulation(id: string | null): UseQueryResult<SimulationRecord> {
  const query = useQuery({
    queryKey: id ? queryKeys.sim(id) : ["sim", "none"],
    queryFn: () => getSimulation(id as string),
    enabled: id !== null,
    staleTime: STALE.sim,
    refetchOnWindowFocus: false,
    refetchInterval: (query) =>
      query.state.data?.status === "running" ? 1500 : false,
  });
  return query;
}

/* ---------------------------------------------------------------------------
 * Mutations (invalidate the list/detail queries after runs)
 * ------------------------------------------------------------------------- */

export function useRunExperiment() {
  const qc = useQueryClient();
  return useMutation({
    mutationFn: (experimentId: string) => runExperiment(experimentId),
    onSuccess: (record: SimulationRecord) => {
      qc.setQueryData(queryKeys.sim(record.id), record);
      void qc.invalidateQueries({ queryKey: queryKeys.simulations });
    },
  });
}

export function useRunSimulation() {
  const qc = useQueryClient();
  return useMutation({
    mutationFn: (simulationId: string) => runSimulation(simulationId),
    onSuccess: (record: SimulationRecord) => {
      qc.setQueryData(queryKeys.sim(record.id), record);
      void qc.invalidateQueries({ queryKey: queryKeys.simulations });
    },
  });
}

export function useCreateSimulation() {
  const qc = useQueryClient();
  return useMutation({
    mutationFn: (payload: { name?: string; config: unknown }) =>
      createSimulation(payload),
    onSuccess: (record: SimulationRecord) => {
      qc.setQueryData(queryKeys.sim(record.id), record);
      void qc.invalidateQueries({ queryKey: queryKeys.simulations });
    },
  });
}

export function useDeleteSimulation() {
  const qc = useQueryClient();
  return useMutation({
    mutationFn: (simulationId: string) => deleteSimulation(simulationId),
    onSuccess: (_data, simulationId) => {
      qc.removeQueries({ queryKey: queryKeys.sim(simulationId) });
      void qc.invalidateQueries({ queryKey: queryKeys.simulations });
    },
  });
}
