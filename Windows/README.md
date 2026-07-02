# Iwana Proxy (Windows)

پورت ویندوزی پروژه‌ی [Iwana-Proxy](https://github.com/Iwanian/Iwana-Proxy) (که اصلش یک اپ اندرویدی با Kotlin بود)، بازنویسی‌شده با **C# / .NET 8 / WPF** تا روی ویندوز به‌صورت فایل `.exe` مستقل اجرا بشه.

## این برنامه چیکار می‌کند؟

1. لیست پروکسی‌های MTProto تلگرام را از همان منبع گیت‌هاب پروژه‌ی اصلی دانلود می‌کند.
2. لینک‌های `tg://proxy?...` را پارس می‌کند.
3. به‌صورت موازی (حداکثر ۸ اتصال هم‌زمان) تأخیر اتصال (ping) هرکدام را تست می‌کند.
4. پروکسی‌های فعال را بر اساس سرعت مرتب نمایش می‌دهد.
5. با کلیک روی «اتصال»، Telegram Desktop را با آن پروکسی باز می‌کند.

> برای کار کردن دکمه‌ی «اتصال»، باید **Telegram Desktop** روی سیستم نصب باشد (همان نرم‌افزار رسمی تلگرام برای ویندوز)، چون این برنامه از پروتکل `tg://` که توسط Telegram Desktop ثبت می‌شود استفاده می‌کند.

## پیش‌نیاز برای ساخت exe

فقط یک چیز لازم است: **.NET 8 SDK**
دانلود: https://dotnet.microsoft.com/download/dotnet/8.0

## ساخت فایل exe

در ترمینال (PowerShell یا CMD) داخل پوشه‌ی پروژه (همان پوشه‌ای که فایل `IwanaProxy.csproj` در آن است) دستور زیر را اجرا کنید:

```powershell
dotnet publish -c Release
```

فایل خروجی اینجا قرار می‌گیرد:

```
bin\Release\net8.0-windows\win-x64\publish\IwanaProxy.exe
```

این فایل **مستقل (self-contained)** است؛ یعنی نیازی نیست کاربر نهایی .NET Runtime را جداگانه نصب کند — همه‌چیز داخل خود exe بسته‌بندی شده است. حجم فایل حدود ۱۵۰-۱۷۰ مگابایت خواهد بود (به‌خاطر همراه بودن runtime).

### اگر می‌خواهید فایل کوچک‌تر باشد (نیازمند نصب .NET Runtime روی سیستم کاربر)

فایل `IwanaProxy.csproj` را باز کنید و خط `<SelfContained>true</SelfContained>` را به `<SelfContained>false</SelfContained>` تغییر دهید، سپس همان دستور `dotnet publish -c Release` را دوباره اجرا کنید. در این حالت کاربر باید [.NET 8 Desktop Runtime](https://dotnet.microsoft.com/download/dotnet/8.0) را نصب کرده باشد.

## ساختار پروژه

```
IwanaProxy/
├── IwanaProxy.csproj          تنظیمات پروژه و publish
├── App.xaml / App.xaml.cs     نقطه‌ی شروع برنامه و تم رنگی
├── MainWindow.xaml            رابط کاربری (لیست پروکسی، جستجو، دکمه‌ها)
├── MainWindow.xaml.cs         منطق اتصال UI به سرویس‌ها
├── Models/
│   └── ProxyItem.cs           مدل داده‌ی هر پروکسی
└── Services/
    ├── ProxyParser.cs         پارس لینک‌های tg://proxy با regex
    ├── ProxyRepository.cs     دانلود لیست از گیت‌هاب
    ├── PingService.cs         تست TCP ping موازی
    └── TelegramLauncher.cs    باز کردن Telegram Desktop با پروکسی
```

## نکات

- اگر آنتی‌ویروس یا Windows SmartScreen به فایل exe هشدار داد (چون امضای دیجیتال/کد ساینینگ ندارد)، طبیعی است برای فایل‌های exe خودساخته؛ گزینه‌ی "More info → Run anyway" را بزنید.
- برای ساخت یک آیکون اختصاصی برای exe، یک فایل `.ico` به پروژه اضافه کنید و در `IwanaProxy.csproj` خط `<ApplicationIcon>icon.ico</ApplicationIcon>` را تکمیل کنید.
