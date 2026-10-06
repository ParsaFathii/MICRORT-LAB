"use client";

import { WorkstationShell } from "@/components/workstation/WorkstationShell";

/**
 * MicroRT-Lab is a single-route, client-side data application.
 * Every "screen" is a view switched through the workstation store — there is
 * no routing beyond this page.
 */
export default function Page() {
  return <WorkstationShell />;
}
