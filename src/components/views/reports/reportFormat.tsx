"use client";

/**
 * MicroRT-Lab — regex-based JSON syntax highlighter (no dependencies) and a
 * tiny quoted-CSV parser used by the Reports view.
 */

import * as React from "react";

import { cn } from "@/lib/utils";

const JSON_TOKEN_RE =
  /("(?:\\.|[^"\\])*")(\s*:)|("(?:\\.|[^"\\])*")|\b(true|false|null)\b|(-?\b\d+(?:\.\d+)?(?:[eE][+-]?\d+)?\b)/g;

function highlightJson(src: string): React.ReactNode[] {
  const nodes: React.ReactNode[] = [];
  let last = 0;
  let m: RegExpExecArray | null;
  const re = new RegExp(JSON_TOKEN_RE);
  let key = 0;
  const push = (text: string, className?: string) => {
    nodes.push(
      className ? (
        <span key={key++} className={className}>
          {text}
        </span>
      ) : (
        text
      ),
    );
  };
  while ((m = re.exec(src)) !== null) {
    if (m.index > last) push(src.slice(last, m.index));
    if (m[1] !== undefined) {
      // object key
      push(m[1], "text-accent");
      push(m[2] ?? "", "");
    } else if (m[3] !== undefined) {
      push(m[3], "text-state-waiting");
    } else if (m[4] !== undefined) {
      push(m[4], "text-ctx");
    } else if (m[5] !== undefined) {
      push(m[5], "text-state-sleeping");
    }
    last = m.index + m[0].length;
  }
  if (last < src.length) push(src.slice(last));
  return nodes;
}

export function JsonHighlight({ source, className }: { source: string; className?: string }) {
  const nodes = React.useMemo(() => highlightJson(source), [source]);
  return (
    <pre
      className={cn(
        "overflow-auto rounded-md border border-line bg-bg p-2 font-mono text-[10px] leading-4 text-ink-dim",
        className,
      )}
      aria-label="JSON content"
    >
      {nodes}
    </pre>
  );
}

/* CSV ---------------------------------------------------------------------- */

/** parse CSV with optional double-quoted fields ("" escapes a quote) */
export function parseCsv(text: string): string[][] {
  const rows: string[][] = [];
  let row: string[] = [];
  let field = "";
  let inQuotes = false;
  for (let i = 0; i < text.length; i++) {
    const ch = text[i];
    if (inQuotes) {
      if (ch === '"') {
        if (text[i + 1] === '"') {
          field += '"';
          i++;
        } else {
          inQuotes = false;
        }
      } else {
        field += ch;
      }
    } else if (ch === '"') {
      inQuotes = true;
    } else if (ch === ",") {
      row.push(field);
      field = "";
    } else if (ch === "\n") {
      row.push(field);
      field = "";
      if (row.some((c) => c !== "")) rows.push(row);
      row = [];
    } else if (ch !== "\r") {
      field += ch;
    }
  }
  row.push(field);
  if (row.some((c) => c !== "")) rows.push(row);
  return rows;
}

export function CsvTable({ csv, className }: { csv: string; className?: string }) {
  const rows = React.useMemo(() => parseCsv(csv), [csv]);
  const [header, ...body] = rows;
  if (!header) return null;
  return (
    <div className={cn("overflow-x-auto", className)}>
      <table className="w-full border-collapse font-mono text-[11px]">
        <thead>
          <tr className="text-left">
            {header.map((h, i) => (
              <th
                key={i}
                className="border-b border-line px-2 py-1 text-[9px] font-normal tracking-widest whitespace-nowrap text-ink-dim"
              >
                {h}
              </th>
            ))}
          </tr>
        </thead>
        <tbody>
          {body.map((r, ri) => (
            <tr key={ri} className="border-b border-line/40">
              {header.map((_, ci) => (
                <td key={ci} className="px-2 py-1 whitespace-nowrap text-ink tabular-nums">
                  {r[ci] ?? ""}
                </td>
              ))}
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}
