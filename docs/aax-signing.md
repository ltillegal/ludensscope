# AAX: как получить подпись PACE и заставить Pro Tools грузить плагин

AAX уже собирается (SDK входит в JUCE 8), но розничный Pro Tools грузит
только AAX, подписанный PACE. Неподписанный плагин не появляется в списке, на
части сборок с ошибкой **-7054**. Подпись делает `wraptool` из PACE Eden.

Заявку в PACE подаёт **Avid**, а не вы. Порядок ниже — фактический, по кейсу
марта 2026 ([[1]](https://note.com/kawato3/n/ne11473420ad5) и
[[2]](https://developer.avid.com/aax/)).

## Стоимость и сроки

| Пункт | Цена |
|---|---|
| Avid-аккаунт, AAX SDK, регистрация разработчика | 0 |
| Pro Tools Developer (для тестов неподписанного AAX) | 0 |
| PACE Eden Tools + PACE Central | 0 (платит Avid) |
| iLok 3 USB | ~50 € |
| Apple Developer Program | 99 $/год, нужен для нотаризации `.pkg` |
| Pro Tools | не нужен, хватает Developer-сборки |

Лицензии Eden Tools и PACE Central выдаются **на год** и продлеваются. Подпись
в течение года не ограничена по количеству.

Сроки: в удачном кейсе — неделя от заявки до подписи. По сообщениям на форумах
бывает и два месяца: Avid отвечает вручную. Планировать стоит на недели.

## Шаг 1. Avid-аккаунт и AAX SDK

1. Зарегистрируйте аккаунт на https://my.avid.com/
2. Откройте https://my.avid.com/cpp/sdk/aax — это click-through лицензия.
3. Примите соглашение. После этого SDK доступен в аккаунте, и вы —
   **зарегистрированный AAX-разработчик**. Качать SDK не нужно: он уже в JUCE,
   нужен только сам факт регистрации.

## Шаг 2. Лицензия Pro Tools Developer

Нужна, чтобы тестировать **неподписанный** AAX до получения PACE. Заявка:

- форма: https://forms.cloud.microsoft/r/Hgnye2RRYU
- либо письмо на `devauth@avid.com`

В письме ровно три пункта:

```
Subject: Pro Tools Developer license request

- Avid Developer account email: <email вашего my.avid.com>
- Number of licenses: 1
- Purpose: AAX plug-in development and testing
```

Автоответ обещает 5 рабочих дней, на практике активация приходит в тот же или
на следующий день. Лицензия кладётся на iLok (или iLok Cloud).

## Шаг 3. Заявка на PACE signing tools

Важно: **PACE напрямую заявку не принимает** — Avid связывается с ними за вас.

- форма: https://forms.cloud.microsoft/r/hCYdLUGaEt
- если регистрация разработчика свежая, подождите два рабочих дня
- либо письмо на `audiosdk@avid.com` с темой `PACE Eden Signing Tools Request`

Данные, которые спросят (готовьте сразу):

```
Company name:              LudensScope
Admin full name:           <имя фамилия>
Admin email:               alek.ludens@gmail.com
Phone (with country code): +<код><номер>
Website:                   https://ludensscope.pages.dev
Plugin description:        Stereo spectrum, loudness (BS.1770), VU and
                           stereo-field analyzer for mixing and mastering.
                           AU, VST3, AAX, CLAP, standalone.
Avid Developer email:      <email вашего my.avid.com>
Manufacturer code:         LDNS
Plugin code:               LDSC
```

`Company name` — это имя издателя, оно попадёт в PACE Central и в подпись.
Менять потом неприятно, выберите один раз. Для соло-разработчика нормально
указать собственный бренд: `LudensScope`.

Manufacturer и plugin code — те же, что в `CMakeLists.txt`. Они регистрируются
у Avid при выдаче инструментов подписи и после релиза меняться не должны:
по ним Pro Tools находит плагин в старых сессиях.

## Шаг 4. Что приходит от PACE

PACE присылает письмо со ссылкой на форму (на заполнение ~72 часа) и после неё:

- администраторский аккаунт **PACE Central** (central.paceap.com);
- лицензия **PACE Central Access** — на iLok-аккаунт;
- лицензия **Eden Tools** — на iLok-аккаунт.

Дальше:

1. В iLok License Manager перенести обе лицензии на **USB-ключ**.
2. **Правый клик по ключу → Synchronize.** Если пропустить, сертификат подписи
   не скачается на ключ и `wraptool` упадёт с ошибкой авторизации. Это главная
   ловушка всего процесса.
3. Установить **Eden SDK** — в нём `wraptool`.
4. В PACE Central создать *wrap configuration*, получить GUID (`--wcguid`).

## Шаг 5. Подпись

macOS (нужен Developer ID, если собираетесь нотаризовать):

```bash
export AAX_ACCOUNT="<iLok account>"
export AAX_KEYFILE="$HOME/.pace/eden.p12"
export AAX_KEYPASSWORD="<password>"
export AAX_WRAP_CONFIG="<wrap configuration GUID>"
export APPLE_SIGN_ID="Developer ID Application: ... (TEAMID)"

scripts/sign-aax-macos.sh
```

Windows:

```powershell
$env:AAX_ACCOUNT="..."; $env:AAX_KEYFILE="C:\pace\eden.p12"
$env:AAX_KEYPASSWORD="..."; $env:AAX_WRAP_CONFIG="..."

.\scripts\sign-aax-windows.ps1
```

Скрипты вызывают:

```
wraptool sign --verbose --account <acct> --keyfile <p12> --keypassword <pw> \
  --wcguid <GUID> --dsigharden --in <binary> --out <binary>
```

`--dsigharden` выставляет hardened runtime, нужный Apple для нотаризации.
Порядок жёсткий: **Apple-подпись → wraptool → и больше ничего**. Повторный
`codesign` после `wraptool` стирает PACE-wrap, и Pro Tools перестаёт видеть
плагин.

## Проверка

```bash
codesign -dv --verbose=4 dist/LudensScope.aaxplugin   # Developer ID + runtime
```

Затем Pro Tools: плагин должен появиться в меню и загрузиться. Нет плагина или
**-7054** — подпись не встала.

## Локальная сборка релиза

CI и локальная сборка используют один скрипт, поэтому установщик одинаковый:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target LudensScope_AAX

scripts/sign-aax-macos.sh build/LudensScope_artefacts/Release/AAX/LudensScope.aaxplugin \
                          dist/LudensScope.aaxplugin

AAX_SIGNED_BUNDLE=dist/LudensScope.aaxplugin scripts/package-macos.sh 1.1.0 build dist

gh release upload v1.1.0 dist/LudensScope-macos.pkg --clobber
```

Нотаризуется `.pkg`, а не плагин: без этого macOS ругается на установщик, но на
загрузку AAX в Pro Tools это не влияет.

## Про CI

GitHub-hosted runner подписать AAX не может: `wraptool` работает только там, где
воткнут iLok с лицензией `Eden Tools`. Поэтому CI собирает и пакует как обычно
(AAX внутри остаётся неподписанным), а подпись делается локально. Класть
PACE-сертификат в GitHub Secrets бессмысленно — без ключа он не работает.

## Границы

- Linux не поддерживается: Avid не выпускает AAX SDK под Linux.
- Windows-AAX собирается только под хост-архитектуру.
- Сертификацию плагина Avid **не** требует: соглашение — это click-through при
  получении SDK, после подписи можно публиковать (FAQ 4–5).
- iLok-защита продукта не обязательна, у нас свой офлайн-ключ (FAQ 11).
- Облачная подпись через Avid не выдаётся, только локальная; cloud signing —
  отдельный разговор с PACE (~1000 $), не нужен.
- Подпись привязана к конкретному бинарю: каждый релиз подписывается заново.
