/**
 * MicroRT-Lab — typed API client for the Python analysis layer.
 *
 * The FastAPI service runs on port 3031 and is reached ONLY through the
 * sandbox gateway using RELATIVE URLs with the marker query parameter:
 *   /api/v1/health?XTransformPort=3031
 * Never absolute URLs, never other ports, never from server components.
 */

import {
  ApiError,
  type ExperimentRecord,
  type HealthResponse,
  type ListResponse,
  type SimulationRecord,
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

export const api = {
  get: <T>(path: string) => request<T>(path, "GET"),
  post: <T>(path: string, body?: unknown) => request<T>(path, "POST", body ?? {}),
  del: <T>(path: string) => request<T>(path, "DELETE"),
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
