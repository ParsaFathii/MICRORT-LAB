/**
 * MicroRT-Lab — typed API client for the Python analysis layer.
 *
 * The FastAPI service runs on port 3031 and is reached ONLY through relative
 * URLs. Two transports carry the same URL shape:
 *   /api/v1/health?XTransformPort=3031
 *   1. through the sandbox gateway (marker → Caddy → 127.0.0.1:3031)
 *   2. directly on the Next.js dev server, where the catch-all route handler
 *      at src/app/api/v1/[...path]/route.ts proxies server-side to 3031
 *      (it strips the marker before forwarding).
 * Never absolute URLs, never other ports, never from server components.
 */

import {
  ApiError,
  type AnalysisResult,
  type ComparisonRecord,
  type ComparisonSummary,
  type CreateComparisonPayload,
  type ExperimentRecord,
  type HealthResponse,
  type ListResponse,
  type MetricsCheck,
  type ReportRecord,
  type ReportRecordWithContent,
  type SimulationRecord,
  type SimulationReportJson,
  type TraceListResponse,
} from "@/lib/types";

const GATEWAY_MARKER = "XTransformPort";
const GATEWAY_PORT = "3031";
const BASE_PATH = "/api/v1";
const TIMEOUT_MS = 15_000;

/**
 * Build the gateway URL for an API path.
 *
 * @param path API path starting with "/", may already carry a query string
 *             (e.g. "/simulations?limit=50") — the gateway marker is then
 *             appended to the existing parameters.
 * @returns relative URL like "/api/v1/simulations?limit=50&XTransformPort=3031"
 */
export function buildUrl(path: string): string {
  const queryIndex = path.indexOf("?");
  const pathname = queryIndex === -1 ? path : path.slice(0, queryIndex);
  const existing =
    queryIndex === -1 ? "" : path.slice(queryIndex + 1);
  const params = new URLSearchParams(existing);
  params.set(GATEWAY_MARKER, GATEWAY_PORT);
  return `${BASE_PATH}${pathname}?${params.toString()}`;
}

async function readErrorBody(res: Response): Promise<ApiError> {
  let message = `Request failed (${res.status} ${res.statusText || "Error"})`;
  let details: unknown[] | undefined;
  try {
    const text = await res.text();
    if (text) {
      try {
        const parsed: unknown = JSON.parse(text);
        if (typeof parsed === "object" && parsed !== null) {
          const body = parsed as Record<string, unknown>;
          if (typeof body.error === "string") {
            message = body.error;
          }
          if (Array.isArray(body.details)) {
            details = body.details;
          } else if (body.details !== undefined) {
            details = [body.details];
          }
        }
      } catch {
        message = text.slice(0, 300);
      }
    }
  } catch {
    // body unreadable — keep the generic message
  }
  return new ApiError(res.status, message, details);
}

async function request<T>(
  path: string,
  method: "GET" | "POST" | "DELETE",
  body?: unknown,
): Promise<T> {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), TIMEOUT_MS);
  let res: Response;
  try {
    res = await fetch(buildUrl(path), {
      method,
      signal: controller.signal,
      headers: body === undefined ? undefined : {
        "Content-Type": "application/json",
      },
      body: body === undefined ? undefined : JSON.stringify(body),
    });
  } catch (err) {
    if (err instanceof DOMException && err.name === "AbortError") {
      throw new ApiError(408, `Request timed out after ${TIMEOUT_MS / 1000}s (${path})`);
    }
    throw new ApiError(0, `Network error reaching the analysis service (${path})`);
  } finally {
    clearTimeout(timer);
  }

  if (!res.ok) {
    throw await readErrorBody(res);
  }
  if (res.status === 204) {
    return undefined as T;
  }
  try {
    return (await res.json()) as T;
  } catch {
    throw new ApiError(502, `Invalid JSON response from the analysis service (${path})`);
  }
}

async function requestText(path: string): Promise<string> {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), TIMEOUT_MS);
  let res: Response;
  try {
    res = await fetch(buildUrl(path), { method: "GET", signal: controller.signal });
  } catch (err) {
    if (err instanceof DOMException && err.name === "AbortError") {
      throw new ApiError(408, `Request timed out after ${TIMEOUT_MS / 1000}s (${path})`);
    }
    throw new ApiError(0, `Network error reaching the analysis service (${path})`);
  } finally {
    clearTimeout(timer);
  }
  if (!res.ok) {
    throw await readErrorBody(res);
  }
  return res.text();
}

export const api = {
  get: <T>(path: string) => request<T>(path, "GET"),
  post: <T>(path: string, body?: unknown) => request<T>(path, "POST", body ?? {}),
  del: <T>(path: string) => request<T>(path, "DELETE"),
  /** for endpoints that return raw text/csv/markdown instead of JSON */
  getText: (path: string) => requestText(path),
};

/* ---------------------------------------------------------------------------
 * Typed endpoint helpers
 * ------------------------------------------------------------------------- */

export function fetchHealth(): Promise<HealthResponse> {
  return api.get<HealthResponse>("/health");
}

export function listExperiments(limit = 50): Promise<ListResponse<ExperimentRecord>> {
  return api.get<ListResponse<ExperimentRecord>>(`/experiments?limit=${limit}`);
}

export function getExperiment(id: string): Promise<ExperimentRecord> {
  return api.get<ExperimentRecord>(`/experiments/${encodeURIComponent(id)}`);
}

/** Synchronous run — resolves with the simulation record including its result. */
export function runExperiment(id: string): Promise<SimulationRecord> {
  return api.post<SimulationRecord>(`/experiments/${encodeURIComponent(id)}/run`, {});
}

export function listSimulations(limit = 50): Promise<ListResponse<SimulationRecord>> {
  return api.get<ListResponse<SimulationRecord>>(`/simulations?limit=${limit}`);
}

export function getSimulation(id: string): Promise<SimulationRecord> {
  return api.get<SimulationRecord>(`/simulations/${encodeURIComponent(id)}`);
}

export function runSimulation(id: string): Promise<SimulationRecord> {
  return api.post<SimulationRecord>(`/simulations/${encodeURIComponent(id)}/run`, {});
}

export function createSimulation(payload: {
  name?: string;
  config: unknown;
}): Promise<SimulationRecord> {
  return api.post<SimulationRecord>("/simulations", payload);
}

export function deleteSimulation(id: string): Promise<unknown> {
  return api.del<unknown>(`/simulations/${encodeURIComponent(id)}`);
}

/* --- simulations: analysis / trace / report (stage 2) ------------------- */

export interface TraceFilterOptions {
  type?: string;
  task?: string;
  limit?: number;
  offset?: number;
}

/** GET /api/v1/simulations/{id}/trace — filtered + paginated trace events. */
export function getSimulationTrace(
  id: string,
  filters: TraceFilterOptions = {},
): Promise<TraceListResponse> {
  const params = new URLSearchParams();
  if (filters.type) params.set("type", filters.type);
  if (filters.task) params.set("task", filters.task);
  params.set("limit", String(filters.limit ?? 1000));
  params.set("offset", String(filters.offset ?? 0));
  return api.get<TraceListResponse>(
    `/simulations/${encodeURIComponent(id)}/trace?${params.toString()}`,
  );
}

/** GET /api/v1/simulations/{id}/metrics — engine vs recompute cross-check. */
export function getSimulationMetrics(id: string): Promise<MetricsCheck> {
  return api.get<MetricsCheck>(`/simulations/${encodeURIComponent(id)}/metrics`);
}

/** GET /api/v1/simulations/{id}/analysis — RT / starvation / fragmentation. */
export function getSimulationAnalysis(id: string): Promise<AnalysisResult> {
  return api.get<AnalysisResult>(`/simulations/${encodeURIComponent(id)}/analysis`);
}

/**
 * GET /api/v1/simulations/{id}/report?format=markdown|csv — raw text body
 * (the endpoint also persists a report record in the library).
 */
export function getSimulationReport(
  id: string,
  format: "markdown" | "csv",
): Promise<string> {
  return api.getText(`/simulations/${encodeURIComponent(id)}/report?format=${format}`);
}

/** GET /api/v1/simulations/{id}/report?format=json — full export document. */
export function getSimulationReportJson(id: string): Promise<SimulationReportJson> {
  return api.get<SimulationReportJson>(
    `/simulations/${encodeURIComponent(id)}/report?format=json`,
  );
}

/* --- comparisons (stage 2) ------------------------------------------------ */

export function listComparisons(limit = 50): Promise<ListResponse<ComparisonSummary>> {
  return api.get<ListResponse<ComparisonSummary>>(`/comparisons?limit=${limit}`);
}

export function getComparison(id: string): Promise<ComparisonRecord> {
  return api.get<ComparisonRecord>(`/comparisons/${encodeURIComponent(id)}`);
}

/**
 * POST /api/v1/comparisons — runs every variant (deep-merged over the base
 * config) and returns the full comparison record with results.
 */
export function createComparison(
  payload: CreateComparisonPayload,
): Promise<ComparisonRecord> {
  return api.post<ComparisonRecord>("/comparisons", payload);
}

/** GET /api/v1/comparisons/{id}/report — markdown report (raw text body). */
export function getComparisonReport(id: string): Promise<string> {
  return api.getText(`/comparisons/${encodeURIComponent(id)}/report`);
}

/* --- stored reports (stage 2) ---------------------------------------------- */

export function listReports(
  simulationId?: string,
  limit = 50,
): Promise<ListResponse<ReportRecord>> {
  const params = new URLSearchParams({ limit: String(limit) });
  if (simulationId) params.set("simulationId", simulationId);
  return api.get<ListResponse<ReportRecord>>(`/reports?${params.toString()}`);
}

export function getReport(id: string): Promise<ReportRecordWithContent> {
  return api.get<ReportRecordWithContent>(`/reports/${encodeURIComponent(id)}`);
}

/** Format an ApiError for a toast message (message + up to 3 detail lines). */
export function formatApiError(err: unknown): string {
  if (err instanceof ApiError) {
    const detailLines =
      err.details?.slice(0, 3).map((d) => (typeof d === "string" ? d : JSON.stringify(d))) ?? [];
    return [err.message, ...detailLines].join("\n");
  }
  if (err instanceof Error) return err.message;
  return "Unexpected error";
}
