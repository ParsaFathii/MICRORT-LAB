"use client";

/**
 * MicroRT-Lab — minimal markdown renderer (no GFM plugin dependency).
 *
 * Supports the subset the analysis layer emits in its reports: ATX headings,
 * pipe tables (with the | --- | separator row), fenced code blocks, unordered
 * lists, blockquotes, horizontal rules, bold / italic / inline code / links.
 */

import * as React from "react";

import { cn } from "@/lib/utils";

/* inline: **bold** · *italic* · `code` · [text](url) ---------------------- */

const INLINE_RE = /(\*\*[^*]+\*\*|\*[^*\s][^*]*\*|`[^`]+`|\[[^\]]+\]\([^)\s]+\))/g;

function renderInline(text: string, keyPrefix: string): React.ReactNode[] {
  const nodes: React.ReactNode[] = [];
  let last = 0;
  let m: RegExpExecArray | null;
  const re = new RegExp(INLINE_RE);
  let i = 0;
  while ((m = re.exec(text)) !== null) {
    if (m.index > last) nodes.push(text.slice(last, m.index));
    const tok = m[0];
    const key = `${keyPrefix}-${i++}`;
    if (tok.startsWith("**")) {
      nodes.push(
        <strong key={key} className="font-semibold text-ink">
          {tok.slice(2, -2)}
        </strong>,
      );
    } else if (tok.startsWith("`")) {
      nodes.push(
        <code
          key={key}
          className="rounded-sm border border-line bg-bg px-1 font-mono text-[0.85em] text-accent"
        >
          {tok.slice(1, -1)}
        </code>,
      );
    } else if (tok.startsWith("[")) {
      const mm = /^\[([^\]]+)\]\(([^)\s]+)\)$/.exec(tok);
      if (mm) {
        nodes.push(
          <a
            key={key}
            href={mm[2]}
            target="_blank"
            rel="noopener noreferrer"
            className="text-accent underline decoration-dotted hover:text-accent/80"
          >
            {mm[1]}
          </a>,
        );
      } else nodes.push(tok);
    } else {
      nodes.push(
        <em key={key} className="italic text-ink">
          {tok.slice(1, -1)}
        </em>,
      );
    }
    last = m.index + tok.length;
  }
  if (last < text.length) nodes.push(text.slice(last));
  return nodes;
}

/* split a table row like "| a | b |" into cells --------------------------- */

function splitRow(line: string): string[] {
  return line
    .replace(/^\s*\|/, "")
    .replace(/\|\s*$/, "")
    .split("|")
    .map((c) => c.trim());
}

const SEP_RE = /^\s*\|?\s*:?-{2,}:?\s*(\|\s*:?-{2,}:?\s*)*\|?\s*$/;

/* block parsing ----------------------------------------------------------- */

type Block =
  | { kind: "heading"; level: number; text: string }
  | { kind: "para"; text: string }
  | { kind: "code"; lines: string[] }
  | { kind: "table"; header: string[]; rows: string[][] }
  | { kind: "list"; items: string[]; ordered: boolean }
  | { kind: "quote"; lines: string[] }
  | { kind: "hr" };

function parseBlocks(source: string): Block[] {
  const lines = source.split(/\r?\n/);
  const blocks: Block[] = [];
  let i = 0;
  while (i < lines.length) {
    const line = lines[i];
    if (line.trim() === "") {
      i++;
      continue;
    }
    // fenced code
    if (line.trim().startsWith("```")) {
      const code: string[] = [];
      i++;
      while (i < lines.length && !lines[i].trim().startsWith("```")) {
        code.push(lines[i]);
        i++;
      }
      i++; // closing fence
      blocks.push({ kind: "code", lines: code });
      continue;
    }
    // heading
    const h = /^(#{1,6})\s+(.*)$/.exec(line);
    if (h) {
      blocks.push({ kind: "heading", level: h[1].length, text: h[2] });
      i++;
      continue;
    }
    // hr
    if (/^\s*(-{3,}|\*{3,}|_{3,})\s*$/.test(line)) {
      blocks.push({ kind: "hr" });
      i++;
      continue;
    }
    // table
    if (line.includes("|") && i + 1 < lines.length && SEP_RE.test(lines[i + 1])) {
      const header = splitRow(line);
      i += 2;
      const rows: string[][] = [];
      while (i < lines.length && lines[i].includes("|") && lines[i].trim() !== "") {
        rows.push(splitRow(lines[i]));
        i++;
      }
      blocks.push({ kind: "table", header, rows });
      continue;
    }
    // list
    if (/^\s*[-*]\s+/.test(line)) {
      const items: string[] = [];
      while (i < lines.length && /^\s*[-*]\s+/.test(lines[i])) {
        items.push(lines[i].replace(/^\s*[-*]\s+/, ""));
        i++;
      }
      blocks.push({ kind: "list", items, ordered: false });
      continue;
    }
    if (/^\s*\d+\.\s+/.test(line)) {
      const items: string[] = [];
      while (i < lines.length && /^\s*\d+\.\s+/.test(lines[i])) {
        items.push(lines[i].replace(/^\s*\d+\.\s+/, ""));
        i++;
      }
      blocks.push({ kind: "list", items, ordered: true });
      continue;
    }
    // blockquote
    if (/^\s*>\s?/.test(line)) {
      const quote: string[] = [];
      while (i < lines.length && /^\s*>\s?/.test(lines[i])) {
        quote.push(lines[i].replace(/^\s*>\s?/, ""));
        i++;
      }
      blocks.push({ kind: "quote", lines: quote });
      continue;
    }
    // paragraph: consecutive plain lines
    const para: string[] = [line];
    i++;
    while (
      i < lines.length &&
      lines[i].trim() !== "" &&
      !/^(#{1,6})\s+/.test(lines[i]) &&
      !lines[i].trim().startsWith("```") &&
      !/^\s*[-*]\s+/.test(lines[i]) &&
      !/^\s*\d+\.\s+/.test(lines[i]) &&
      !/^\s*>\s?/.test(lines[i]) &&
      !SEP_RE.test(lines[i])
    ) {
      para.push(lines[i]);
      i++;
    }
    blocks.push({ kind: "para", text: para.join(" ") });
  }
  return blocks;
}

/* renderer ---------------------------------------------------------------- */

const HEADING_CLASS: Record<number, string> = {
  1: "mt-4 text-base font-semibold text-ink",
  2: "mt-4 text-sm font-semibold text-accent",
  3: "mt-3 text-xs font-semibold text-ink",
  4: "mt-2 text-[11px] font-semibold text-ink-dim",
  5: "mt-2 text-[11px] font-semibold text-ink-dim",
  6: "mt-2 text-[11px] font-semibold text-ink-dim",
};

export function MiniMarkdown({ source, className }: { source: string; className?: string }) {
  const blocks = React.useMemo(() => parseBlocks(source), [source]);
  return (
    <div className={cn("prose-instrument max-w-none", className)}>
      {blocks.map((block, bi) => {
        switch (block.kind) {
          case "heading": {
            const Tag = (`h${Math.min(block.level + 1, 6)}` as unknown) as "h2";
            return (
              <Tag key={bi} className={HEADING_CLASS[block.level] ?? HEADING_CLASS[6]}>
                {renderInline(block.text, `h${bi}`)}
              </Tag>
            );
          }
          case "para":
            return (
              <p key={bi} className="mt-2 text-xs leading-5 text-ink-dim">
                {renderInline(block.text, `p${bi}`)}
              </p>
            );
          case "code":
            return (
              <pre
                key={bi}
                className="mt-2 overflow-x-auto rounded-md border border-line bg-bg p-2 font-mono text-[10px] leading-4 text-ink-dim"
              >
                {block.lines.join("\n")}
              </pre>
            );
          case "table":
            return (
              <div key={bi} className="mt-2 overflow-x-auto">
                <table className="w-full border-collapse font-mono text-[10px]">
                  <thead>
                    <tr className="text-left">
                      {block.header.map((cell, ci) => (
                        <th
                          key={ci}
                          className="border-b border-line px-2 py-1 text-[9px] font-normal tracking-widest text-ink-dim"
                        >
                          {renderInline(cell, `th${bi}-${ci}`)}
                        </th>
                      ))}
                    </tr>
                  </thead>
                  <tbody>
                    {block.rows.map((row, ri) => (
                      <tr key={ri} className="border-b border-line/40">
                        {row.map((cell, ci) => (
                          <td key={ci} className="px-2 py-1 text-ink">
                            {renderInline(cell, `td${bi}-${ri}-${ci}`)}
                          </td>
                        ))}
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>
            );
          case "list":
            return React.createElement(
              block.ordered ? "ol" : "ul",
              {
                key: bi,
                className: cn(
                  "mt-2 space-y-0.5 pl-5 text-xs leading-5 text-ink-dim",
                  block.ordered && "list-decimal",
                  !block.ordered && "list-disc",
                ),
              },
              block.items.map((item, ii) => (
                <li key={ii}>{renderInline(item, `li${bi}-${ii}`)}</li>
              )),
            );
          case "quote":
            return (
              <blockquote
                key={bi}
                className="mt-2 border-l-2 border-accent/50 pl-3 text-[11px] italic leading-5 text-ink-dim"
              >
                {renderInline(block.lines.join(" "), `q${bi}`)}
              </blockquote>
            );
          case "hr":
            return <hr key={bi} className="mt-3 border-t border-line" />;
          default:
            return null;
        }
      })}
    </div>
  );
}
