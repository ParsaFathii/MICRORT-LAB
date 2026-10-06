# حافظه (MEMORY_FA)

دو مدل حافظه‌ی MicroRT-Lab: مدل **region** (تخصیص‌های متغیر با سیاست‌های
first/best/worst-fit و coalescing) و مدل **pool** (بلوک‌های هم‌اندازه و internal
fragmentation) — و سناریوی fragmentation.

- [بازگشت به فهرست فارسی](README_FA.md)
- گام‌های مرتبط: `alloc` (با `size` و `tag` اختیاری) و `free` (با `tag`)

## فعال‌سازی

اگر کانفیگ بلوک `memory` داشته باشد گام‌های `alloc`/`free` مجازند (بدون آن،
validation error می‌گیرید):

```json
"memory": {
  "model": "region",          // region | pool
  "total": 1024,              // region: کل بایت‌ها؛ pool: blockCount × blockSize
  "policy": "first_fit",      // first_fit | best_fit | worst_fit (فقط region)
  "blockSize": 64,            // فقط pool
  "blockCount": 16            // فقط pool
}
```

- `alloc` با `size` (و `tag` اختیاری — پیش‌فرض `"a<شماره‌گام>"`)؛ شکست تخصیص رخداد
  `MEM_ALLOC` با `ok: false` می‌دهد و **برنامه ادامه می‌یابد** (کرش نمی‌کند).
- `free` با `tag` تخصیص مربوطه را آزاد می‌کند؛ تگِ ناشناخته خطای اعتبارسنجی است
  (در شروع run چک می‌شود).
- تخصیصِ زنده در پایان job ⇒ **leak** (در `memory.leaks` گزارش می‌شود).

## مدل region

- حافظه‌ی پیوسته‌ی `total` بایتی که با سیاست‌های کلاسیک جاگذاری می‌شود:
  - **first_fit:** اولین حفره‌ی آزاد که جا شود.
  - **best_fit:** کوچک‌ترین حفره‌ای که جا شود — حفره‌های ریزِ بی‌خاصیت کم می‌سازد ولی
    کندتر است.
  - **worst_fit:** بزرگ‌ترین حفره — بقایای بزرگ نگه می‌دارد ولی حفره‌های متوسط را
    می‌بلعد.
- **Coalescing:** دو حفره‌ی آزادِ مجاور موقع `free` ادغام می‌شوند (رفتار طراحی‌شده‌ی
  allocator — تست‌های kernel آن را صریح پوشش می‌دهند).
- **External fragmentation:** در هر رخداد alloc/free محاسبه و ثبت می‌شود:
  `fragmentation = freeTotal − largestFreeBlock` — یعنی بایت‌هایی که آزادند ولی به
  علت پراکنده‌بودن قابل استفاده برای درخواست بزرگ نیستند.
- سری `fragSeries` (t، used، free، largest، frag) برای رسم نمودار زمانی در UI
  گزارش می‌شود.

## مدل pool

- `blockCount` بلوک ثابتِ `blockSize` بایتی.
- `alloc` فقط اندازه‌ای ≤ blockSize قبول می‌کند و یک بلوک کامل مصرف می‌کند —
  **internal fragmentation:** تفاوت `blockSize` با `size` درخواستی هدر می‌رود.
- مزیت: تخصیص/آزادسازی O(1) و بدون external fragmentation؛ بهای آن: هدر رفت
  داخل بلوک‌ها (همان trade-off کلاسیک slab/pool allocator).

## خروجی سند نتیجه

```json
"memory": {
  "model": "region", "total": 512, "policy": "first_fit",
  "events":  [ { "t": 3, "op": "alloc", "task": "Churner", "tag": "a3",
                 "size": 128, "result": "ok", "offset": 0 } ],
  "finalLayout": [ { "offset": 0, "size": 128, "owner": "Churner", "tag": "a3" },
                   { "offset": 128, "size": 384, "owner": null } ],
  "failures": [], "leaks": [], "peakUsage": 640,
  "fragSeries": [ { "t": 3, "used": 128, "free": 384, "largest": 384, "frag": 0 } ]
}
```

- `finalLayout`: چیدمان نهایی (بخش‌های آزاد با `owner: null`).
- `failures`: خطاهای allocation با tick و اندازه.
- metricهای تجمعی هم در `metrics` می‌آیند: `memoryUtilizationAvg` (میانگین used/total
  روی رخدادهای alloc/free)، `memoryPeak`، `fragmentationAvg/Max` و `allocFailures`.

## سناریوی memory-fragmentation

آزمایش `memory-fragmentation.json` (region، ۵۱۲ بایت، first_fit) با دو تسک Churner و
BigClient تخصیص/آزادسازی می‌کنند. نتایج اجرای جاری:

- **۳۷ خطای allocation** (درخواست‌هایی که به‌خاطر پراکندگی جا نشدند)
- **۷ leak** (تخصیص‌های آزادنشده در پایان job)
- **fragmentation تا ۱۶۰ بایت** (از ۵۱۲ بایت کل)
- در UI (نمای Memory، کلید ۷): نقشه‌ی آدرس بایت‌به‌بایت که با scrubber بازپخش
  می‌شود، نمودار سطحیِ used/frag و جدول‌های failures/leaks با کلیک به جزئیات.

**تمرین:** در نمای Compare نسخه‌ی best-fit را روی همان کانفیگ اجرا کنید
(preset «first vs best fit») و `allocFailures` و `fragmentationMax` را کنار هم
بگذارید — best-fit معمولاً تعداد خطا را کم می‌کند ولی حفره‌های کوچک‌تری باقی
می‌گذارد.

## نکات طراحی

- مدل حافظه در خودِ kernel پیاده شده (`mrt_mem.c`): آرایه‌ی ثابت بلوک‌ها، بدون
  malloc — شبیه allocator یک RTOS واقعی. تست‌های kernel (`test_mem`، ۸۲ check)
  مسیرهای allocate/free/coalesce/full را می‌پوشانند.
- «قطعی» بودن allocator یعنی برای دنباله‌ی یکسان alloc/free، چیدمان نتیجه هم یکسان
  است — بخشی از تضمین byte-determinism کل سند نتیجه.
- memory model هیچ ارتباطی با صف‌بندی I/O یا deviceها ندارد؛ برای مدل حافظه‌ی
  فیزیکیِ جدا (per-task memory partitions) طراحی نشده است.
