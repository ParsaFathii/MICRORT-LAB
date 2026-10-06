# نصب و راه‌اندازی (INSTALLATION_FA)

نصب و اجرای MicroRT-Lab روی سه بخش مستقل تقسیم می‌شود: موتور native (هسته + موتور C++)،
سرویس تحلیل Python و رابط کاربری وب. هر سه می‌توانند مستقل اجرا شوند، اما تجربه‌ی کامل
به هر سه نیاز دارد.

- [بازگشت به فهرست فارسی](README_FA.md)

## پیش‌نیازها

| بخش | پیش‌نیاز | توضیح |
|---|---|---|
| موتور native | gcc 14+ یا clang اخیر | پشتیبانی C11 و C++20 لازم است (نسخه‌ی g++ را با `g++ --version` چک کنید) |
| موتور native | CMake ≥ 3.16 | اگر از pip نصب می‌کنید، مطمئن شوید باینری آن در `PATH` است (بخش عیب‌یابی) |
| سرویس API | Python 3.12+ | به‌همراه fastapi، uvicorn و pydantic v2 (برای توسعه: pytest و ruff) |
| رابط کاربری | Node 20+ یا bun 1.1+ | توسعه‌ی پروژه با bun انجام می‌شود (`bun run dev`) |
| عمومی | git | برای کلون مخزن |

کلون مخزن:

```bash
git clone https://github.com/ParsaFathii/MICRORT-LAB.git
cd MICRORT-LAB
```

## ۱) ساخت هسته و موتور

```bash
bash scripts/build-native.sh
```

این اسکریپت دو مرحله را انجام می‌دهد:

```bash
cmake -S . -B engine/build -DCMAKE_BUILD_TYPE=Release
cmake --build engine/build -j<nproc>
```

- **مسیر باینری نهایی: `engine/build/engine/micrort-engine`** (چیدمان زیرپوشه‌ای CMake؛
  توجه کنید که مسیر واقعی باینری یک سطح داخل `engine/build/engine/` است).
- اگر فقط می‌خواهید هسته را جداگانه بسازید: `cmake -S . -B kernel/build && cmake --build kernel/build`.

بررسی سریع موفقیت ساخت:

```console
$ engine/build/engine/micrort-engine selftest
{"checks":54,"selftest":"ok"}

$ engine/build/engine/micrort-engine validate --config experiments/rate-monotonic.json
{"valid":true,"configHash":"a48b326b378f504c"}
```

## ۲) اجرای سرویس تحلیل (API)

```bash
cd services/api
python3 -m uvicorn app.main:app --host 127.0.0.1 --port 3031
# یا حالت توسعه با reload:
bash run-dev.sh
```

- سرویس روی `127.0.0.1:3031` بالا می‌آید (عمداً به loopback بسته است — ابزار آزمایشگاهی
  تک‌کاربره است، نه سرویس اینترنتی).
- مستندات تعاملی OpenAPI: `http://127.0.0.1:3031/docs`
- **باینری موتور باید از قبل ساخته شده باشد.** اگر نباشد، سرویس بالا می‌آید اما
  `/api/v1/health` مقدار `engine.available=false` برمی‌گرداند و همه‌ی endpointهای اجرا
  با **503** و پیام «engine binary not built — run scripts/build-native.sh» جواب می‌دهند.
- مسیر باینری موتور، پوشه‌ی داده (SQLite) و پوشه‌ی آزمایش‌ها با متغیرهای محیطی قابل
  تغییرند: `MICRORT_ENGINE_BIN`، `MICRORT_DATA_DIR`، `MICRORT_EXPERIMENTS_DIR`.

بررسی سلامت:

```bash
curl -s http://127.0.0.1:3031/api/v1/health
```

## ۳) اجرای رابط کاربری

```bash
bun install
bun run dev          # پورت 3000
```

سپس `http://localhost:3000` را باز کنید. UI از طریق درخواست‌های نسبیِ
`/api/v1/...?XTransformPort=3031` با سرویس API حرف می‌زند؛ در محیط عادی (خارج از
sandbox) سرویس API باید روی `127.0.0.1:3031` در حال اجرا باشد.

## قرارداد پورت‌ها

| پورت | سرویس | توضیح |
|---|---|---|
| 3000 | رابط کاربری (Next.js dev) | در محیط sandbox از طریق gateway روی پورت خارجی ۸۱ در دسترس است |
| 3031 | سرویس تحلیل FastAPI | فقط از طریق `XTransformPort=3031` از مرورگر قابل دسترس است (در sandbox)؛ دسترسی مستقیم `http://127.0.0.1:3031` |

جزئیات محیط توسعه و gateway: [../dev/SETUP.md](../dev/SETUP.md)

## عیب‌یابی رایج

| نشانه | علت و راه‌حل |
|---|---|
| خطای کامپایل مربوط به C++20 | نسخه‌ی toolchain قدیمی است؛ gcc 14+ لازم است |
| `cmake` پیدا نمی‌شود یا قدیمی است | CMake در برخی محیط‌ها از pip نصب می‌شود؛ `cmake --version` را چک کنید و مطمئن شوید پوشه‌ی bin کاربر در `PATH` است |
| API خطای 503 می‌دهد | موتور ساخته نشده — `bash scripts/build-native.sh` را اجرا کنید |
| پورت 3031 اشغال است | یک نمونه‌ی uvicorn دیگر در حال اجراست؛ با `--port` دیگری بالا بیاورید |
| پورت 3000 اشغال است | dev server قبلی هنوز در حال اجراست؛ `bun run dev` قدیمی را ببندید |
| API خطای 422 می‌دهد | خطای validation در config — `micrort-engine validate --config <فایل>` جزئیات را مستقیم نشان می‌دهد |
| صفحه‌ی وب بالا می‌آید ولی API جواب نمی‌دهد | سرویس API روی 3031 اجرا نشده است یا base URL نادرست است |

## اجرای تست‌ها (اختیاری، برای اطمینان از نصب)

```bash
ctest --test-dir engine/build        # ۱۱/۱۱ suite (۵ هسته + ۶ موتور)
cd services/api && python3 -m pytest  # ۹۷ تست (نیاز به باینری موتور دارد)
cd /path/to/repo && bun run lint      # eslint پروژه‌ی وب
```
