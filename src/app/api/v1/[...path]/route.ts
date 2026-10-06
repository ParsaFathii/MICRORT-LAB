/**
 * MicroRT-Lab — Next.js API gateway fallback for the Python analysis layer.
 *
 * The browser always calls relative URLs under /api/v1/… carrying the
 * sandbox-gateway marker `?XTransformPort=3031`. Two transport paths exist:
 *
 *   1. Gateway path — Caddy sees the marker and reverse-proxies straight to
 *      the FastAPI service on 127.0.0.1:3031 (this handler is never hit).
 *   2. Direct path — when the workstation is opened straight on the dev
 *      server (localhost:3000) the request lands here and is proxied
 *      server-side to the very same FastAPI service.
 *
 * Route handlers keep the browser contract unchanged (relative paths only),
 * so the workstation works regardless of how it is reached.
 */

import { NextRequest } from "next/server";

const UPSTREAM =
  process.env.MICRORT_API_UPSTREAM ?? "http://127.0.0.1:3031";

/** Hop-by-hop / transport headers that must not be forwarded verbatim. */
const STRIP_REQUEST_HEADERS = new Set([
  "host",
  "connection",
  "keep-alive",
  "transfer-encoding",
  "upgrade",
  "content-length",
  "accept-encoding",
]);

/** Response headers worth preserving on top of content-type. */
const KEEP_RESPONSE_HEADERS = new Set([
  "content-type",
  "content-disposition",
]);

async function proxy(req: NextRequest, segments: string[]): Promise<Response> {
  const incoming = new URL(req.url);
  const path = segments.map((s) => encodeURIComponent(s)).join("/");
  const target = new URL(`${UPSTREAM}/api/v1/${path}`);

  // Preserve the query string minus the gateway marker (upstream never sees it).
  incoming.searchParams.forEach((value, key) => {
    if (key.toLowerCase() !== "xtransformport") {
      target.searchParams.set(key, value);
    }
  });

  const headers = new Headers();
  req.headers.forEach((value, key) => {
    if (!STRIP_REQUEST_HEADERS.has(key.toLowerCase())) {
      headers.set(key, value);
    }
  });
  headers.set("accept-encoding", "identity"); // no upstream compression → no re-decode

  const hasBody = req.method !== "GET" && req.method !== "HEAD";
  const body = hasBody ? await req.text() : undefined;

  let upstream: Response;
  try {
    upstream = await fetch(target, {
      method: req.method,
      headers,
      body,
      cache: "no-store",
      redirect: "manual",
    });
  } catch {
    return Response.json(
      {
        error:
          "analysis service unreachable — start it with services/api/run-dev.sh (port 3031)",
      },
      { status: 502 },
    );
  }

  const payload = await upstream.arrayBuffer();
  const outHeaders = new Headers({ "cache-control": "no-store" });
  upstream.headers.forEach((value, key) => {
    if (KEEP_RESPONSE_HEADERS.has(key.toLowerCase())) {
      outHeaders.set(key, value);
    }
  });
  return new Response(payload, {
    status: upstream.status,
    statusText: upstream.statusText,
    headers: outHeaders,
  });
}

type Ctx = { params: Promise<{ path: string[] }> };

export async function GET(req: NextRequest, ctx: Ctx): Promise<Response> {
  const { path } = await ctx.params;
  return proxy(req, path);
}

export async function POST(req: NextRequest, ctx: Ctx): Promise<Response> {
  const { path } = await ctx.params;
  return proxy(req, path);
}

export async function DELETE(req: NextRequest, ctx: Ctx): Promise<Response> {
  const { path } = await ctx.params;
  return proxy(req, path);
}

export const dynamic = "force-dynamic";
