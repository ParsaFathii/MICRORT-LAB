# API فارسی (API_FA)

مرجع endpointهای سرویس تحلیل (`/api/v1`)، مثال‌های curl و فرمت خطاها.

- [بازگشت به فهرست فارسی](README_FA.md)
- مرجع کامل انگلیسی: [services/api/README.md](../../services/api/README.md)
- مستندات تعاملی: `http://127.0.0.1:3031/docs`

## قراردادهای عمومی

- **آدرس مستقیم:** `http://127.0.0.1:3031/api/v1/...` — سرویس عمداً به loopback بسته
  است (ابزار آزمایشگاهی تک‌کاربره؛ authentication ندارد).
- **idها:** دوازده رقم hex؛ زمان‌ها ISO-8601 با پسوند Z.
- **لیست‌ها:** همیشه `{ "items": [...], "total": n, "limit": l, "offset": o }`.
- **خطاها:** `{ "error": پیام, "details": [...] }` با کدهای 404 (ناشناخته)، 422
  (خطای validation — با مسیر فیلدها)، 409 (run نکرده/وضعیت ناسازگار)، 502 (خطای
  runtime/timeout موتور) و 503 (باینری موتور ساخته نشده).
- **چرخه‌ی عمر simulation:** `created | running | completed | failed | stopped`.

## Health

| Method | Path | توضیح |
|---|---|---|
| GET | `/health` | وضعیت سرویس، در دسترس بودن موتور (کش ۳۰ ثانیه) و تعداد ردیف‌ها |

```bash
curl -s http://127.0.0.1:3031/api/v1/health
# → {"status":"ok","engine":{"available":true,"binary":".../micrort-engine","info":{...}}}
```

## Simulations

| Method | Path | توضیح |
|---|---|---|
| POST | `/simulations` | ساخت از `{name?, config}` (اعتبارسنجی pydantic) |
| GET | `/simulations?limit&offset` | فهرست صفحه‌بندی‌شده |
| GET | `/simulations/{id}` | رکورد کامل با سند نتیجه |
| POST | `/simulations/{id}/run` | اجرای موتور (همزمان) و ذخیره‌ی نتیجه |
| POST | `/simulations/{id}/stop` | خاتمه‌ی اجرای فعال (اگر running نباشد 409) |
| DELETE | `/simulations/{id}` | حذف رکورد و گزارش‌هایش |
| GET | `/simulations/{id}/trace?type&task&res&limit&offset` | رخدادها، مرتب بر اساس (t, seq) |
| GET | `/simulations/{id}/metrics` | metricهای موتور + بازمحاسبه‌ی Python + Δ |
| GET | `/simulations/{id}/tasks` | حسابداری per-task |
| GET | `/simulations/{id}/gantt` | بخش‌های gantt |
| GET | `/simulations/{id}/memory` | بخش حافظه (events، layout، fragSeries) |
| GET | `/simulations/{id}/deadlocks` | چرخه‌های RAG |
| GET | `/simulations/{id}/analysis` | تحلیل rt + starvation + fragmentation |
| GET | `/simulations/{id}/report?format=markdown\|csv\|json` | گزارش (و ذخیره‌ی آن) |

```bash
# اجرای یک آزمایش آماده (همزمان؛ چند میلی‌ثانیه)
curl -s -X POST http://127.0.0.1:3031/api/v1/experiments/rr-quantum-comparison/run
# → {"id":"333e411fe594","status":"completed","scheduler":"rr","taskCount":4,
#    "simulatedUntil":51,"configHash":"a48b326b378f504c", ...}

# trace با فیلتر و صفحه‌بندی
curl -s "http://127.0.0.1:3031/api/v1/simulations/333e411fe594/trace?limit=2"
# → {"id":"333e411fe594","items":[{"seq":0,"t":0,"type":"SIM_START",...},
#    {"seq":1,"t":0,"task":"T1","type":"TASK_ARRIVAL","to":"READY",...}],
#    "total":59,"limit":2,"offset":0,"unfilteredTotal":59}

# cross-check metricها
curl -s http://127.0.0.1:3031/api/v1/simulations/333e411fe594/metrics
# → {"engine":{"avgWaiting":24.0,"cpuUtilization":0.941,"ctxSwitches":11,...},
#    "recomputed":{...},"deltas":{...},"match":true,"tolerance":0.001}
```

## Experiments

| Method | Path | توضیح |
|---|---|---|
| GET | `/experiments` | ۹ آزمایش آماده (از `experiments/*.json`) + سفارشی‌های DB |
| GET | `/experiments/{id}` | رکورد کامل (id = نام فایل یا uuid کوتاه) |
| POST | `/experiments` | ذخیره‌ی آزمایش سفارشی (pydantic + validate موتور) |
| POST | `/experiments/{id}/run` | ساخت + اجرای simulation از آزمایش |
| POST | `/experiments/{id}/validate` | نتیجه‌ی validate موتور |

## Comparisons

| Method | Path | توضیح |
|---|---|---|
| POST | `/comparisons` | `{name?, config \| experimentId, variants: [{label, scheduler?, memory?, aging?}]}` |
| GET | `/comparisons?limit&offset` | فهرست صفحه‌بندی‌شده |
| GET | `/comparisons/{id}` | رکورد کامل (metric هر واریانت، بهترین هر metric، Δ نسبت به اولی) |
| GET | `/comparisons/{id}/report` | گزارش markdown |

```bash
curl -s -X POST http://127.0.0.1:3031/api/v1/comparisons \
  -H 'Content-Type: application/json' \
  -d '{"experimentId":"edf-deadlines",
       "variants":[{"label":"priority_p","scheduler":{"type":"priority_p"}},
                   {"label":"edf","scheduler":{"type":"edf"}}]}'
# → {"id":"132f7747a442","baseSource":"experiment:edf-deadlines",
#    "variantCount":2,"variants":[...],"results":[...],"bestPerMetric":[...]}
```

منطق merge: شیءهای `scheduler`/`memory`/`aging` هر واریانت روی کانفیگ پایه deep-merge
می‌شوند (dictها بازگشتی، لیست/اسکالر جایگزین کامل) و نتیجه دوباره validate می‌شود.

## Reports

| Method | Path | توضیح |
|---|---|---|
| GET | `/reports?simulationId=\|comparisonId=&limit&offset` | گزارش‌های ذخیره‌شده |
| GET | `/reports/{id}?format=` | محتوای گزارش (۴۰۰ اگر فرمت نخوانده) |

## متغیرهای محیطی

| متغیر | پیش‌فرض (نسبت به `services/api`) | معنا |
|---|---|---|
| `MICRORT_ENGINE_BIN` | `../../engine/build/engine/micrort-engine` | مسیر باینری موتور |
| `MICRORT_DATA_DIR` | `./data` | پوشه‌ی داده SQLite |
| `MICRORT_EXPERIMENTS_DIR` | `../../experiments` | پوشه‌ی آزمایش‌های آماده |

## نکته‌های رفتاری

- **اجرای موتور synchronous است:** هر درخواست run یک subprocess موتور اجرا می‌کند با
  timeout ۶۰ ثانیه و kill سخت‌گیرانه؛ endpoint `stop` همان subprocess را می‌کشد و
  رکورد با وضعیت `stopped` نهایی می‌شود.
- **دو لایه اعتبارسنجی:** pydantic در Python (شکل فیلدها و قواعد cross-field) و بعد
  از آن `micrort-engine validate` خودِ موتور؛ نتیجه‌ی خطای موتور با 422 به client
  می‌رسد.
- **Determinism از طریق API هم برقرار است:** merge و اجرا دوباره انجام می‌شود ولی با
  ورودی یکسان، سند نتیجه یکسان است (فقط `wallMicros` فرق می‌کند).
