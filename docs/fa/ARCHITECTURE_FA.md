# معماری (ARCHITECTURE_FA)

معماری لایه‌ای MicroRT-Lab، چرایی انتخاب هر زبان و جریان داده از کانفیگ تا رابط کاربری.

- [بازگشت به فهرست فارسی](README_FA.md)
- سند رسمی داده: [SIMULATION_SCHEMA.md](../spec/SIMULATION_SCHEMA.md)

## نمای کلی لایه‌ها

```
            config JSON (micrort-config/1) + seed
                            │
┌───────────────────────────▼───────────────────────────────┐
│ kernel/  (C11)                                             │
│ TCB · صف آماده (اولویت + ترتیب enqueue) · mutex (hand-off  │
│ + priority inheritance) · semaphore شمارنده · صف پیام ·    │
│ event flags · allocator حافظه (region/pool)                │
│ ظرفیت‌های ثابت compile-time، بدون تخصیص dynamic            │
├────────────────────────────────────────────────────────────┤
│ engine/  (C++20)                                           │
│ event queue · مفسر برنامه‌ی ۱۳ op · ۸ Scheduler ·          │
│ preemption + هزینه‌ی context switch · تشخیص deadlock (RAG) │
│ · aging · ضبط trace/gantt/metrics · سند نتیجه (JSON)       │
│ CLI: micrort-engine (run | validate | schema | selftest)   │
├────────────────────────────────────────────────────────────┤
│ services/api/  (Python 3.12 + FastAPI)                     │
│ مدیریت آزمایش · اجرای subprocess موتور · اعتبارسنجی        │
│ pydantic · بازمحاسبه‌ی metricها (cross-check) · تحلیل      │
│ RM/EDF · مقایسه‌ی واریانت‌ها · گزارش · ذخیره‌سازی SQLite    │
├────────────────────────────────────────────────────────────┤
│ src/  (TypeScript + React / Next.js 16)                    │
│ workstation شبیه‌سازی: Gantt · playback · trace · RAG ·     │
│ نقشه‌ی حافظه · metricها · مقایسه · گزارش                   │
└────────────────────────────────────────────────────────────┘
```

## چرا هر لایه با این زبان؟

**kernel/ با C.** مدل سطح‌پایین task و همگام‌سازی دقیقاً از همان جنس کدی است که از C
سود می‌برد: کوچک و صریح، آرایه‌های ثابت به‌جای containerهای dynamic، مرز `extern "C"`
و کد خطا به‌جای exception. موتور C++ مستقیماً به همین کد link می‌شود — یعنی
**kernelِ شبیه‌سازی‌شده همان کد C است**؛ semantics ای که مطالعه می‌کنید همان semantics ای
است که توابع C پیاده کرده‌اند، نه یک بازسازی جداگانه.

**engine/ با C++.** event queue، مفسر برنامه، سیاست‌های Scheduler، ورودی/خروجی JSON
(با کتابخانه‌ی vendored شده‌ی nlohmann/json) و سریال‌سازی سند نتیجه به RAII و
destructor و containerهای دارای ترتیب قطعی نیاز دارند. نکته‌ی مهم برای
byte-determinism: موتور از containerهای unordered (مثل `std::unordered_map`) به‌طور
کامل پرهیز می‌کند؛ همه‌ی مسیرهای خروجی یا به ترتیب declaration می‌گردند یا از mapهای
مرتب — برای همین سند نتیجه برای ورودی یکسان، بایت‌به‌بایت یکسان است.

**services/api/ با Python.** مدیریت آزمایش، کنترل subprocess، اعتبارسنجی pydantic
(که گرامر دقیق موتور را آینه می‌کند)، بازمحاسبه‌ی metricها و تولید گزارش کارِ
plumbing و I/O است؛ FastAPI، pydantic و pytest ابزارهای درست این لایه‌اند. توابع
handler همگی sync هستند و در threadpool اجرا می‌شوند؛ اجرای موتور با timeout ۶۰ ثانیه
و kill سخت‌گیرانه همراه است تا event loop برای همیشه بلاک نشود.

**src/ با TypeScript/React.** UI نمایش‌دهنده‌ی داده‌های trace است و **هرگز نتیجه‌ی
Scheduler را محاسبه نمی‌کند** — هر عددی که می‌بینید از سند نتیجه‌ی موتور یا لایه‌ی
تحلیل Python می‌آید. به این ترتیب نمودارها هرگز از شبیه‌سازی جدا نمی‌افتند.

## جریان داده

```
config JSON ──▶ micrort-engine run (subprocess) ──▶ سند نتیجه (micrort-result/1)
                                                       │
                      trace · gantt · tasks · metrics · deadlocks · memory
                                                       │
                                                       ▼
                    FastAPI /api/v1/* (ذخیره در SQLite، بازمحاسبه‌ی metric،
                    مقایسه‌ی واریانت‌ها، تحلیل RM/EDF، تولید گزارش)
                                                       │
                                                       ▼
                    workstation وب (timeline · playback · RAG · نقشه‌ی حافظه)
```

## چرخه‌ی event در موتور

موتور در هر tick این فازها را به‌ترتیب ثابت اجرا می‌کند (تا رفتار یکسان بماند):

```
t = min(سر صف event موتور، سر لیست timerهای kernel)
 ۱. پردازش timerهای سررسیده به ترتیب درج (IO_END / SLEEP_END / DEADLINE / aging)
 ۲. arrival و releaseهای job به ترتیب declaration تسک‌ها
 ۳. اتمام‌ها و unblockهای هم‌تیک به ترتیب seq
 ۴. دقیقاً یک نقطه‌ی تصمیم زمان‌بندی: بررسی preemption + حلقه‌ی dispatch
 ۵. رأی‌گیری به‌تعویق‌افتاده‌ی deadline-miss (اتمام دقیقاً «روی» tick مهلت = به‌وقت)
```

## نقش SQLite و چرخه‌ی عمر simulation

- لایه‌ی Python هر simulation/report/comparison را در SQLite (پوشه‌ی `services/api/data`) ذخیره
  می‌کند؛ idها دوازده رقم hex و زمان‌ها ISO-8601 (Z) هستند.
- چرخه‌ی عمر سطح API: `created | running | completed | failed | stopped`. وضعیت خودِ سند
  نتیجه‌ی موتور (`completed | stopped` — یعنی رسیدن به افق/سقف رخداد با کار باقی‌مانده)
  داخل سند نتیجه می‌ماند و در سطح API همچنان «completed» حساب می‌شود.

## ظرفیت‌ها و مرزهای طراحی

- یک CPU (`cpus` باید ۱ باشد)، حداکثر ۶۴ تسک، ۳۲ منبع، ۲۵۶ گام در هر job، ۳۲ waiter
  برای هر منبع، ۱۲۸ timer، ۵۰۰٬۰۰۰ رخداد trace و افق ≤ ۱۰۰٬۰۰۰ tick. همه‌ی این‌ها
  ثابت‌های compile-time در kernel هستند — مثل چیدمان یک RTOS کوچک واقعی.
- جزئیات قراردادهای داده (فیلدها، معنای stateها، قواعد tie-breaking، فرمول metricها):
  [SIMULATION_SCHEMA.md](../spec/SIMULATION_SCHEMA.md)
