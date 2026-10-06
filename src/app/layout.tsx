import type { Metadata } from "next";

import { WorkstationProviders } from "@/components/workstation/WorkstationShell";

import "./globals.css";

export const metadata: Metadata = {
  title: "MicroRT Lab — Deterministic RT Scheduling Laboratory",
  description:
    "Deterministic real-time OS and scheduling laboratory: run canonical scheduling experiments (FIFO, RR, priority, EDF, RM, SJF/SRTF), inspect Gantt timelines, traces, deadlocks, memory and metrics from a discrete-event engine.",
  applicationName: "MicroRT Lab",
  authors: [{ name: "Parsa Fathi" }],
  keywords: [
    "real-time systems",
    "scheduling",
    "RTOS",
    "EDF",
    "rate-monotonic",
    "round-robin",
    "Gantt",
    "simulation",
  ],
};

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    <html lang="en" suppressHydrationWarning>
      <body className="antialiased">
        <WorkstationProviders>{children}</WorkstationProviders>
      </body>
    </html>
  );
}
