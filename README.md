# LudensScope

Минималистичный стерео-анализатор в формате **VST3**: спектр, громкость по
ITU-R BS.1770-4, true-peak, векторскоп и осциллограф.

Без пресетов, без лицензирования, без триалов, без телеметрии. В состояние
проекта пишутся только снапшоты FFT (A–D) — в остальном это чистый
измерительный прибор.

## Что внутри

| Модуль | Файлы |
|---|---|
| LUFS (M/S/I, гейтинг), K-weighting, LRA | `Source/dsp/LoudnessMeter.*` |
| Спектр: FFT 4096, Hann, лог-шкала 20 Гц – 20 кГц, инспектор, усреднение и референс | `Source/dsp/SpectrumAnalyzer.*`, `Source/ui/SpectrumView.h` |
| True-peak: 4x oversampling, polyphase FIR | `Source/dsp/TruePeakDetector.h` |
| PLR и счётчик клиппинга | `Source/PluginProcessor.*`, `Source/PluginEditor.cpp` |
| UI: спектр, водопад, метры, гониометр, осциллограф | `Source/ui/*` |

Аудио-поток только фильтрует и считает энергию; FFT и отрисовка идут в UI-потоке
(данные передаются через lock-free `AbstractFifo`). Никаких блокировок в
`processBlock`.

## Сборка (Linux)

```bash
cmake --preset default
cmake --build --preset default -j$(nproc)
```

или без пресетов:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

JUCE (8.0.15) подтягивается автоматически через `FetchContent`. Готовый плагин
копируется в `~/.vst3/LudensScope.vst3`.

Проверка калибровки (опционально):

```bash
cmake --preset dev
cmake --build --preset dev -j$(nproc)

# LUFS (BS.1770-4) + true-peak: 10 проверок
./build/LudensScopeLoudnessCalibration

# калибровка спектра: синус -6 dBFS должен читаться около -6 dBFS
./build/LudensScopeCalibration_artefacts/Release/LudensScopeCalibration

# рендер UI в PNG без DAW (проверка, что редактор не падает)
./build/LudensScopeUiSnapshot_artefacts/Release/LudensScopeUiSnapshot
# -> docs/ui-preview.png (или путь из первого аргумента)
```

## Использование

1. Поставь плагин на мастер-шину или на группу.
2. Спектр: лог-ось, шкала dBFS, жёлтая линия — peak hold. Колесо мыши над
   спектром — зум в октавах вокруг курсора, двойной клик — полный диапазон
   (20 Гц – 20 кГц). Кнопка `SPAN` переключает 10/8/6/4/2/1 октавы, водопад
   следует за выбранным диапазоном. Наведи курсор — под ним частота, нота,
   уровень и peak. Ниже — водопад (история около 7 секунд), его останавливает
   `FREEZE`. Высоту водопада можно менять, перетаскивая разделитель; двойной
   клик возвращает норму. Вверху спектра — HUD-пилюли с балансом LOW/MID/HIGH
   в процентах энергии. `GUIDES` включает подложку с зонами SUB / KICK / BASS /
   SNARE / VOX / PRES / AIR — подсказка, где обычно живут инструменты.
   Кнопка `FFT` открывает настройки анализатора: размер окна 2048–16384,
   скорость сглаживания (Fast / Medium / Slow) и наклон +3 или +4.5 дБ/окт,
   чтобы высокие частоты читались выразительнее.
3. LOUDNESS: momentary (400 мс), short-term (3 с), integrated (с гейтингом
   -70 LUFS / -10 LU). Клик по `TGT` в шапке выбирает цель (−14/−16/−18/−23),
   на полосе integrated появляется метка, под ней — отклонение в LU.
4. TRUE PEAK — 4x передискретизация, dBTP. Красный при > -1 dBTP.
   LRA — диапазон громкости (EBU 3342), PLR — peak-to-loudness, CLIP — число
   сэмплов ≥ 0 dBFS и intersample-оверов.
5. STEREO IMAGE — гониометр со шлейфом, OSCILLOSCOPE — с триггером по переходу
   через ноль. Колесо над осциллографом меняет окно времени (до ~2.7 с),
   Shift+колесо — вертикальный масштаб, двойной клик — сброс. В плашке —
   PEAK / RMS / CREST / LUFS по видимому окну. `MONO` — анализ суммы каналов
   для проверки моно-совместимости.
6. Снапшоты FFT: клик по букве `A`–`D` в шапке спектра выбирает слот, `CAPTURE`
   записывает в него усреднённый спектр. Заполненные слоты рисуются цветными
   пунктирами, инспектор показывает уровень и дельту к выбранному слоту.
   `REF FILE` — загрузить wav/aiff/flac: его усреднённая кривая рисуется серым,
   а после загрузки на спектре появляется транспорт (play/stop, `LOOP`, `A/B`,
   время); громкость референса подгоняется под текущий integrated микса. Файлы
   длиннее 10 минут обрезаются. `FREEZE` — стопор кадров, `RESET` — сброс
   пиков, громкости, снапшотов, референса и счётчиков.

## macOS + Cubase

Да, формат VST3 Cubase поддерживает и на Intel, и на Apple Silicon. Но собрать
плагин нужно **на macOS** — кросс-компиляция с Linux не поддерживается.
Исходники платформонезависимые (чистый C++ + JUCE), Linux-специфичного кода нет.

```bash
xcode-select --install   # если ещё нет Command Line Tools

# всё сразу: configure + build + ad-hoc подпись + установка
./scripts/build-macos.sh
```

Вручную:

```bash
cmake --preset macos-universal
cmake --build --preset macos-universal -j$(sysctl -n hw.ncpu)

# локальная ad-hoc подпись (обязательна для arm64)
codesign --force --deep --sign - \
  build/LudensScope_artefacts/Release/VST3/LudensScope.vst3
```

`COPY_PLUGIN_AFTER_BUILD` кладёт бандл в `~/Library/Audio/Plug-Ins/VST3/`
(для всех пользователей — `/Library/Audio/Plug-Ins/VST3/`).

В Cubase: `Studio → VST Plug-in Manager → Rescan`, либо просто перезапустить
Cubase. Плагин появится в категории `Analyzer`.

Замечания:

- Универсальный бинарник (`arm64;x86_64`) нужен, чтобы работало и на M-чипах,
  и на Intel.
- Для раздачи другим людям — Developer ID + notarization. Для себя достаточно
  ad-hoc подписи.
- AU не собирается (Cubase он не нужен). Для Logic добавь `AU` в `FORMATS`.
- Проверено только на Linux; macOS-сборка ожидаемо рабочая, но на железе не
  тестировалась.

## Ограничения

- Только стерео (2 in / 2 out), только VST3.
- Пресетов нет намеренно: в состоянии проекта хранятся только снапшоты FFT.
- AAX/AU не собираются.
