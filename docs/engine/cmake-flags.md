# Флаги CMake и настройки релизного билда

Проверено 14.11.2026 по исходникам рабочей копии IX-Ray, HEAD `5ec6feae5`, включая локальные изменения модуля Tracy. Значения «по умолчанию» относятся к новому CMake cache; preset и ранее сохранённый cache могут их менять.

Колонка **«Для релизного билда»** рассчитана на обычный **Windows x64 single-player клиент для распространения**. Для MP, редакторов и утилит нужные компоненты включаются отдельно. Рекомендации по составу поставки не означают ускорение игрового кода.

## Опции проекта

Все `option()` из текущих CMake-файлов и строковый параметр unity-сборки. Основные объявления находятся в [CMakeLists.txt](../../CMakeLists.txt); `IXRAY_DISCORD_RPC` объявлен в [cmake/msvc.cmake](../../cmake/msvc.cmake).

| Флаг | По умолчанию | Что меняет при включении / изменении | Для релизного билда |
| --- | --- | --- | --- |
| `IXRAY_PROFILER` | `ON` | Подключает **Optick**: определение для C++, линковку `OptickCore` и копирование `OptickCore.dll`. Действует, если Tracy выключен. Штатные зоны `PROF_*` доступны только с `DEBUG_DRAW`; в `Release` они пустые, но CMake-зависимость остаётся. | **Выставить `OFF`**, чтобы убрать зависимость Optick. Preset Windows сам её не отключает. |
| `IXRAY_PROFILER_TRACY` | `OFF` | Подключает **Tracy 0.14.1** и `TracyClient`. Имеет приоритет над Optick, даже если оба флага `ON`. Штатные зоны также зависят от `DEBUG_DRAW`. | **Выставить `OFF`**. Для отдельной сборки профилирования использовать `ON` и `RelWithDebInfo`. |
| `IXRAY_ASAN` | `OFF` | Добавляет AddressSanitizer; в `xrCore` задаёт `PURE_ONLY`. На MSVC заменяет Debug `/ZI` на `/Zi`; в Clang вне Windows также добавляет флаг ASan при линковке. | **Оставить `OFF`**. ASan предназначен для отдельной диагностической сборки с полной пересборкой. |
| `IXRAY_LDEBUG` | `OFF` | Принудительно включает `LUABIND_DEBUG_SCRIPTS` в luabind. При `ON` MSVC-ветка перестаёт добавлять `/EHsc` для Debug. Не выбирает конфигурацию Debug и не является общим переключателем Lua-отладчиков. | **Оставить `OFF`**, если не нужна диагностика привязок Lua. |
| `DEVIXRAY_ENABLE_SHIPPING` | `OFF` | Добавляет отдельную конфигурацию `Shipping`, макросы `MASTER_GOLD` и `SHIPPING_BUILD`, собственные настройки компилятора. Сам флаг не выбирает эту конфигурацию. | **Оставить `OFF` и собирать `Release`**. Отличия Shipping приведены ниже. |
| `IXRAY_MP` | `OFF` | Включает MP-ветки сетевой и звуковой библиотек, голосовую связь и её зависимости, MP-зависимости `xrGame`; добавляет `xrRender_DS0`, `xrServer`, `xrServerCLI`, `xrGameSpy`. С `IXRAY_UTILS` добавляет MP-инструменты. | **`OFF` для SP**, **`ON` для MP-релиза**. Это функциональный выбор. |
| `IXRAY_MP_SQL` | `OFF` | При `IXRAY_MP=ON` включает SQL-код FreeMP, MySQL Connector и копирование его DLL/OpenSSL. С утилитами добавляет `SQL_Gen`. Без MP не действует. | **`OFF` для SP и MP без SQL**. `ON` только при использовании SQL-функций FreeMP. |
| `IXRAY_USE_R1` | `ON` | Добавляет `xrRender_R1`: D3D9, статическое освещение, режим `renderer_r1`. | Обычно **оставить `ON`** для поддержки режима и резервной загрузки R1. `OFF` допустим при намеренном сокращении поддерживаемых рендереров. |
| `IXRAY_USE_R2` | `ON` | Добавляет `xrRender_R2`: D3D9, динамическое освещение, режим `renderer_r2`. | Обычно **оставить `ON`** для поддержки режима и резервной загрузки R2. Отключение не ускоряет выбранный R4. |
| `IXRAY_USE_COMPRESSOR` | `ON` | Добавляет `xrCompress`, даже при `IXRAY_UTILS=OFF`. Если `IXRAY_UTILS=ON`, компрессор всё равно добавляется. | **Можно `OFF`** для пакета только движка. `ON`, если нужен компрессор; на скорость игры не влияет. |
| `IXRAY_COMPRESSOR_ONLY` | `OFF` | Убирает основные engine targets, кроме `xrCore`, и часть сторонних библиотек; startup project становится `xrCompress`. Для самого компрессора всё ещё нужен `IXRAY_USE_COMPRESSOR=ON` или `IXRAY_UTILS=ON`. | **Обязательно `OFF` для клиента**. Это режим отдельной сборки компрессора. |
| `IXRAY_UTILS` | `OFF` | Добавляет `xrCompress`, `xrDXT`, `xrLevelBuilder` (объединяет xrLC/xrAI), `xrSE_Factory`, `TextureCloner`, `EFC`, `xrLevelFilesConverter`; также подтягивает общие редакторские библиотеки. | **Оставить `OFF` для поставки клиента**. `ON` для отдельного пакета утилит. |
| `IXRAY_UTILS_OPTIX` | `OFF` | В `xrLevelBuilder` включает NVIDIA CUDA/OptiX для расчёта освещения. Требует Windows x64 + MSVC, CUDA ≥ 12.8 и OptiX ≥ 9; SDK загружаются или берутся из заданных каталогов. Генерирует `CuTrace.ptx`. | **`OFF` для клиента**. `ON` для пакета утилит с NVIDIA backend; не включает игровой RTX-режим. |
| `IXRAY_UTILS_VULKAN` | `OFF` | В `xrLevelBuilder` запрашивает Vulkan SDK с `glslangValidator` и включает расчёт освещения через Vulkan ray query. Если SDK не найден, выдаёт предупреждение и оставляет backend выключенным, несмотря на `ON`. | **`OFF` для клиента**. `ON` по требованиям пакета утилит; не включает Vulkan-рендерер игры. |
| `IXRAY_EDITORS` | `OFF` | Добавляет `ActorEditor`, `LevelEditor`, `ShaderEditor`, `ParticlesEditor`, `xrECore`, `xrEUI`, `xrEProps`, FileWatch; также `xrSE_Factory` и `xrDXT`, даже без полного пакета utils. | **Оставить `OFF` для поставки клиента**. `ON` для пакета редакторов. |
| `IXRAY_PLUGINS` | `OFF` | На x64 добавляет `PCore` и плагины Maya 2024–2027. Подключение 3ds Max в текущем CMake закомментировано. | **Оставить `OFF` для клиента**. `ON` для отдельной сборки Maya-плагинов. |
| `IXRAY_TESTS` | `OFF` | Добавляет `src/test/benchmark` и исполняемый файл `TestBenchmark`, связанный с `xrEngineCore`. | **Оставить `OFF` для игрового дистрибутива**. |
| `IXRAY_UNITYBUILD` | `ON` | Объединяет исходники **только `xrGame`** в unity-пакеты; для MSVC добавляет `/bigobj`. Некоторые исходники исключены из объединения. | Обычно **оставить `ON`**. `OFF` допустим для диагностики проблем компиляции; не является переключателем релизных оптимизаций. |
| `IXRAY_BUILD_BATCH_SIZE` | `32` | Размер unity-пакета `xrGame`, используется при `IXRAY_UNITYBUILD=ON`. Это число, а не `ON/OFF`. | **Оставить `32`** либо подобрать под память и параллельность машины сборки. |
| `IXRAY_DISCORD_RPC` | `ON` в MSVC-ветке | Определяет `IXR_ENABLE_DISCORD` для работы Discord activity. **При `OFF` Windows CMake всё равно подключает и линкует Discord SDK.** В других ветках компилятора опция автоматически не объявляется. | **По требованиям продукта**: `ON` для активности Discord, `OFF`, если она не нужна. Не обязательная релизная настройка. |
| `IXRAY_CI` | `OFF` | При `ON` пропускает подключение `cmake/clangtidy.cmake`. Также упомянут при задании внутреннего `IXR_TEST_CI`, но текущее выражение не вычисляет Boolean — см. ниже. | **`OFF` для локальной сборки**, `ON` для CI по требованиям процесса. Не выбирает Release. |
| `IXRAY_CROSS_COMPILATION` | `OFF` | Для Windows target меняет NuGet restore на Wine/winepath, создаёт обходные WindowsSDK targets, меняет обработку PCH. Сам по себе не выбирает компилятор и toolchain. | **`OFF` для нативной Windows-сборки**. `ON` только с соответствующим cross toolchain/preset. |
| `DEVIXRAY_ENABLE_OGL_RENDER` | `OFF` | Единственный потребитель пытается добавить `src/3rd-party/glew`; в текущем дереве этого каталога нет. Полноценный OpenGL renderer этот флаг не добавляет. | **Оставить `OFF`**. При обычной сборке `ON` приводит к ошибке отсутствующего подкаталога. |

Места применения: [engine targets](../../src/CMakeLists.txt), [xrCore / профилировщики и Discord](../../src/xrCore/CMakeLists.txt), [макросы профилирования](../../src/xrCore/profiler.h), [xrGame / unity](../../src/xrGame/CMakeLists.txt), [утилиты](../../src/utils/CMakeLists.txt), [GPU backend утилиты](../../src/utils/xrLevelBuilder/CMakeLists.txt), [редакторы](../../src/Editors/CMakeLists.txt), [плагины](../../src/plugins/CMakeLists.txt), [сеть / SQL](../../src/xrNetServer/CMakeLists.txt), [звук / MP](../../src/xrSound/CMakeLists.txt), [модули зависимостей](../../cmake/modules.cmake).

## Release, RelWithDebInfo и Shipping

Тип сборки выбирается отдельно от опций. Для Visual Studio и Ninja Multi-Config используется `cmake --build <каталог> --config Release` или соответствующий build preset. Одного `-DCMAKE_BUILD_TYPE=Release` для этих генераторов недостаточно. Для одноконфигурационного Ninja/Makefiles используется `-DCMAKE_BUILD_TYPE=Release` при конфигурации.

| Конфигурация | Макросы, задаваемые проектом | Проверки и профилирование | Назначение / рекомендация |
| --- | --- | --- | --- |
| `Debug` | `DEBUG`, `DEBUG_DRAW` | `VERIFY` и `R_ASSERT` работают. Доступны отладочная отрисовка и штатные зоны выбранного профилировщика. | Отладка. В MSVC явно добавляется `/Od`. |
| `RelWithDebInfo` | `DEBUG_DRAW` | `VERIFY` удаляется, `R_ASSERT` сохраняется. Отладочная отрисовка и штатные зоны выбранного профилировщика остаются. | Оптимизированная сборка для диагностики и профилирования. Не эквивалентна пользовательскому Release. |
| **`Release`** | `MASTER_GOLD` | `VERIFY` удаляется, `R_ASSERT` сохраняется. Проект не задаёт `DEBUG_DRAW`, штатные `PROF_*` пустые. | **Рекомендуемая конфигурация обычного релиза.** Для устранения profiler dependencies дополнительно выключить обе опции профилировщиков. |
| `Shipping` | `MASTER_GOLD`, `SHIPPING_BUILD` | `VERIFY` и `R_ASSERT` удаляются; `R_CHK` оставляет вычисление выражения без проверки результата. Штатные `PROF_*` пустые. | Только при осознанной необходимости этой конфигурации и отдельной проверке её настроек. Требует `DEVIXRAY_ENABLE_SHIPPING=ON`. |

В MSVC для `Release` и `RelWithDebInfo` проект добавляет `/Ot`, `/Ob2`, `/GF`, `/Oi`, `/Oy`, `/GT`, `/GL`, `/Zi`, а при линковке — `/LTCG:incremental` и `/INCREMENTAL:NO`. Наличие `/Zi` означает, что Release не следует описывать как сборку вообще без отладочной информации; выдача итогового linker PDB отдельно зависит от настроек линковки.

**Shipping не наследует набор настроек Release.** Его `CMAKE_CXX_FLAGS_SHIPPING` берётся из общих `CMAKE_CXX_FLAGS`, а не из `CMAKE_CXX_FLAGS_RELEASE`. Явно добавляются `/Ob2`, `/WX` и подавление ряда предупреждений; перечисленные выше Release-оптимизации не переносятся автоматически. Кроме того, Shipping-модуль задаёт MSVC-подобные ключи без ограничения по платформе. Поэтому включать Shipping только ради предполагаемой дополнительной оптимизации не следует.

Источники: [определения конфигураций](../../CMakeLists.txt), [MSVC](../../cmake/msvc.cmake), [Shipping](../../cmake/shipping.cmake), [VERIFY / R_ASSERT / R_CHK](../../src/xrCore/xrDebug_macros.h), [PROF_*](../../src/xrCore/profiler.h).

## Внутренние, дополнительные и устаревшие переменные

Эти имена могут встречаться в cache, presets или CI, но не все являются самостоятельными рабочими опциями проекта.

| Переменная | Умолчание / текущее значение | Реальное действие | Для релизного билда |
| --- | --- | --- | --- |
| `IXR_TEST_CI` | Строковый список `NOT;IXRAY_UTILS;OR;NOT;IXRAY_CI` | Корневой `set(IXR_TEST_CI NOT IXRAY_UTILS OR NOT IXRAY_CI)` **не вычисляет логическое выражение**. В текущих `if(IXR_TEST_CI)` этот непустой список истинен. Поэтому R4, `xrGame`, `xrPlay` включаются независимо от сочетания utils/CI, если `IXRAY_COMPRESSOR_ONLY=OFF`; MP-targets дополнительно требуют `IXRAY_MP`. | **Не задавать вручную**. Обычный `set()` затеняет переданный cache `-DIXR_TEST_CI=OFF`. |
| `IXRAY_USE_R4` | Не объявлен и не используется | Отдельного переключателя R4 в текущем дереве нет. На Windows `xrEngine` также явно зависит от `xrRender_R4`. | **Не задавать**: такой `-D` не управляет R4. |
| `IXRAY_USE_GL` | В Linux preset задан `ON` | В CMake-коде потребителя нет. Наличие в preset не включает OpenGL. | **Не использовать как рабочий флаг**. |
| `IXRAY_ENABLE_RESONANCEAUDIO` | В Linux CI передан `OFF` | Объявления и потребителей в текущем CMake нет. | **Не требуется**; это оставшийся параметр workflow. |
| `IXRAY_OFFLINE` | Не объявлен через `option()`, по умолчанию отсутствует / ложен | Только [NGX.cmake](../../cmake/modules/NGX.cmake) проверяет его перед загрузкой отсутствующего DLSS-архива. Остальные загрузки проекта этим флагом не запрещаются. | **Не связан с Release**. Использовать при заранее подготовленном DLSS-кэше; это не глобальный offline-режим. |
| `IXR_ARM_ENABLE` | Вычисляется MSVC-веткой из `CMAKE_GENERATOR_PLATFORM` | Внутренний признак ARM64, влияет на настройки и зависимости. | **Не задавать вручную**; для рассматриваемого релиза выбирать платформу x64. |
| `IXR_CONFIGURATIONS_STR`, `CMAKE_CONFIGURATION_TYPES` | `Debug;RelWithDebInfo;Release`, с опцией Shipping добавляется `Shipping` | Проект задаёт списки через `CACHE ... FORCE`. Второй параметр задаёт набор конфигураций у многоконфигурационных генераторов. | **Не переопределять**. Выбирать `Release` при сборке. |
| `CMAKE_BUILD_TYPE` | Зависит от генератора и cache | Тип сборки одноконфигурационного генератора. Visual Studio и Ninja Multi-Config выбирают конфигурацию через `--config`. | **`Release` только для single-config**. Для Windows preset использовать build preset `Engine-x64-Windows-Release`. |
| `CMAKE_CXX_STANDARD`, `CMAKE_CXX_EXTENSIONS` | Проект задаёт `23`, `OFF` | Требует C++23 без расширений языка; значения заданы обычным `set()` в корне. | **Оставить как в проекте**. Это не параметры оптимизации Release. |
| `CMAKE_EXPORT_COMPILE_COMMANDS` | Проект задаёт `ON` | Запрашивает базу команд компиляции у поддерживающих её генераторов. | **Оставить**; на поведение собранной игры не влияет. |
| `CMAKE_RUNTIME_OUTPUT_DIRECTORY`, `CMAKE_LIBRARY_OUTPUT_DIRECTORY`, `CMAKE_ARCHIVE_OUTPUT_DIRECTORY` | Задаются compiler-модулем относительно binary directory | Каталоги EXE/DLL и библиотек. Для MSVC runtime output — `<binaryDir>/bin/<Config>/`. | **Не считать универсальным путём `build/bin/Release`**: конкретный путь зависит от выбранного preset / `-B`. |

Проект использует CMake ≥ 3.26. Для нативного Windows x64 пример ниже опирается на существующий VS 2022 preset. Другие toolchain требуют своих настроек; приведённые MSVC-ключи к ним автоматически не относятся. Linux RHI подключает DXVK Native независимо от опции Vulkan для `xrLevelBuilder` — см. [xrRHI](../../src/xrRHI/CMakeLists.txt).

## Параметры SDK для утилит

Эти пути читаются при `IXRAY_UTILS_OPTIX=ON`; для игрового клиента задавать их не нужно. [Подготовка NVIDIA SDK](../../cmake/modules/NvidiaOptix.cmake), [поиск OptiX](../../cmake/finders/FindOptiX.cmake).

| Параметр | Умолчание / источник | Что меняет | Для релизного билда |
| --- | --- | --- | --- |
| `CUDAToolkit_ROOT` | При отсутствии выбранного SDK используются закреплённые загрузки | Корень установленного CUDA toolkit; поддерживается также переменная окружения. Нужна версия ≥ 12.8. | **Не задавать для клиента**. Для утилит — при использовании своего CUDA SDK. |
| `OptiX_ROOT`, `OptiX_INCLUDE_DIR` | Поиск установленного SDK либо закреплённая загрузка | Корень OptiX SDK или каталог заголовков `optix.h`; нужна версия ≥ 9. Явный корень имеет приоритет над ранее сохранённым include path. | **Не задавать для клиента**. Для утилит — при использовании своего OptiX SDK. |
| `IXRAY_NVIDIA_DEPS_DIR` | `<binaryDir>/_deps/nvidia` | Каталог загружаемых инструментов и SDK NVIDIA. | **Оставить для утилит**. Этот build-каталог не включается в распространяемый пакет. |
| `IXRAY_CUDA_LICENSE_FILE`, `IXRAY_OPTIX_LICENSE_FILE` | Автопоиск сопровождающих лицензий SDK | Явные пути к лицензиям для нестандартного размещения установленных SDK. | **Не задавать для клиента**. Для утилит — при необходимости; сохранять подготовленные runtime-файлы и `licenses/NVIDIA/`. |

## Параметры зависимостей, принудительно заданные проектом

Здесь перечислены переключатели зависимостей, которые модули проекта явно фиксируют через `CACHE ... FORCE`. Это не независимые настройки релиза IX-Ray: попытка передать противоположное значение через `-D` будет перезаписана при исполнении соответствующего модуля. Полный набор опций скачиваемых сторонних проектов в эту таблицу не входит.

| Параметр | Значение проекта | Что меняет | Для релизного билда |
| --- | --- | --- | --- |
| `TRACY_ENABLE` | `1` | Включает клиент Tracy, когда подключён его модуль. | Отключать **`IXRAY_PROFILER_TRACY`**, а не этот параметр. |
| `TRACY_STATIC` | `OFF` | Не запрашивает статический вариант клиента. Фактический тип target также зависит от настроек самого Tracy, включая LTO. | Отдельно не задавать. |
| `TRACY_NO_VSYNC_CAPTURE` | `1` | Отключает захват аппаратных событий VSync. | Отдельно не задавать. |
| `TRACY_ON_DEMAND` | `1` | Запись профилирования по подключению клиента просмотра. | Отдельно не задавать. |
| `TRACY_CALLSTACK` | `1` в модуле | Задаёт параметр стеков Tracy; в C++ интеграции также есть переопределение глубины в `8`. Не означает единую глубину всех зон. | Отдельно не задавать. |
| `TRACY_FIBERS` | `1` | Включает поддержку fibers для маркеров выполнения задач. | Отдельно не задавать. |
| `LUABIND_DEBUG_SCRIPTS` | Следует `IXRAY_LDEBUG` | Диагностика привязок Lua. | Управлять через **`IXRAY_LDEBUG=OFF`**. |
| `LUABIND_TESTAPP`, `LUABIND_USE_EXTERNAL_LUA` | `OFF`, `OFF` | Отключают соответствующие режимы сборки luabind; пути и библиотека Lua задаются модулями IX-Ray. | Оставить значения проекта. |
| `SDL_SHARED`, `SDL_STATIC`, `SDL_TEST_LIBRARY` | `ON`, `OFF`, `OFF` | Собирается shared SDL3 без static SDL и тестовой библиотеки. | Оставить значения проекта. |
| `SDL_REVISION` | `3.2.22` | Строка ревизии SDL; архив SDL также закреплён на release 3.2.22. | Оставить значение проекта. |
| `ALSOFT_UTILS`, `ALSOFT_EXAMPLES`, `ALSOFT_TESTS` | Все `OFF` | При сборке OpenAL Soft из исходников отключают дополнительные утилиты, примеры и тесты. Не относятся к готовому Windows NuGet-пакету OpenAL. | Оставить значения проекта. |
| `LUNASVG_BUILD_EXAMPLES` | `OFF` | Отключает примеры lunasvg. | Оставить значение проекта. |
| `BUILD_X86`, `BUILD_X64`, `BUILD_DX12` | `OFF`, `ON`, `OFF` | В не-Windows ветке модуля DirectXMesh задают варианты **этой зависимости**. `BUILD_DX12` не управляет рендерером IX-Ray. | Оставить значения проекта; архитектуру движка выбирать через generator/preset. |
| `BUILD_TOOLS`, `BUILD_TESTING` | `OFF`, `OFF` | В той же ветке DirectXMesh отключают его инструменты и тесты. | Оставить; тесты IX-Ray управляются через **`IXRAY_TESTS`**. |
| `CMAKE_POLICY_VERSION_MINIMUM` | `3.5`, если выполняется fallback сборки OpenAL Soft | Позволяет подключить старый CMake-проект зависимости. Не меняет минимальную требуемую версию CMake самого IX-Ray. | Отдельно не задавать. |

Источники: [Tracy](../../cmake/modules/Tracy.cmake), [luabind](../../cmake/modules/LuaBind.cmake), [SDL3](../../cmake/modules/SDL3.cmake), [OpenAL Soft](../../cmake/modules/OpenalSoft.cmake), [lunasvg](../../cmake/modules/lunasvg.cmake), [DirectXMesh](../../cmake/modules/DirectXMesh.cmake).

## Готовая команда: Windows x64 SP Release

Запускать из корня репозитория в PowerShell. Отдельный `-B` сохраняет релизные настройки в своём cache и не переключает существующий рабочий cache на сборку без профилировщиков. Пример сохраняет R1/R2/R4, unity-сборку и Discord activity; дополнительные инструменты выключены.

```powershell
cmake --preset Engine-x64-Windows -B build/x64/Engine-Windows-Release `
  -DIXRAY_PROFILER=OFF `
  -DIXRAY_PROFILER_TRACY=OFF `
  -DIXRAY_ASAN=OFF `
  -DIXRAY_LDEBUG=OFF `
  -DDEVIXRAY_ENABLE_SHIPPING=OFF `
  -DIXRAY_MP=OFF `
  -DIXRAY_MP_SQL=OFF `
  -DIXRAY_USE_R1=ON `
  -DIXRAY_USE_R2=ON `
  -DIXRAY_USE_COMPRESSOR=OFF `
  -DIXRAY_COMPRESSOR_ONLY=OFF `
  -DIXRAY_UTILS=OFF `
  -DIXRAY_UTILS_OPTIX=OFF `
  -DIXRAY_UTILS_VULKAN=OFF `
  -DIXRAY_EDITORS=OFF `
  -DIXRAY_PLUGINS=OFF `
  -DIXRAY_TESTS=OFF `
  -DIXRAY_UNITYBUILD=ON `
  -DIXRAY_BUILD_BATCH_SIZE=32 `
  -DIXRAY_DISCORD_RPC=ON `
  -DIXRAY_CI=OFF `
  -DIXRAY_CROSS_COMPILATION=OFF `
  -DDEVIXRAY_ENABLE_OGL_RENDER=OFF

cmake --build build/x64/Engine-Windows-Release --config Release --parallel
```

Результат этого примера: `build/x64/Engine-Windows-Release/bin/Release/`. Поскольку `-B` меняет каталог относительно preset, в примере сборка запускается по этому каталогу, а не через build preset с прежним binary directory.

Если использовать стандартный каталог preset без `-B`, сборку выбирает `cmake --build --preset Engine-x64-Windows-Release --parallel`, а результат находится в `build/x64/Engine-Windows/bin/Release/`.

**Выбор Release сам по себе не отключает Optick в configure preset.** [CMakePresets.json](../../CMakePresets.json) явно задаёт `IXRAY_PROFILER=ON` в `Engine-Base-Windows`; build preset `Engine-x64-Windows-Release` выбирает только конфигурацию. Старый [CMakeSettings.json](../../CMakeSettings.json) содержит другое значение для своего Release-профиля и не заменяет настройки `cmake --preset`.

Проверка документа выполнена чтением CMake-файлов и мест использования макросов. Команды приведены как инструкция; конфигурация, компиляция и запуск игры в рамках подготовки этой таблицы не выполнялись.
