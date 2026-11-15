# AAX: подпись PACE

AAX-плагин в наших `.pkg` / `.exe` собран, но **не подписан**. Pro Tools
загружает только подписанный AAX; подпись — это не отдельный файл, а
PACE-«wrap» сертификатом через `wraptool`.

## Что нужно получить

1. **Avid Developer / AAX SDK** — https://developer.avid.com/  
   AAX SDK уже входит в JUCE 8; для релиза AAX нужно одобрение Avid.
2. **PACE Eden** — доступ к PACE Anti-Piracy (opens через iLok):  
   https://www.paceap.com/ · https://www.ilok.com/
3. **PACE Central** — создать *wrap configuration* (GUID); открывается
   кнопкой из iLok License Manager.
4. **DSIG identity** (signing ID) — то, что подставляется в `--signid`.
5. **iLok** с лицензией Pro Tools Developer — для проверки до релиза.

Процесс хорошо расписан в [Dplug AAX Guide](https://github.com/AuburnSounds/Dplug/wiki/Dplug-AAX-Guide)
и на [JUCE forum](https://forum.juce.com/t/aax-plugin-eden-pace-wraptool-signid-your-dsig-id-what-exactly-is-dsig-id/59907).

## Сборка AAX

AAX собирается на macOS/Windows (на Linux — нет):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target LudensScope_AAX --config Release
```

Результат: `build/LudensScope_artefacts/Release/AAX/LudensScope.aaxplugin`.

## Подпись одной командой

macOS:

```bash
export AAX_SIGN_ID="<DSIG identity>"
export AAX_WRAP_CONFIG="<PACE Central wrap configuration GUID>"
# если wraptool не в PATH:
export AAX_WRAP_TOOL="/Applications/PACEAntiPiracy/Eden/Fusion/Versions/5/bin/wraptool"

scripts/sign-aax-macos.sh
# → dist/LudensScope.aaxplugin
```

Windows (PowerShell):

```powershell
$env:AAX_SIGN_ID="<DSIG identity>"
$env:AAX_WRAP_CONFIG="<PACE Central wrap configuration GUID>"
$env:AAX_WRAP_TOOL="C:\Program Files\PACE Anti-Piracy\...\wraptool.exe"

.\scripts\sign-aax-windows.ps1
```

Скрипты вызывают:

```bash
wraptool --signid "$AAX_SIGN_ID" --wrapConfig "$AAX_WRAP_CONFIG" dist/LudensScope.aaxplugin
```

Флаги `wraptool` зависят от версии Eden — если ваша версия просит другой
синтаксис, поправьте `WRAP_ARGS` в скрипте (или выполните команду из
`wraptool --help` вручную; скрипт — обёртка, не магия).

## Проверка

```bash
codesign -dv --verbose=2 dist/LudensScope.aaxplugin
```

Затем — Pro Tools Developer build с iLok: плагин должен появиться и
загрузиться. На обычном Pro Tools без подписи его не будет.

## Упаковка релиза

Подписанный `dist/LudensScope.aaxplugin` нужно положить в установщик
вместо неподписанного:

- macOS `.pkg`: заменить `pkgroot/Library/Application Support/Avid/Audio/Plug-Ins/LudensScope.aaxplugin`
  и пересобрать `pkgbuild`/`productbuild`;
- Windows `.exe`: заменить файл в NSIS-скрипте (`$COMMONFILES64\Avid\Audio\Plug-Ins`)
  и пересобрать `makensis`.

Либо добавить шаг подписи в CI (секреты `AAX_SIGN_ID` / `AAX_WRAP_CONFIG` /
`AAX_WRAP_TOOL`), но сертификат PACE в GitHub Secrets — на ваш риск.

## Границы

- Подпись AAX не делает плагин доступным без одобрения Avid.
- Linux-сборка AAX не поддерживается (Pro Tools — macOS/Windows).
- Наша `.pkg`/`.exe` сейчас содержат неподписанный AAX; это ожидаемо до
  получения PACE-доступа.
