# همگام‌سازی (SYNCHRONIZATION_FA)

ساختارهای همگام‌سازی MicroRT-Lab: Mutex (با hand-off و Priority Inheritance)،
Semaphore شمارنده، صف پیام و Event Flags — به‌علاوه‌ی دو سناریوی کلاسیک.

- [بازگشت به فهرست فارسی](README_FA.md)
- فرمت دقیق کانفیگ منابع: [SIMULATION_SCHEMA.md](../spec/SIMULATION_SCHEMA.md)

## چهار نوع منبع

```json
{ "id": "M1", "type": "mutex",   "protocol": "inherit" }   // none | inherit
{ "id": "S1", "type": "sem",     "initial": 0, "max": 3 }
{ "id": "Q1", "type": "msgq",    "capacity": 4 }
{ "id": "E1", "type": "evflags" }
```

در هر simulation حداکثر ۳۲ منبع ثبت می‌شود و هر منبع حداکثر ۳۲ waiter می‌پذیرد.
گرامرِ گام‌ها هم باید با نوع منبع بخواند: `lock`/`unlock` فقط برای mutex،
`wait`/`signal` فقط برای sem، `send`/`recv` فقط برای msgq و `evwait`/`evset` فقط
برای evflags (اعتبارسنجی در موتور و pydantic هر دو انجام می‌شود).

## Mutex

- **مالکیت انحصاری:** `lock` وقتی mutex آزاد است فوراً برمی‌گردد؛ وقتی مشغول است،
  تسک **BLOCKED** می‌شود و رخداد `LOCK_BLOCK` (با detail: منبع و مالک فعلی) ثبت
  می‌شود.
- **Hand-off:** موقع `unlock`، mutex مستقیماً به **بالاترین اولویت**ِ waiterها داده
  می‌شود (`LOCK_RELEASE` با `handedTo`)؛ به این ترتیب mutex هرگز «آزادِ بی‌صاحب» در
  صف نمی‌ماند.
- **Lock بازگشتی (recursive) پشتیبانی نمی‌شود:** اگر مالکِ فعلی دوباره `lock` بزند
  خطای `MRT_ERR_BAD_STATE` برمی‌گردد — چون از دید RAG یک self-cycle است. if شما
  نیاز به بازدخول دارید، طراحی برنامه را تغییر دهید.
- **`unlock` توسط غیرمالک** خطای validation/run-time است.

### Priority Inheritance (`protocol: "inherit"`)

وقتی تسکی روی mutexی با پروتکل inherit بلاک می‌شود و اولویت مؤثرش از مالک بالاتر
است، **اولویت مالک فوراً تا اولویتِ waiter بالا برده می‌شود** (رخداد
`LOCK_INHERIT`). این ارتقا در زنجیره‌های mutex از نوع inherit **گذرا (transitive)**
همه دنبال می‌شود؛ ارتقاها یکنوا (monotone) هستند تا حتی در حضور حلقه‌ی RAG هم
الگوریتم خاتمه یابد (تشخیص حلقه وظیفه‌ی detector موتور است). با `unlock`، rollback
از طریق بازمحاسبه‌ی اولویت برای تسکِ آزادکننده و مالک جدید انجام می‌شود
(`LOCK_UNINHERIT`).

**Priority ceiling protocol پیاده‌سازی نشده** — پروتکل‌های mutex فقط `none` و
`inherit` هستند.

## Semaphore شمارنده

- `initial` از ۰ تا `max` و `max ≥ 1`.
- `wait` (عملیات P): اگر توکنی هست مصرفش می‌کند (`SEM_WAIT` با `acquired: true`)؛
  وگرنه تسک BLOCKED می‌شود (`acquired: false`).
- `signal` (عملیات V): **hand-off به بالاترین‌اولویتِ waiter**؛ اگر waiter نبود
  شمارنده یکی زیاد می‌شود (اگر از قبل در `max` باشد، رخداد `overflow: true`).
- سیگنال‌های گم‌شونده (lost wakeup) به این شکل رخ نمی‌دهند چون wake با انتقال مستقیم
  توکن انجام می‌شود.

## صف پیام (Message Queue)

- صف کرانه‌دار با `capacity ≥ 1` و پیام‌های عدد صحیح (`msg`).
- `send`: اگر جا هست، پیام در انتهای صف می‌نشیند؛ **صف پر ⇒ فرستنده BLOCKED** می‌شود
  (رخداد `MSG_SEND` با `blocked: true`).
- `recv`: اگر صف خالی نیست، قدیمی‌ترین پیام (FIFO) مصرف می‌شود؛ **صف خالی ⇒ گیرنده
  BLOCKED**.
- هنگام `recv`، اگر فرستنده‌ی مسدودی در انتظار جا باشد، پیامش مستقیم به گیرنده‌ی
  فعلی تحویل می‌شود (hand-off) و فرستنده آزاد می‌شود.
- **سیاست بیدارباش waiterها در msgq FIFO است، نه بر اساس اولویت** — تفاوت آگاهانه با
  mutex/semaphore که بالاترین‌اولویت را بیدار می‌کنند.

## Event Flags

- هر evflags یک مجموعه‌بیت ۳۲ بیتی است.
- `evwait` با `mask` و `mode`:
  - `mode: "any"` (پیش‌فرض): شرط برقرار است اگر **حداقل یکی** از بیت‌های mask ست
    باشد؛ رضایت‌یافته ⇒ بیت‌های mask مصرف (consume) می‌شوند.
  - `mode: "all"`: هر ۳۲ بیت mask باید ست باشند.
  - ناراضی ⇒ تسک BLOCKED می‌شود (`EV_WAIT` با `satisfied: false`).
- `evset` بیت‌های mask را OR می‌کند (`EV_SET`) و همه‌ی waiterهای رضایت‌یافته را بیدار
  می‌کند (`woken: [ids]`).
- پارامترهای انتظار (mask و mode) در `engine_ctx` تسکِ بلاک‌شده بسته‌بندی می‌شوند تا
  engine بتواند بعداً بفهمد چه چیزی انتظار می‌شده است.

## سناریو ۱: Producer-Consumer

آزمایش `producer-consumer.json` یک بافر کرانه‌دار را با msgq می‌سازد:

- producerها تا پر شدن صف پیام می‌فرستند و بعد **BLOCKED** می‌شوند تا consumerها جا
  باز کنند — الگوی کلاسیک backpressure.
- در نمای Resources می‌توانید محتویات صف را tick به tick بازپخش کنید (مثلاً
  `[303, 304]` در t=۱۲) و hand-offهای مسدود را در trace ببینید.
- انتظار: تناوب producer/consumer بدون هیچ deadlock.

نکته‌ی آموزشی: با بزرگ‌کردن `capacity`، بلاک‌شدن producerها کاهش می‌یابد ولی memory
مصرف صف بیشتر می‌شود — همان trade-off بافر کرانه‌دار.

## سناریو ۲: Priority Inversion و نجات با Inheritance

آزمایش‌های `priority-inversion.json` و `priority-inheritance.json` دقیقاً یک workload
را با دو پروتکل اجرا می‌کنند:

- تسک **Low** با `M1` قفل می‌شود؛ تسک **High** (فوری‌تر) به `M1` نیاز پیدا می‌کند و
  BLOCKED می‌شود؛ تسک **Medium** (اولویتی بین این دو، بدون نیاز به mutex) Low را
  preemption می‌کند و عملاً انتظار High را طولانی می‌کند.
- بدون inheritance: High عدد **۳۴ tick** در BLOCKED می‌ماند (شماره‌های دقیق از اجرای
  جاری engine).
- با `protocol: "inherit"`: به‌محض block شدن High، اولویت Low تا سطح High ارتقا می‌یابد
  (`LOCK_INHERIT` در trace)، Medium دیگر نمی‌تواند Low را preempt کند و High فقط **۹
  tick** بلاک می‌شود.

در workstation: رخدادهای `LOCK_INHERIT`/`LOCK_UNINHERIT` در trace قابل فیلترند و
نمای Compare می‌تواند هر دو کانفیگ را کنار هم بگذارد.

## جدول رخدادهای trace مربوط به همگام‌سازی

| رخداد | معنا |
|---|---|
| `LOCK_ACQUIRE` | گرفتن mutex |
| `LOCK_BLOCK` | بلاک شدن روی mutex (با مالک فعلی) |
| `LOCK_RELEASE` | آزادسازی (با گیرنده‌ی بعدی: handedTo) |
| `LOCK_INHERIT` / `LOCK_UNINHERIT` | ارتقا/بازگشت اولویت با ارقام قبل/بعد |
| `SEM_WAIT` | عملیات P (با نتیجه‌ی acquired) |
| `SEM_SIGNAL` | عملیات V (با woken/overflow) |
| `MSG_SEND` / `MSG_RECV` | ارسال/دریافت با پیام و وضعیت block |
| `EV_WAIT` / `EV_SET` | انتظار/ست‌کردن بیت‌ها (با woken) |
