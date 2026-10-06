# توسعه (DEVELOPMENT_FA)

ساختار پروژه، قراردادهای schema، اجرای تست‌ها و راهنمای افزودن زمان‌بند جدید.

- [بازگشت به فهرست فارسی](README_FA.md)
- راهنمای انگلیسی مشارکت: [CONTRIBUTING.md](../../CONTRIBUTING.md)

## ساختار پروژه

```
MICRORT-LAB/
├── kernel/                    هسته‌ی C11 (بدون وابستگی به engine/UI)
│   ├── include/micrort/       mrt_types.h · mrt_task.h · mrt_queue.h ·
│   │                          mrt_sync.h · mrt_mem.h · mrt_kernel.h  ← قرارداد API
│   ├── src/                   mrt_task.c · mrt_queue.c · mrt_sync.c ·
│   │                          mrt_mem.c · mrt_timer.c · mrt_kernel.c
│   └── tests/                 test_task · test_queue · test_sync ·
│                              test_mem · test_timer (harness سبک C)
├── engine/                    موتور C++20
│   ├── include/micrort/       config.hpp · scheduler.hpp · simulation.hpp ·
│   │                          trace.hpp · result.hpp · cli.hpp · rng.hpp · sha256.hpp
│   ├── src/                   پیاده‌سازی‌ها + main.cpp (CLI)
│   ├── tests/                 ۶ suite (config · scheduler · cli ·
│   │                          simulation · trace_gantt · metrics)
│   └── third_party/nlohmann/  json.hpp نسخه‌ی ۳٫۱۱٫۳ (MIT، vendored)
├── services/api/              لایه‌ی Python 3.12 + FastAPI
│   ├── app/                   main · config · models · engine · store ·
│   │                          analysis · reports · api/routeها
│   └── tests/                 ۹۷ تست pytest (با باینری واقعی موتور)
├── src/                       رابط کاربری Next.js 16 (workstation)
│   ├── app/                   layout · page (route واحد /)
│   ├── components/views/      ۱۱ نما + registry
│   ├── components/timeline/   GanttTimeline · EventInspector · ابزارهای مشترک
│   ├── lib/                   api.ts (کلاینت typed) · types.ts (آینه‌ی schema)
│   ├── store/                 workstation.ts (zustand)
│   └── hooks/                 useSimulation · usePlaybackClock · useComparisons · useReports
├── experiments/               ۹ کانفیگ آماده (micrort-config/1)
├── scripts/                   build-native.sh
├── docs/                      spec (SIMULATION_SCHEMA · FRONTEND_ARCHITECTURE) ·
│                              dev · fa (همین مجموعه)
└── CMakeLists.txt             ریشه (enable_testing قبل از add_subdirectory)
```

## قراردادهای schema — مهم‌ترین قانون توسعه

**`docs/spec/SIMULATION_SCHEMA.md` اول به‌روز شود، بعد لایه‌ها.** این سند قرارداد
منجمدِ داده بین موتور C++، لایه‌ی Python و UI است: کانفیگ، سند نتیجه، نوع رخدادهای
trace، معنای stateها، قواعد tie-breaking، قرارداد CLI و فرمول metricها. هر تغییر بدون
به‌روزرسانی schema یعنی لایه‌ها از هم می‌افتند. pydantic در Python و `types.ts` در
TS باید آینه‌ی دقیق این سند بمانند (شامل قواعد cross-field مثل «quantum فقط برای
rr» یا «jitter فقط روی periodic»).

## ساخت و تست

```bash
# ساخت native (هسته + موتور)
bash scripts/build-native.sh           # → engine/build/engine/micrort-engine

# تست‌های C (۱۱ suite: ۵ هسته + ۶ موتور)
ctest --test-dir engine/build
engine/build/engine/micrort-engine selftest   # {"checks":54,"selftest":"ok"}

# لایه‌ی Python
cd services/api
python3 -m ruff check app tests        # lint (line-length 100)
python3 -m pytest                      # ۹۷ تست — از باینری واقعی موتور استفاده می‌کند

# رابط کاربری
bun run lint                           # eslint
bunx tsc --noEmit                      # type-check (پروژه‌ی src)
bun run dev                            # dev server روی 3000
```

انتظار این است که هر subsystem با چرخه‌ی «پیاده‌سازی → تست → اصلاح → تست مجدد»
جلو رفته باشد و در پایان کار همه‌ی تست‌ها سبز باشند.

## سبک کد

- **C (هسته):** C11 خالص، ظرفیت‌های ثابت، بدون تخصیص dynamic، بدون warning با
  `-Wall -Wextra -Wpedantic`؛ کد خطا از `mrt_result_t`؛ هدرها قرارداد را مستند
  می‌کنند. تست‌ها با harness سبکِ خودِ پروژه نوشته می‌شوند (شمارش check).
- **C++ (موتور):** RAII و بدون متغیر global؛ **بدون containerهای unordered** (خروجی باید
  به ترتیب declaration یا mapهای مرتب سریال‌یزد تا سند نتیجه بایت‌به‌بایت قطعی بماند)؛
  هدرها قرارداد هر کلاس/تابع را مستند می‌کنند.
- **Python:** type-annotated کامل؛ ruff با قواعد E/W/F/I/UP/B و طول خط ۱۰۰؛
  handlerها sync و handlerهای DB با قفل store؛ بدون state سراسری بیرون از store و
  registry پروسه‌های فعال.
- **TypeScript (UI):** strict و **بدون `any`** (از `unknown` + guard استفاده کنید)؛
  همه‌ی درخواست‌ها از `src/lib/api.ts` با timeout و خطای typed؛ رنگ‌ها فقط از
  design tokenهای workstation (بدون آبی/indigo).

## افزودن زمان‌بند جدید (مثال عملی)

فرض کنید Scheduler جدیدی با نام `my_sched` می‌خواهیم:

1. **Schema اول:** `docs/spec/SIMULATION_SCHEMA.md` — نوع جدید را به لیست schedulerها
   اضافه کنید و semantics آن را دقیق بنویسید (ترتیب dispatch، preemption، پارامترها).
2. **هسته اگر لازم است:** اگر سیاست شما با صف اولویتِ kernel قابل بیان نیست (مثل
   deadline-based)، کاری به kernel ندارد — kernel همان صف (اولویت، ترتیب enqueue) را
   نگه می‌دارد و انتخاب با engine است.
3. **موتور:** `engine/src/scheduler.cpp` (و `scheduler.hpp`) — کلاس `IScheduler` را
   پیاده کنید: `compare()` (رتبه‌ی کاندیدها؛ کوچک‌تر = فوری‌تر)، `preemptive()`،
   `ranksByQueue()`، `quantum()`؛ در کارخانه‌ی `makeScheduler()` نوع رشته‌ای را
   map کنید. اگر اولویت‌های پایه را remap می‌کنید (مثل RM)، در هدر مستند کنید.
4. **اعتبارسنجی:** `engine/src/config.cpp` — پارامترهای جدید را validate کنید.
5. **Python:** `services/api/app/models.py` — نوع scheduler را به Literal اضافه کنید
   (pydantic) و اگر پارامتر جدید دارد، مدلش را بسازید؛ `analysis.py` فقط در صورت
   نیاز به تحلیل نظری تازه.
6. **UI:** `src/lib/types.ts` (نوع) و selectorهای مربوطه در Compare/Workbench.
7. **تست:** در `engine/tests/test_scheduler.*` رفتار جدید را اضافه کنید (شامل
   tie-breakها)، یک آزمایش `experiments/*.json` که ویژگی متمایزش را نشان دهد و اگر
   مسیر analysis جدیدی دارید، تست pytest.
8. **مستندات:** همین مجموعه (SCHEDULING_FA و README) و جدول presetهای Compare.

## کار با مخزن

- commitهای کوچک با پیشوند conventional: `feat:` · `fix:` · `docs:` · `ci:` · `test:`
- پیش از PR: `ctest` + `pytest` + `ruff` + `bun run lint` همه سبز.
- کد شخص ثالث فقط با ذکر منبع و مجوز در `THIRD_PARTY_NOTICES.md`.
- `worklog.md` (در محیط sandbox) حافظه‌ی مشترک agentهاست — فقط append، بدون بازنویسی.
