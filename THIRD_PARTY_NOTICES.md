# Third-Party Notices

MicroRT-Lab — Deterministic Real-Time Operating System & Scheduling Laboratory.
Copyright © 2026 Parsa Fathi. Licensed under Apache-2.0 (see [LICENSE](LICENSE)).

This file lists third-party software that MicroRT-Lab uses, depends on or vendors.
**MicroRT-Lab does not claim ownership of any of these components**; each remains
under its own license and the copyright of its authors. Versions below are the ones
this repository was built and tested with. Apache-2.0 is compatible with all of the
licenses listed here; MIT/BSD/ISC require license/copyright preservation, which this
file and the vendored headers provide.

## C++ engine (vendored)

| Name | Version | Purpose | License | Source | Attribution |
|---|---|---|---|---|---|
| nlohmann/json | 3.11.3 | JSON parsing/serialization for the engine's config and result documents; vendored single header at `engine/third_party/nlohmann/json.hpp` | MIT | https://github.com/nlohmann/json | © Niels Lohmann; MIT license text retained in the header |

## Python analysis layer (services/api)

| Name | Version | Purpose | License | Source | Attribution |
|---|---|---|---|---|---|
| fastapi | ≥ 0.128 | Web framework for the /api/v1 service | MIT | https://github.com/fastapi/fastapi | © FastAPI contributors; license notice retained |
| uvicorn | ≥ 0.44 | ASGI server running the FastAPI app | BSD-3-Clause | https://github.com/encode/uvicorn | © Encode OSS Ltd; BSD-3 license notice |
| pydantic | ≥ 2.12 | Config/result validation models mirroring the simulation schema | MIT | https://github.com/pydantic/pydantic | © Pydantic Services Inc. and contributors |
| starlette | (transitive via fastapi) | ASGI toolkit underneath FastAPI | MIT | https://github.com/encode/starlette | © Encode OSS Ltd |
| pytest | ≥ 9 | Test framework for the 97-test API suite (dev) | MIT | https://github.com/pytest-dev/pytest | © Holger Krekel and contributors |
| ruff | ≥ 0.13 | Linter/formatter checks for app and tests (dev) | MIT | https://github.com/astral-sh/ruff | © Astral Software Inc. |
| httpx | ≥ 0.28 | ASGI test client used by the API test suite (dev) | BSD-3-Clause | https://github.com/encode/httpx | © Encode OSS Ltd |

## Web workstation (src/)

| Name | Version | Purpose | License | Source | Attribution |
|---|---|---|---|---|---|
| next | 16.x | App Router framework, single / route | MIT | https://github.com/vercel/next.js | © Vercel, Inc. and Next.js contributors |
| react / react-dom | 19.x | UI component runtime | MIT | https://github.com/facebook/react | © Meta Platforms, Inc. and affiliates |
| tailwindcss | 4.x | Utility CSS mapped to the workstation design tokens | MIT | https://github.com/tailwindlabs/tailwindcss | © Tailwind Labs |
| shadcn/ui | (component generator, template-provided) | Component scaffolding (buttons, dialogs, tabs, …) — code copied into the project | MIT | https://github.com/shadcn-ui/ui | © shadcn-ui |
| Radix UI primitives (@radix-ui/react-*) | per package | Accessible headless primitives behind shadcn components | MIT | https://github.com/radix-ui/primitives | © WorkOS, Inc. |
| lucide-react | 0.525.x | Icon set (instrument rail, buttons) | ISC | https://github.com/lucide-icons/lucide | © Lucide Contributors |
| framer-motion | 12.x | 150 ms view transitions | MIT | https://github.com/motiondivision/motion | © Framer (Motion) |
| @tanstack/react-query | 5.x | Server state: queries/mutations for API data | MIT | https://github.com/TanStack/query | © Tanner Linsley and TanStack contributors |
| zustand | 5.x | Workstation store (views, playback, filters) | MIT | https://github.com/pmndrs/zustand | © Paul Henschel and contributors |
| recharts | 2.15.x | Metrics/compare/fragmentation charts | MIT | https://github.com/recharts/recharts | © Recharts Group |
| d3-* (d3-scale, d3-shape, d3-array, …) | recharts dependencies | Scales/shapes used by Recharts | ISC | https://github.com/d3/d3 | © Mike Bostock and D3 contributors |
| sonner | 2.x | Toast notifications | MIT | https://github.com/emilkowalski/sonner | © Emil Kowalski |
| next-themes | 0.4.x | Dark/light theme switching | MIT | https://github.com/pacocoursey/next-themes | © Paco Coursey |
| react-markdown | 10.x | Markdown rendering (installed; the reports view additionally ships a dependency-free MiniMarkdown renderer) | MIT | https://github.com/remarkjs/react-markdown | © Titus Wormer and contributors |

The web workstation also inherits a set of template-provided packages from the
Next.js + shadcn starter (zod, react-hook-form, class-variance-authority, clsx,
tailwind-merge, cmdk, vaul, date-fns, uuid, eslint tooling, and others in
`package.json`), all MIT/ISC-licensed; none are modified.

## Toolchain note

The native build uses the system C/C++ toolchain (gcc/Debian in the development
environment) and CMake — these are build tools, not vendored or redistributed
software, and no attribution obligation attaches to MicroRT-Lab for them.

## Attribution requirement summary

For the MIT-licensed components: the above copyright and permission notice must be
included in copies or substantial portions — this file and the vendored headers
serve that purpose. For BSD-3-Clause (uvicorn, httpx): redistribution must retain
the copyright notice, the license text and the disclaimer. For ISC (lucide, d3):
the copyright and ISC license text. If you redistribute MicroRT-Lab, include this
file alongside the [LICENSE](LICENSE).
