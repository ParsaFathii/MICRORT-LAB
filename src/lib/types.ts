/**
 * MicroRT-Lab — TypeScript mirrors of docs/spec/SIMULATION_SCHEMA.md.
 * Every field the engine/Python layers emit is represented; absent values
 * are `null` (never undefined) per the frozen data contract.
 */

/* ---------------------------------------------------------------------------
 * Experiment configuration (micrort-config/1)
 * ------------------------------------------------------------------------- */

export type TaskKind = "aperiodic" | "periodic" | "sporadic";

export interface StepCfg {
  op: string;
  /** cpu/io/sleep burst length */
  d?: number;
  res?: string;
  dev?: string;
  msg?: number;
  mask?: number;
  mode?: string;
  size?: number;
  tag?: string;
}

export interface TaskCfg {
  id: string;
  name?: string;
  kind?: TaskKind;
  priority?: number;
  arrival?: number;
  period?: number;
  relativeDeadline?: number;
  jitter?: number;
  releases?: number[];
  steps: StepCfg[];
}

export type ResourceType = "mutex" | "sem" | "msgq" | "evflags";

export interface ResourceCfg {
  id: string;
  type: ResourceType;
  protocol?: string;
  initial?: number;
  max?: number;
  capacity?: number;
}

export interface MemoryCfg {
  model: "region" | "pool";
  total: number;
  policy?: string;
  blockSize?: number;
  blockCount?: number;
}

export interface AgingCfg {
  interval: number;
  cap: number;
}

export interface SchedulerCfg {
  type: string;
  quantum?: number | null;
}

export interface ExperimentConfig {
  schema: string;
  name: string;
  description?: string;
  seed?: number;
  duration: number;
  cpus?: number;
  contextSwitchCost?: number;
  scheduler: SchedulerCfg;
  aging?: AgingCfg;
  memory?: MemoryCfg;
  tasks: TaskCfg[];
  resources?: ResourceCfg[];
}

/* ---------------------------------------------------------------------------
 * Trace / gantt
 * ------------------------------------------------------------------------- */

export type TraceEventType =
  | "SIM_START"
  | "SIM_END"
  | "TASK_ARRIVAL"
  | "JOB_RELEASE"
  | "DISPATCH"
  | "PREEMPT"
  | "QUANTUM_EXPIRE"
  | "CONTEXT_SWITCH"
  | "CPU_START"
  | "CPU_END"
  | "IO_START"
  | "IO_END"
  | "SLEEP_START"
  | "SLEEP_END"
  | "LOCK_ACQUIRE"
  | "LOCK_BLOCK"
  | "LOCK_RELEASE"
  | "LOCK_INHERIT"
  | "LOCK_UNINHERIT"
  | "SEM_WAIT"
  | "SEM_SIGNAL"
  | "MSG_SEND"
  | "MSG_RECV"
  | "EV_WAIT"
  | "EV_SET"
  | "MEM_ALLOC"
  | "MEM_FREE"
  | "DEADLOCK"
  | "DEADLINE_MISS"
  | "TASK_COMPLETE"
  | "AGING_BOOST"
  | "TIMER_EXPIRE";

export type TaskState =
  | "READY"
  | "RUNNING"
  | "BLOCKED"
  | "WAITING"
  | "SLEEPING"
  | "TERMINATED";

export interface TraceEvent {
  seq: number;
  t: number;
  type: TraceEventType;
  task: string | null;
  res: string | null;
  from: TaskState | null;
  to: TaskState | null;
  dur: number | null;
  detail: Record<string, unknown>;
}

export interface GanttSeg {
  task: string;
  t0: number;
  t1: number;
  state: TaskState;
  note: string | null;
}

/* ---------------------------------------------------------------------------
 * Result document (micrort-result/1)
 * ------------------------------------------------------------------------- */

export interface TaskResult {
  id: string;
  name: string;
  kind: TaskKind | string;
  priority: number;
  arrival: number;
  period?: number | null;
  relativeDeadline?: number | null;
  jobsReleased: number;
  jobsCompleted: number;
  deadlineMisses: number;
  cpuTime: number;
  readyWaitTotal: number;
  blockedTotal: number;
  waitingTotal: number;
  sleepingTotal: number;
  firstStart: number;
  lastFinish: number;
  responseTimes: number[];
  turnaroundTimes: number[];
  waitTimes: number[];
  starved: boolean;
}

export interface Metrics {
  avgWaiting: number;
  avgTurnaround: number;
  avgResponse: number;
  throughput: number;
  completedJobs: number;
  releasedJobs: number;
  completionRate: number;
  cpuUtilization: number;
  idleTime: number;
  ctxSwitches: number;
  preemptions: number;
  ctxOverheadTime: number;
  avgQueueLen: number;
  maxQueueLen: number;
  deadlineMisses: number;
  blockedTimeTotal: number;
  memoryUtilizationAvg: number | null;
  memoryPeak: number | null;
  fragmentationAvg: number | null;
  fragmentationMax: number | null;
  allocFailures: number | null;
  totalEvents: number;
}

export interface ResourceResult {
  id: string;
  type: ResourceType | string;
  finalOwner: string | null;
  acquisitions: number;
  contentions: number;
  holderAtEnd: string | null;
  queueDepth: number;
}

export interface DeadlockRec {
  t: number;
  cycle: string[];
  tasks: string[];
  resources: string[];
}

export interface MemoryEvent {
  t: number;
  op: string;
  task: string;
  tag?: string;
  size?: number;
  result?: string;
  offset?: number;
}

export interface MemoryBlock {
  offset: number;
  size: number;
  owner: string | null;
  tag?: string | null;
}

export interface FragPoint {
  t: number;
  used: number;
  free: number;
  largest: number;
  frag: number;
}

export interface MemoryResult {
  model: string;
  total: number;
  policy?: string | null;
  events: MemoryEvent[];
  finalLayout: MemoryBlock[];
  failures: MemoryEvent[];
  leaks: MemoryBlock[];
  peakUsage: number;
  fragSeries: FragPoint[];
}

export interface Anomaly {
  t: number;
  type: string;
  tasks?: string[];
  resources?: string[];
  detail?: string;
}

export interface SchedNote {
  t: number;
  chosen: string;
  reason: string;
}

export interface SimulationResult {
  schema: string;
  config: ExperimentConfig;
  configHash: string;
  seed: number;
  status: "completed" | "stopped";
  simulatedUntil: number;
  wallMicros: number;
  scheduler: SchedulerCfg;
  tasks: TaskResult[];
  metrics: Metrics;
  trace: TraceEvent[];
  gantt: GanttSeg[];
  resources: ResourceResult[];
  deadlocks: DeadlockRec[];
  memory: MemoryResult | null;
  anomalies: Anomaly[];
  schedulerNotes: SchedNote[];
}

/* ---------------------------------------------------------------------------
 * API records (Python analysis layer — flat camelCase)
 * ------------------------------------------------------------------------- */

export interface ExperimentRecord {
  id: string;
  name: string;
  description: string;
  source: "builtin" | "custom";
  config: ExperimentConfig;
  scheduler?: string;
  taskCount?: number;
  duration?: number;
}

export type SimulationStatus =
  | "created"
  | "running"
  | "completed"
  | "failed"
  | "stopped";

export interface SimulationRecord {
  id: string;
  name: string;
  status: SimulationStatus;
  config: ExperimentConfig;
  result: SimulationResult | null;
  error: string | null;
  createdAt: string;
  startedAt?: string | null;
  finishedAt?: string | null;
  scheduler: string;
  taskCount: number;
  duration: number;
  configHash?: string | null;
  simulatedUntil?: number | null;
  /** list summaries only */
  resultStatus?: string | null;
  completedJobs?: number | null;
  deadlineMisses?: number | null;
  totalEvents?: number | null;
}

export interface ListResponse<T> {
  items: T[];
  total: number;
  limit: number;
  offset: number;
}

export interface EngineInfo {
  config: string;
  result: string;
  schedulers: string[];
  steps: string[];
}

export interface HealthResponse {
  status: string;
  engine: {
    available: boolean;
    binary: string | null;
    info?: EngineInfo | null;
    cachedForSeconds?: number;
  };
  counts?: Record<string, number>;
}

/* ---------------------------------------------------------------------------
 * Errors
 * ------------------------------------------------------------------------- */

export class ApiError extends Error {
  status: number;
  details?: unknown[];

  constructor(status: number, message: string, details?: unknown[]) {
    super(message);
    this.name = "ApiError";
    this.status = status;
    this.details = details;
  }
}
