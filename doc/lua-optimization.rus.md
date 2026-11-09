# Как оптимизировать Lua-скрипт в IX-Ray 1.6

Представим панель снабжения в CoP. Пока панель открыта, она показывает количество аптечек и бинтов в инвентаре и расстояние до ближайшего живого сталкера в радиусе 25 метров. Состав инвентаря должен отразиться к ближайшему обновлению актёра. Для расстояния допустимы замеры раз в 250 мс игрового времени устройства. Это требования примера. С них начинается выбор оптимизации.

В первой реализации один обработчик при каждом обновлении актёра обходит инвентарь, ищет сталкеров и пересобирает текст. Даже если игрок стоит на месте и ничего не подбирает, все три операции повторяются. Мы оставим каждому виду работы свой повод для запуска. Изменился инвентарь — пометим счётчики для пересчёта. Настал срок замера — запросим окружение. Изменилось число на экране — обновим текст.

| Часть | Что выясняем |
| --- | --- |
| [1. Замер](#1-замер) | Как отделить дорогой вызов от слишком частого |
| [2. События и время](#2-события-и-время) | Когда пересчитывать инвентарь, расстояние и текст |
| [3. Поиск и сборка панели](#3-поиск-и-сборка-панели) | Как сузить запрос и связать обработчики в одном файле |
| [4. Запуск и жизненный цикл](#4-запуск-и-жизненный-цикл) | Где загружать настройки, что сбрасывать и что сохранять |
| [5. Рост нагрузки](#5-рост-нагрузки) | Как пережить большое число NPC, условий и задач |
| [6. Разбор оставшихся пиков](#6-разбор-оставшихся-пиков) | Как различить работу Lua, C++ и сборщика мусора |

Основной маршрут — первые три части. Дальше разбираем, как добавить настройки и сохранение, как масштабировать механику и куда смотреть при оставшихся пиках. В каждом рецепте два фрагмента. Подпись объясняет, куда ставить код или какие данные ему передать. В третьей части есть [собранный скрипт панели](#собранный-скрипт-панели) с кодом от подписки до чтения данных в UI. Остальные фрагменты показывают отдельные варианты, которые выбирают по задаче.

Контракты сверены с `default-dev` на `543beff26`. Игровой Lua здесь работает на LuaJIT с API Lua 5.1. В примерах с событиями используется IXR Framework из этого репозитория. SGM и другие аддоны могут подменить места отправки событий и обработчики. Сверяйте именно файл, который загружается в вашей сборке.

## 1. Замер

Движок связывает игровой объект актёра с **биндером** — Lua-объектом, который хранит его скриптовое состояние. При обновлении биндер отправляет `actor_on_update`, а Framework вызывает подписанную функцию. Такая функция называется **callback**, или обработчик. В ней `binder.object` даёт сам игровой объект. Этот путь идёт через планировщик объектов, поэтому число вызовов обработчика измеряем отдельно от числа отрисованных кадров.

Вызов `binder.object:iterate_inventory(...)` обращается через luabind к C++. **Luabind** связывает Lua-методы и объекты движка с их реализацией. Обход внутри C++ затем вызывает Lua для каждого предмета. При поиске по уровню Lua запускает запрос и перебирает результаты. Поэтому ищем две величины — сколько раз запустили работу и сколько занял один запуск.

См. [акторский callback](../gamedata/scripts/bind_stalker.script#L554), [биндер и планировщик](../src/xrGame/script_binder.cpp#L154), [обход инвентаря](../src/xrGame/script_game_object_inventory_owner.cpp#L328).

### 1. Посчитайте запуски и измерьте один вызов

Для замера возьмите один повторяемый проход: та же сборка, сохранение, маршрут и состояние панели. Первый вызов может включать загрузку скрипта или прогрев JIT, поэтому записывайте его отдельно. На прогретом проходе сопоставляйте число обходов и поисков, обычное время кадра и хвост распределения. p95 означает время, в которое уложились 95 кадров из 100, p99 показывает более редкие задержки.

**Пример A · Вставка в исходный update.** Разместите этот блок после функций `recount_supplies` и `find_nearest_stalker` из рецептов 4 и 5. В своём обработчике `actor_on_update` временно замените обход и поиск одним вызовом `counted_update(binder)`. Обновление надписей оставьте в прежнем месте. Из точки завершения замера в том же файле один раз вызовите `report_work()`. После оптимизации увеличивайте счётчики внутри веток, которые действительно запускают обход или поиск.

```lua
local passes = { updates = 0, inventory = 0, searches = 0 }
local counts = { medkit = 0, bandage = 0 }
local nearest_m

local function counted_update(binder)
    passes.updates = passes.updates + 1
    local actor = binder.object

    passes.inventory = passes.inventory + 1
    recount_supplies(actor, counts)

    passes.searches = passes.searches + 1
    nearest_m = find_nearest_stalker(actor:position(), 25)
end

local function report_work()
    log1(string.format("supply updates=%d inventory=%d searches=%d",
        passes.updates, passes.inventory, passes.searches))
end
```

Если до правки за проход было 200 вызовов `update`, исходный вариант запустит 200 обходов и 200 поисков. После правки смотрите на каждый счётчик отдельно. Например, десять секунд игрового времени дадут порядка 40 поисков при периоде 250 мс и достаточно частом update.

**Пример B · Вставка вокруг поиска.** Для замера временно вызывайте `measured_search` вместо `find_nearest_stalker`. После серии вызовов выполните `report_search()`. Время включает сам запрос и перебор результата. У `profile_timer` показания накопительные, в микросекундах. Разница до и после вызова даёт время одной операции. Снимите значения после прогретой серии. Для холодного замера начните новую серию.

```lua
local search_timer = profile_timer()
local search_calls, peak_us = 0, 0

local function measured_search(center, radius)
    local before = search_timer:time()
    search_timer:start()
    local distance = find_nearest_stalker(center, radius)
    search_timer:stop()

    search_calls = search_calls + 1
    peak_us = math.max(peak_us, search_timer:time() - before)
    return distance
end

local function report_search()
    if search_calls == 0 then return end
    log1(string.format("supply scan: n=%d avg=%.1f us max=%.1f us",
        search_calls, search_timer:time() / search_calls, peak_us))
end
```

В данном замере `find_nearest_stalker` выполняется синхронно. Для обработки ошибок при измерении см. рецепт 22. Выигрыш проверяйте повторным проходом в игре. Отдельный таймер функции и время кадра отвечают на разные вопросы.

См. [таймер и единицы](../src/xrScripts/script_engine_script.cpp#L87), [экспорт `profile_timer` и `log1`](../src/xrScripts/script_engine_script.cpp#L329).

В следующей части уменьшим число обходов и поисков. Оставьте эти счётчики на один повторный прогон, чтобы увидеть изменение без догадок по FPS.

## 2. События и время

Предположим, счётчики показывают, что панель запускает обход и поиск на каждом update. У этих задач разные причины меняться. Инвентарь меняется дискретно, поэтому события могут поставить флаг. Расстояние зависит от движения двух объектов, поэтому его удобно измерять с выбранным периодом. Когда обе величины готовы, UI решает, нужно ли менять надписи.

### 2. Пересчитывайте инвентарь по событию

У событий взятия, выбрасывания и использования предмета одна задача — поставить `inventory_dirty`. Если между обновлениями было три события, флаг всё равно один и обработчик обойдёт инвентарь один раз. Первый обход нужен и без событий, чтобы показать предметы, уже лежащие в инвентаре при загрузке.

**Пример A · Фрагмент `.script`.** Здесь считаем только объекты секции `medkit`. Это имя аптечки в LTX. Так проще проследить флаг от подписки до пересчёта. Сочетание аптечек и бинтов появится в рецепте 4.

```lua
local inventory_dirty = true
local medkit_count = 0

local function invalidate()
    inventory_dirty = true
end

local function count_item(_, item)
    if item:section() == "medkit" then
        medkit_count = medkit_count + 1
    end
    return false -- true завершает обход
end

local function refresh(binder)
    if not inventory_dirty then return end
    medkit_count = 0
    binder.object:iterate_inventory(count_item, binder)
    inventory_dirty = false
end

local function clear_snapshot()
    inventory_dirty = true
    medkit_count = 0
end

function get_medkit_count()
    return medkit_count
end

function on_game_start()
    RegisterScriptCallback("actor_on_first_update", invalidate)
    RegisterScriptCallback("actor_on_item_take", invalidate)
    RegisterScriptCallback("actor_on_item_drop", invalidate)
    RegisterScriptCallback("actor_on_item_use", invalidate)
    RegisterScriptCallback("actor_on_update", refresh)
    RegisterScriptCallback("actor_on_net_destroy", clear_snapshot)
end
```

**Пример B · Собственное событие.** Допустим, другой мод хочет знать, когда панель впервые нашла сталкера или снова потеряла его. Передавайте в `set_alert` флаг `nearest_m ~= nil` после замера. Рассылка произойдёт только при переходе.

```lua
local event_name = "my_mod_alert_changed"
local alert = false
local alert_changes = 0

local function on_alert_changed()
    alert_changes = alert_changes + 1
end

function set_alert(value)
    if alert == value then return end
    alert = value
    SendScriptCallback(event_name, value)
end

function stop_counting_alerts()
    UnregisterScriptCallback(event_name, on_alert_changed)
end

function on_game_start()
    AddIntercept(event_name, {"boolean"})
    RegisterScriptCallback(event_name, on_alert_changed)
end
```

На этой ветке `RegisterScriptCallback` и `SendScriptCallback` работают через `ixr_signals`. Обработчики вызываются синхронно, в порядке обхода `pairs`. Их результаты игнорируются, а Lua-ошибка может оборвать рассылку. Для снятия подписки храните ту же функцию. Новых слушателей добавляйте вне рассылки этого сигнала. Собственное имя сначала объявите через `AddIntercept`. Для штатных сигналов сверяйте отправителя: `actor_on_update` передаёт биндер и `delta`, а совместимый `update` передаёт только `delta`. В конкретном аддоне проверьте все пути, которыми меняется интересующий вас инвентарь.

См. [callback API](../gamedata/scripts/ixray_system/dynamic_callbacks.lua#L13), [диспетчер сигналов](../gamedata/scripts/ixr_framework/modules/ixr_module_signals.script#L442), [события актёрского биндера](../gamedata/scripts/bind_stalker.script#L227), [порядок обновлений](../gamedata/scripts/bind_stalker.script#L554).

### 3. Дайте поиску свой интервал

Для расстояния флага инвентаря недостаточно: игрок и NPC двигаются независимо от этих событий. Ставим проверку периода перед запросом. Через 250 мс новый замер становится разрешён, а выполняется он на первом подходящем обновлении актёра. При долгом разрыве берём один свежий снимок. Выполнять цепочку замеров за все пропущенные интервалы бесполезно, потому что прошлых позиций уже нет.

`time_global()` даёт время устройства в миллисекундах. Его ход связан с паузой и масштабированием времени устройства. `delta`, полученный биндером, описывает интервал планировщика, который в обычной ветке может быть ограничен. Поэтому здесь сравниваем два значения часов. `time_global_async()` обращается к другому таймеру устройства. Для календарного часа Зоны есть отдельные часы `game.get_game_time()` со своим масштабом.

**Пример A · Вставка в update панели.** Передайте в `sample_nearest` функцию `find_nearest_stalker` из рецепта 5. До срока функция возвращает `false`. После замера она возвращает `true` и расстояние, которое может быть `nil`, если вокруг никого нет. Сбросьте `last_scan` при открытии панели и при смене сессии.

```lua
local interval_ms = 250
local clock_modulus = 4294967296
local last_scan

local function sample_nearest(actor, find_nearest)
    local now = time_global()
    if last_scan and (now - last_scan) % clock_modulus < interval_ms then
        return false
    end

    last_scan = now
    return true, find_nearest(actor:position(), 25)
end

local function reset_clock()
    last_scan = nil
end
```

Первый вызов проходит сразу. Остаток по модулю `2^32` сохраняет правильную разницу при переходе таймера через ноль. Такой срок живёт в памяти текущей сессии. Для симуляции, в которой требуется обработать каждый пропущенный шаг, нужен отдельный предел шагов за вызов и правило для остатка.

**Пример B · Календарь для другой задачи.** Допустим, напоминание о снабжении можно показать повторно через игровой час. Вызывайте `try_use` при попытке показать напоминание. Здесь время живёт до уничтожения акторского биндера. Если оно должно пережить загрузку, ему нужен собственный формат сохранения.

```lua
local used_at

function try_use()
    if not db.actor then return false end
    local now = game.get_game_time()
    if used_at and now >= used_at and now:diffSec(used_at) < 3600 then
        return false
    end
    used_at = now
    return true
end

local function reset()
    used_at = nil
end

function on_game_start()
    RegisterScriptCallback("actor_on_net_destroy", reset)
end
```

См. [планировщик](../src/xrEngine/xrSheduler.cpp#L305), [часы устройства](../src/xrScripts/script_engine_script.cpp#L167), [время и паузу устройства](../src/xrEngine/device.cpp#L477), [календарь](../src/xrGame/xr_time.cpp#L15), [экспорт `CTime`](../src/xrGame/level_script.cpp#L2199).

### 4. Соберите счётчики за один обход и обновляйте изменившиеся надписи

Флаг уменьшил число обходов. Внутри одного обхода сразу посчитаем обе секции. Это даст актуальную пару чисел. Поиск сталкера добавит третье значение, и интерфейс сможет прочитать весь снимок без запросов к миру.

**Пример A · Подсчёт по флагу из рецепта 2.** Передайте актёра и одну таблицу с полями `medkit` и `bandage`. Считаются объекты инвентаря. Если ваша механика отображает число порций в стопке, берите количество из контракта соответствующего предмета.

```lua
local function count_selected(counts, item)
    local section = item:section()
    local count = counts[section]
    if count ~= nil then counts[section] = count + 1 end
    return false
end

-- Один расчёт для нескольких секций, по событию изменения инвентаря
-- или при открытии панели. counts принадлежит этой панели или расчёту.
local function recount_supplies(owner, counts)
    counts.medkit = 0
    counts.bandage = 0
    owner:iterate_inventory(count_selected, counts)
end
```

`iterate_inventory(callback, context)` вызывает `callback(context, item)` для каждого предмета. `true` завершает обход. Для рюкзака есть `iterate_ruck`, для отдельного слота — `item_in_slot(slot_id)`. Если нужен состав нескольких секций, один обход выгоднее нескольких поисков по инвентарю. Изменяйте сам инвентарь после выхода из итератора.

**Пример B · Код интерфейса.** При создании окна передайте `window` и два виджета надписей `CUIStatic` в `make_supply_presenter`. Созданную функцию вызывайте в существующем обновлении окна. Она читает готовые данные из полного `my_supply_panel.script` ниже. `nil` в счётчике означает, что первый снимок ещё не получен. `nil` только в расстоянии означает отсутствие цели.

```lua
local function make_supply_presenter(window, stock_label, distance_label)
    local shown_medkit, shown_bandage, shown_distance
    return function()
        if not window:IsShown() then return end
        local medkit, bandage, nearest_m = my_supply_panel.read()

        local medkit_key, bandage_key = medkit or false, bandage or false
        if medkit_key ~= shown_medkit or bandage_key ~= shown_bandage then
            local text = medkit and string.format("%d / %d", medkit, bandage) or "..."
            stock_label:SetText(text)
            shown_medkit, shown_bandage = medkit_key, bandage_key
        end

        local distance_key = nearest_m or false
        if distance_key ~= shown_distance then
            distance_label:SetText(nearest_m and string.format("%d m", nearest_m) or "--")
            shown_distance = distance_key
        end
    end
end
```

Создавайте presenter заново при пересоздании окна. Если другой код меняет тот же виджет, ему и presenter нужен один владелец значения или явный сброс кэша. Для `SetTextST` учитывайте смену локализации. `CUILines::SetText` уже отсекает повтор одинаковой непустой строки. Сравнение чисел в Lua позволяет раньше пропустить и форматирование, и вызов C++.

См. [доступ к слоту](../src/xrGame/script_game_object_inventory_owner.cpp#L1538), [поиск по секции](../src/xrGame/Inventory.cpp#L1675), [обход инвентаря](../src/xrGame/script_game_object_inventory_owner.cpp#L328), [видимость окна](../src/xrUI/Widgets/UIWindow_script.cpp#L185), [экспорт текста](../src/xrUI/Widgets/UIStatic_script.cpp#L33), [обновление текста](../src/xrUI/Widgets/UILines.cpp#L86).

Теперь известен момент пересчёта инвентаря и период поиска. Осталось удешевить один запуск поиска и соединить эти решения в одном файле.

## 3. Поиск и сборка панели

Интервал ограничивает число запусков поиска. Теперь ограничим работу **одного** запуска. Панели нужны сталкеры текущего уровня внутри 25 метров. Такие объекты называют **онлайн**: у них сейчас есть объект в загруженном мире. Серверный реестр ALife включает и сущности вне активной части мира. Для расстояния на HUD начинаем с пространственного запроса по текущему уровню.

### 5. Ищите сталкеров в нужной области

Поиск панели начинается со сферы и маски `STALKER_ALIVE`. Маска отсеивает типы объектов на стороне движка. Попадание в выборку означает пересечение пространственной сферы объекта с областью запроса. Проверим точное расстояние до позиции сами, чтобы соблюсти правило 25 метров.

Итератор использует буфер запроса. Обходите его целиком в текущем синхронном вызове. Вложенный вызов того же запроса или сохранение итератора до следующего update может подменить результаты. Для отложенной работы сохраняйте нужные числа или ID с учётом их срока жизни.

**Пример A · Сам поиск панели.** Снимите позицию актёра один раз и передайте её вместе с радиусом. В цикле сравниваются квадраты расстояний. Корень вычисляется один раз для победителя, затем расстояние округляется для UI.

```lua
local stalker_types = { level.e_spatial_type.STALKER_ALIVE }

local function find_nearest_stalker(center, radius)
    if not level.present() or radius <= 0 then return nil end

    local best_sq
    local radius_sq = radius * radius
    for npc in level.search_online_objects_by_sphere(center, radius, stalker_types) do
        local dist_sq = center:distance_to_sqr(npc:position())
        if dist_sq <= radius_sq and (best_sq == nil or dist_sq < best_sq) then
            best_sq = dist_sq
        end
    end
    return best_sq and math.floor(math.sqrt(best_sq) + 0.5) or nil
end
```

Если нужны несколько типов, таблица масок объединяет их через OR. Для точки в прицеле можно прочитать уже готовый результат HUD через `level.get_target_obj()`. При совместимости со старым `nearest` перебирайте индексы от 0 до `nearest.size() - 1` и завершайте обход до следующего `nearest.set`.

**Пример B · Когда действительно нужен луч.** Если для подписи нужна высота над землёй, запускайте короткий луч после отбора интересующих точек. Пример рассчитан на статическую геометрию.

```lua
local ground_ray = ray_pick()
ground_ray:set_flags(rq_target.rqtStatic)
ground_ray:set_direction(vector():set(0, -1, 0))

-- Вызывайте после дешёвого отбора кандидатов, в игровом контексте.
local function distance_to_ground(origin, max_distance)
    if not level.present() or max_distance <= 0 then return nil end

    ground_ray:set_position(origin)
    ground_ray:set_range(max_distance)
    if ground_ray:query() then
        return ground_ray:get_distance()
    end
    return nil
end
```

Держите луч коротким, используйте нормализованное направление и проверяйте результат `query()` перед чтением полей. В этой реализации `ray_pick` при любом попадании обращается к массиву статических треугольников через `R.element`. У динамического попадания другое значение индекса, поэтому пример использует только `rqtStatic`. При промахе в переиспользованном объекте остаётся старый результат. `npc:see(target)` обращается к памяти видимости AI, а для проверки геометрии нужен запрос с подходящей семантикой.

См. [сферический запрос](../src/xrGame/level_script.cpp#L1667), [условия обхода сферы](../src/xrCore/Collision/ISpatial_q_box.cpp#L145), [экспорт луча](../src/xrGame/level_script.cpp#L2125), [результат луча](../src/xrGame/raypick.cpp#L28), [видимость NPC](../src/xrGame/script_game_object.cpp#L262), [цель HUD](../src/xrGame/level_script.cpp#L1050), [старый nearest](../src/xrGame/level_script.cpp#L1130).

### 6. Повторно используйте снимок позиции и рабочий вектор

В предыдущем поиске один снимок позиции актёра обслуживает всю выборку. Это важно, потому что метод `game_object`, чтение компоненты `vector.x` и вызов метода `vector` проходят через luabind. `obj:position()` возвращает вектор по значению. Снимайте позицию один раз на расчёт, выносите общие для всей пачки значения из цикла. Для порога сравнивайте квадрат расстояния с квадратом радиуса. Для длинного чисто численного расчёта выгодно один раз перенести компоненты в Lua и проверить эффект по профилю. Шесть чтений полей ради одного сравнения легко обходятся дороже одного `distance_to_sqr`.

**Пример A · Уже есть список кандидатов.** Считайте позицию владельца один раз и примените квадрат радиуса ко всему списку.

```lua
-- owner и кандидаты получены в текущем игровом вызове.
-- candidates — плотный массив. Радиус задан в метрах.
local function count_in_range(owner, candidates, radius)
    local center = owner:position()
    local radius_sq = radius * radius
    local count = 0

    for i = 1, #candidates do
        local position = candidates[i]:position()
        if center:distance_to_sqr(position) <= radius_sq then
            count = count + 1
        end
    end
    return count
end
```

**Пример B · Вспомогательная геометрия.** Например, для стрелки направления можно переиспользовать рабочий вектор внутри одной синхронной операции.

```lua
-- Создайте функцию один раз для владельца расчёта.
-- Входные векторы принадлежат вызывающему коду.
local function make_forward_projection()
    local delta = vector()

    return function(origin, target, forward)
        delta:sub(target, origin)
        return delta:dotproduct(forward)
    end
end

local project_forward = make_forward_projection()
-- project_forward(origin, target, unit_forward) даёт проекцию в метрах.
```

Методы `set`, `sub`, `add` и `normalize` меняют вектор-получатель. Рабочий вектор держите у владельца расчёта и используйте внутри синхронного вызова. Если потребителю нужен самостоятельный снимок, передайте числа или скопируйте вектор через `vector():set(source)`. Иначе последующее обновление рабочего буфера изменит и данные потребителя.

См. [экспорт методов объектов](../src/xrGame/script_game_object_script2.cpp#L89), [позиция по значению](../src/xrGame/script_game_object.cpp#L61), [операции с векторами](../src/xrScripts/exports/script_fvector_script.cpp#L20).

### Собранный скрипт панели

В этом файле можно проследить весь путь данных. `on_game_start` подписывает обработчики. События инвентаря ставят флаг. `update` обрабатывает его и проверяет срок замера расстояния. `read` возвращает три готовых значения. Сам поиск использует локальный объект сталкера только внутри цикла.

```lua
-- my_supply_panel.script
local SCAN_MS = 250
local SCAN_RADIUS = 25
local U32_MOD = 4294967296

local counts = { medkit = 0, bandage = 0 }
local inventory_dirty = true
local panel_open, ready = false, false
local last_scan, nearest_m
local stalker_types

local function invalidate_inventory()
    inventory_dirty = true
end

local function count_item(result, item)
    local section = item:section()
    local n = result[section]
    if n ~= nil then result[section] = n + 1 end
    return false
end

-- Inventory events are coalesced into one pass.
local function refresh_inventory(actor)
    if not inventory_dirty then return end
    counts.medkit, counts.bandage = 0, 0
    actor:iterate_inventory(count_item, counts)
    inventory_dirty = false
end

-- A due scan reads the level and keeps only a number for the UI.
local function refresh_nearest(actor)
    local now = time_global()
    if last_scan and (now - last_scan) % U32_MOD < SCAN_MS then return end
    last_scan = now

    local center = actor:position()
    local radius2 = SCAN_RADIUS * SCAN_RADIUS
    local best2
    for npc in level.search_online_objects_by_sphere(center, SCAN_RADIUS, stalker_types) do
        local d2 = center:distance_to_sqr(npc:position())
        if d2 <= radius2 and (best2 == nil or d2 < best2) then best2 = d2 end
    end
    nearest_m = best2 and math.floor(math.sqrt(best2) + 0.5) or nil
end

local function update(binder)
    if not panel_open then return end

    local actor = binder.object
    refresh_inventory(actor)
    refresh_nearest(actor)
    ready = true
end

local function reset()
    counts.medkit, counts.bandage = 0, 0
    inventory_dirty, ready = true, false
    last_scan, nearest_m = nil, nil
end

local function on_session_end()
    panel_open = false
    reset()
end

function on_game_start()
    stalker_types = { level.e_spatial_type.STALKER_ALIVE }
    RegisterScriptCallback("actor_on_item_take", invalidate_inventory)
    RegisterScriptCallback("actor_on_item_drop", invalidate_inventory)
    RegisterScriptCallback("actor_on_item_use", invalidate_inventory)
    RegisterScriptCallback("actor_on_first_update", reset)
    RegisterScriptCallback("actor_on_update", update)
    RegisterScriptCallback("actor_on_net_destroy", on_session_end)
end

function set_open(value)
    if panel_open == value then return end
    panel_open = value
    reset()
end

function read()
    if not ready then return nil, nil, nil end
    return counts.medkit, counts.bandage, nearest_m
end
```

Поместите файл в каталог `scripts` аддона, где его может найти автозагрузчик IXR Framework. В обработчике открытия окна вызовите `my_supply_panel.set_open(true)`, при закрытии или уничтожении окна — `my_supply_panel.set_open(false)`. При обновлении окна используйте presenter из рецепта 4. Первый вызов `read()` до обновления актёра даёт три `nil`. Когда замер готов, первые два значения — числа, а третье остаётся `nil`, если в радиусе нет сталкеров. Разметка и подключение окна относятся к конкретному интерфейсу мода.

Откройте панель, подберите аптечку и бинт и проверьте оба счётчика. Если два события пришли до одного обновления актёра, ожидайте один обход. Затем отойдите от сталкеров: на следующем замере индикатор расстояния должен очиститься. По счётчикам рецепта 1 проверьте, что после закрытия окна обходы и поиски прекращаются. После загрузки сохранения откройте окно снова и проверьте первый снимок.

См. [перехваты актёрских событий](../gamedata/scripts/__ixr_override_signals_intercepts.script#L99), [порядок вызова](../gamedata/scripts/bind_stalker.script#L554), [поиск объектов](../src/xrGame/level_script.cpp#L1667).

Этот пример связывает события, таймер и интерфейс. Следующая часть нужна, когда появятся настройки, связанные объекты или прогресс, который должен пережить загрузку.

## 4. Запуск и жизненный цикл

Панель с фиксированными параметрами уже показывает результат. Чтобы превратить её в часть мода, нужно решить, когда появляется сам модуль, когда читаются его правила и когда устаревают данные. Удобно мыслить границами: загрузка модуля, открытие панели, завершение игровой сессии и сохранение. Всё, что требуется только на одной границе, выносим из регулярного обновления.

### 7. Разделите интерфейс модуля и его состояние

Первый вопрос при подключении панели — как Framework находит `on_game_start` и как UI вызывает `my_supply_panel.read()`. Обе функции доступны через пространство имён файла. Локальные счётчики принадлежат самому модулю.

Движок создаёт таблицу пространства имён по имени файла без каталога и расширения, добавляет локальные `this` и `script_name()` и задаёт окружение через `setfenv`. Поиск недостающего имени в пространстве имён переходит в `_G`. Оставляйте рабочие переменные и функции локальными, а функции, которые ищут движок, конфиги или другие скрипты, экспортируйте под ожидаемым именем. Результат верхнего уровня чанка отбрасывается, поэтому `return M` сам по себе ничего не экспортирует. Для `_g.script` используется глобальное окружение. Окружение обычного модуля тоже имеет доступ к общим `io`, `os`, `debug` и `ffi`, которые открывает VM.

**Пример A.** Файл `mymod_progress_utils.script` экспортирует только нормализацию. Из другого скрипта вызов выглядит как `mymod_progress_utils.normalize(35, 100)`.

```lua
local min, max = math.min, math.max

local function clamp_unit(value)
    return max(0, min(1, value))
end

function normalize(value, limit)
    if limit <= 0 then
        return 0
    end
    return clamp_unit(value / limit)
end
```

**Пример B.** Файл `mymod_visibility_utils.script` даёт одной реализации локальное имя для внутренних вызовов и публичное имя для потребителей. Конфигурацию можно поменять явно.

```lua
local max_distance_sq = 25 * 25

local function is_visible(distance_sq)
    return distance_sq <= max_distance_sq
end

this.is_visible = is_visible

function set_max_distance(meters)
    assert(meters >= 0)
    max_distance_sq = meters * meters
end

function count_visible(distances_sq)
    local count = 0
    for i = 1, #distances_sq do
        if is_visible(distances_sq[i]) then
            count = count + 1
        end
    end
    return count
end
```

См. [Подстановка окружения](../src/xrScripts/script_storage.cpp#L19-L27), [исполнение чанка](../src/xrScripts/script_storage.cpp#L371-L403), [особый случай `_g`](../src/xrScripts/script_engine.cpp#L349-L364).

### 8. Выберите момент первой загрузки

Если панель обзавелась отдельным построителем строк, его первое обращение может попасть в момент открытия интерфейса. Выбираем, на какой границе загрузится зависимость.

При обращении к отсутствующему имени `_G.__index` ищет одноимённый `.script` в индексе и может синхронно прочитать, скомпилировать и выполнить его. Имена файлов должны быть уникальны по базовому имени даже в разных папках. Держите верхний уровень лёгким и убирайте циклы между чанками. Движок публикует пространство имён до завершения чанка, поэтому зависимость в цикле может увидеть таблицу без части экспортов. `prefetch` использует тот же загрузчик и позволяет выбрать момент первой загрузки обязательного модуля. Для проверки **уже загруженной** необязательной интеграции подойдёт `rawget`. Учтите сканирование Framework из следующего рецепта.

**Пример A.** Вызовите `prepare_panel` в фазе открытия панели до первого обновления. `mymod_panel_utils.script` и его `build_rows` обозначают модуль и функцию вашего аддона. Вызов `prefetch` только загружает чанк, построение строк выполняется отдельным вызовом.

```lua
local function prepare_panel()
    prefetch("mymod_panel_utils")
    local panel = rawget(_G, "mymod_panel_utils")
    local build_rows = panel and rawget(panel, "build_rows")
    assert(type(build_rows) == "function", "mymod_panel_utils.build_rows")
    return build_rows()
end
```

**Пример B.** Уведомляйте необязательную интеграцию, если она уже загружена. `mymod_panel_bridge` и `on_count_changed` обозначают интерфейс аддона. Проверка при событии видит модуль, даже если его загрузили позже.

```lua
local function notify_loaded_bridge(count)
    local bridge = rawget(_G, "mymod_panel_bridge")
    if type(bridge) ~= "table" then
        return
    end
    local notify = rawget(bridge, "on_count_changed")
    if type(notify) == "function" then
        notify(count)
    end
end
```

См. [Индекс и загрузка](../src/xrScripts/script_engine.cpp#L146-L207), [публикация пространства имён](../src/xrScripts/script_storage.cpp#L19-L27), [экспорт `prefetch`](../src/xrScripts/script_engine_script.cpp#L82-L85).

### 9. Различайте загрузку Lua, движка и Framework

Есть три механизма, которые легко спутать. Движок открывает `.script` через пространство имён, Lua загружает библиотеки через `require`, а Framework сам ищет модули с `on_game_start`. От этого зависит, сработает ли отложенная загрузка.

`require` использует `package.loaded` и загрузчики Lua. Обычный `require` работает с доступными Lua-модулями, а `.script` в VFS подключается через пространство имён движка. IXR Framework дополнительно перечисляет скрипты и ищет `on_game_start`. Для проверки он обращается к `_G[имя]`, поэтому сканирование может выполнить верхний уровень файла даже при отсутствии `on_game_start`. Оставляйте в хуке короткую регистрацию и откладывайте дорогую подготовку до подходящего события. Для служебных модулей есть штатные исключения, среди них `*_lib` и `*_utils`. Пользовательское исключение влияет на сканирование Framework, а обращение к пространству имён по-прежнему может загрузить файл через движок. Порядок `on_game_start` для разных файлов не гарантирован, список собирается через `pairs`.

**Пример A.** В примере bootstrap регистрирует чистый Lua-модуль через `package.preload`. Реальный `.lua`-файл с доступным загрузчиком может возвращать такую же таблицу. Загружайте модуль один раз перед серией вычислений.

```lua
package.preload["mymod.scale"] = function()
    local function to_percent(value)
        return math.floor(value * 100 + 0.5)
    end

    return { to_percent = to_percent }
end

local scale = require("mymod.scale")
assert(scale.to_percent(0.24) == 24)
```

**Пример B.** Если тяжёлый модуль `mymod_panel_assets.script` имеет собственную точку запуска и загрузка должна происходить по требованию, добавьте строку в существующую `configure` файла [`__ixr_override_autoload_system.script`](../gamedata/scripts/__ixr_override_autoload_system.script#L10-L18). Сохраните действующие правила остальных модов и загрузите модуль явно в нужной фазе.

```lua
-- Добавить внутрь существующей configure(_ref_ixr_autoloader).
_ref_ixr_autoloader.ignore_script("mymod_panel_assets")
```

См. [Загрузка `package`](../src/xrScripts/script_storage.cpp#L62-L75), [отбор и вызов Framework](../gamedata/scripts/ixr_framework/modules/ixr_module_autoloader.script#L262-L337), [проверка имени через `_G`](../gamedata/scripts/ixr_framework/utils/ffx_callable_utils.script#L47-L84), [штатные исключения](../gamedata/scripts/ixr_framework/modules/ixr_module_autoloader.script#L53-L69).

### 10. Прочитайте радиус из LTX при настройке

Теперь вместо фиксированных 25 метров добавим настройку радиуса. Значение читается и проверяется при подготовке панели. Поиск сталкеров получает готовое число.

`system_ini()` возвращает указатель на уже загруженный `pSettings`, а `ini_file(path)` открывает и разбирает файл в `$game_config$`. Чтение отдельных ключей тоже выполняет поиск в конфиге. Выносите открытие и проверки из горячего цикла. Кэшируйте готовые числа, строки и флаги. Ограничивайте кэш секциями вашей подсистемы и пересобирайте его после смены конфигурации. `reload_system_ini()` уничтожает прежний объект, поэтому долгоживущая Lua-ссылка на старый INI теряет действительность.

**Пример A · Кэш радиусов для нескольких секций.** Функция загружает каждое значение при первом обращении. Если первый вызов приходится на update, прогрейте нужные секции на фазе настройки.

```lua
local scan_radius_by_section = {}

-- scan_radius — пример ключа вашего аддона в секции объекта.
local function scan_radius_for(section)
    local cached = scan_radius_by_section[section]
    if cached ~= nil then return cached end

    local ini = system_ini()
    local radius = 20
    if ini:line_exist(section, "scan_radius") then
        local configured = tonumber(ini:r_string(section, "scan_radius"))
        if configured and configured > 0 and configured <= 100 then
            radius = configured
        end
    end
    scan_radius_by_section[section] = radius
    return radius
end

local function invalidate_scan_radii()
    scan_radius_by_section = {}
end
```

**Пример B · Настройка одного интервала.** Для отдельного файла аддона проверьте существование через VFS, затем прочитайте ключ. Передайте результат в проверку периода из рецепта 3.

```lua
-- Имя файла и [scanner] interval_ms — пример формата вашего аддона.
local function load_scan_interval()
    local filename = "my_addon_scan_rules.ltx"
    if not getFS():exist("$game_config$", filename) then return 250 end

    local ini = ini_file(filename)
    if ini:line_exist("scanner", "interval_ms") then
        local ms = tonumber(ini:r_string("scanner", "interval_ms"))
        if ms and ms >= 50 and ms <= 5000 then
            return math.floor(ms)
        end
    end
    return 250
end

local interval_ms = load_scan_interval() -- При инициализации, вне update.
```

Для необязательного ключа достаточно `line_exist`. В этой ветке он проверяет наличие секции сам. Для обязательных полей можно проверять весь набор один раз при инициализации и сообщать о проблеме там же. Отсутствующие поля способны вызвать нативный fatal при прямом чтении. Для пользовательских чисел `tonumber` с проверкой диапазона позволяет отличить нечисловую строку от валидного нуля, тогда как `r_float` использует `atof`.

См. [объект и перезагрузка системного INI](../src/xrScripts/exports/script_ini_file_script.cpp#L15), [конструктор файлового INI](../src/xrScripts/exports/script_ini_file.cpp#L20), [проверки и чтение](../src/xrCore/xr_ini.cpp#L494), [преобразование float](../src/xrCore/xr_ini.cpp#L631), [VFS](../src/xrScripts/exports/fs_registrator_script.cpp#L243).

### 11. Сбрасывайте данные при исчезновении объекта

В собранном варианте панель хранит только расстояние и счётчики. Если понадобится кнопка взаимодействия с найденным NPC или отложенное действие, время жизни ссылки становится частью задачи.

В момент `net_destroy` движок уже убирает объект из online-реестра уровня и затем уничтожает Lua-обёртку. Сохранённый userdata и проверка `obj ~= nil` не гарантируют живой C++-объект. Используйте переданный объект внутри текущего вызова. Для отложенной работы сохраняйте ID и метку поколения, снимайте записи по событию уничтожения, затем повторно получайте объект по ID. Движок переиспользует ID. Для сюжетного объекта, который переживает переход offline, ищите серверную сущность по назначенному `story_id` и получайте её клиентский объект только по необходимости. У серверного объекта `se.id` — поле, у клиентского `obj:id()` — метод.

`alife():object(id)` читает серверный реестр, `level.object_by_id(id)` — online-объекты уровня. `alife():object(name)` линейно перебирает серверный реестр. Для online-состояния NPC подходят `npc_on_net_spawn` и `npc_on_net_destroy`. Очистку состояния, рассчитанного на серверную сущность, привязывайте к её фактическому снятию с регистрации, если её класс отправляет `server_entity_on_unregister`. Если вы владеете собственным биндером, ставьте прямой `obj:set_callback` на фазе инициализации и снимайте при уничтожении. Это один C++-слот на тип callback и объект. Подписка через Lua-сигнал позволяет нескольким обработчикам работать с уже занятым слотом.

**Пример A.** Билет для отложенной задачи по online-NPC. Создавайте его только для объектов, замеченных подпиской на `npc_on_net_spawn`. Задача после выхода NPC в offline или следующего спавна получит `nil`.

```lua
local online = {}

local function mark_spawn(npc)
    online[npc:id()] = {} -- уникальная метка текущего online-периода
end

local function mark_destroy(npc)
    online[npc:id()] = nil
end

local function reset()
    online = {}
end

function make_npc_ticket(npc)
    local id = npc:id()
    local generation = online[id]
    if generation then
        return {id = id, generation = generation}
    end
end

function get_ticket_health(ticket)
    if not ticket or not ticket.generation or online[ticket.id] ~= ticket.generation then
        return nil
    end
    if not level.present() then return nil end
    local npc = level.object_by_id(ticket.id)
    if npc then return npc.health end
end

function on_game_start()
    RegisterScriptCallback("npc_on_net_spawn", mark_spawn)
    RegisterScriptCallback("npc_on_net_destroy", mark_destroy)
    RegisterScriptCallback("actor_on_net_destroy", reset)
end
```

**Пример B.** Подписка на использование объекта, чей биндер принадлежит вашему моду. Вызовите `bind_use` после успешного `net_spawn`, а `unbind_use` при `net_destroy`, пока у биндера есть живой `self.object`. Для объектов со штатным биндером используйте рассылаемое им событие.

```lua
local function on_use(binder, object, user)
    binder.use_count = binder.use_count + 1
end

local function bind_use(binder)
    binder.use_count = 0
    binder.object:set_callback(callback.use_object, on_use, binder)
end

local function unbind_use(binder)
    binder.object:set_callback(callback.use_object)
    binder.use_count = nil
end
```

См. [Разрушение CGameObject](../src/xrGame/GameObject.cpp#L120), [Online-поиск](../src/xrGame/level_script.cpp#L240), [Серверный поиск](../src/xrGame/alife_simulator_script.cpp#L37), [Возврат ID в пул](../src/xrGame/xrServer.cpp#L1093), [NPC net_spawn и net_destroy](../gamedata/scripts/xr_motivator.script#L261), [Снятие серверного сталкера](../gamedata/scripts/se_stalker.script#L129), [Реализация set_callback](../src/xrGame/script_game_object_use.cpp#L227), [Аргументы use_callback](../gamedata/scripts/bind_physic_object.script#L151).

### 12. Выберите объект уровня или запись ALife

Кнопке взаимодействия нужен живой объект уровня. Подпись или маршрут до сюжетного поставщика может понадобиться, даже когда он вне уровня. От этого зависит выбор между online-реестром и ALife.

`level.object_by_id(id)` ищет объект текущего уровня по сетевому ID. `alife():object(id)` обращается к серверному реестру, где объект может быть офлайн. Строковая форма `alife():object(name)` перебирает весь реестр и сравнивает имена. Для повторяющихся запросов сохраняйте числовой ID в пределах жизненного цикла сущности или используйте сюжетный идентификатор. `alife():objects()` полезен для редких аудитов. Его итератор обходится сразу, без изменений серверного реестра в теле цикла.

**Пример A · Текущий объект уровня.** Привязка устанавливается при появлении объекта. При его исчезновении владелец вызывает очистку, поэтому следующий запрос вернёт `nil`.

```lua
-- Вызывайте из обработчиков появления, исчезновения и выхода из уровня.
local tracked_id

local function track_online_object(obj)
    tracked_id = obj:id()
end

local function clear_tracked_object()
    tracked_id = nil
end

local function tracked_position()
    if tracked_id == nil or not level.present() then return nil end

    local obj = level.object_by_id(tracked_id)
    if obj then return obj:position() end
    return nil
end
```

**Пример B · Постоянная сюжетная цель.** Получите серверный ID и, если цель сейчас на уровне, её игровую обёртку. Используйте возвращённый online-объект в текущем вызове.

```lua
-- story_id — числовой сюжетный ID, зарегистрированный в story_ids.
-- Текущий уровень нужен только для запроса онлайн-представления.
local function resolve_story_target(story_id)
    local sim = alife()
    if not sim then return nil end

    local server_obj = sim:story_object(story_id)
    if not server_obj then return nil end

    local id = server_obj.id
    local online = level.present() and level.object_by_id(id) or nil
    return id, online
end
```

Lua-обёртка `game_object` удаляется движком при `net_Destroy`. Наличие Lua-ссылки само по себе не продлевает жизнь объекта. Сохраняйте ID, сбрасывайте привязку при удалении, выходе с уровня и загрузке, заново находите объект прямо перед обращением. ID может быть переиспользован. Для длительной задачи нужна привязка к конкретной игровой сущности, например сюжетный ID, с повторным разрешением. У серверной сущности `id` — поле, у `game_object` `id()` — метод.

См. [онлайн-поиск](../src/xrGame/level_script.cpp#L240), [реестр сетевых ID](../src/xrEngine/xr_object_list.h#L63), [поиск в ALife](../src/xrGame/alife_simulator_script.cpp#L42), [удаление обёртки](../src/xrGame/GameObject.cpp#L149), [серверные поля](../src/xrServerEntities/xrServer_Objects_script.cpp#L111).

### 13. Сохраните прогресс и восстановите рабочие данные

Расстояние и состав инвентаря после загрузки можно пересчитать. А вот факт, что игрок уже видел обучающую подсказку панели, относится к прогрессу и переживает загрузку.

Пишите только данные, необходимые для продолжения игры. Позиции для подсказок, индексы, очереди отрисовки и вычисляемые поля можно собрать заново. Используйте собственные имена ключей, значения по умолчанию и версию структуры. Обычный запуск ALife переинициализирует игровую VM. При флаге `keep_lua` она сохраняется. Сбрасывайте данные текущей сессии явно на границах её жизненного цикла. Восстанавливайте чистые данные на `load_state`, а работу с online-объектами начинайте после их появления. `load_state` вызывается из загрузчика ALife до чтения реестра объектов. Обработчики `actor_on_save` и `actor_on_load` получают общий пакет и reader. Независимым аддонам нельзя полагаться на порядок подписчиков и добавлять несогласованные поля в общий поток.

`xr_logic.pstor_store` рассчитан на bool, string, number и удаление через `nil`. Число пишется как `float`, поэтому для точных больших целых лучше подобрать иное представление. В этой ветке Framework сериализует общее Lua-хранилище через `marshal` в блок основного файла сохранения. Имена старых sidecar-файлов в комментариях и логах сами по себе не означают запись в отдельный файл. В сохраняемые таблицы кладите обычные значения. Сырые userdata, замыкания с объектами и рабочие кэши привяжите к текущей сессии. Избегайте копирования больших таблиц при каждом изменении и проверяйте сохранение, загрузку старого сейва, новую игру и переход уровня.

**Пример A.** Один постоянный флаг в акторском `pstor`. Вызовы предназначены для игровой фазы после появления актёра, где для него уже создано `db.storage`.

```lua
local key = "my_mod_tutorial_seen"

function set_tutorial_seen()
    local actor = db.actor
    if actor then
        xr_logic.pstor_store(actor, key, true)
    end
end

function is_tutorial_seen()
    local actor = db.actor
    return actor ~= nil and xr_logic.pstor_retrieve(actor, key, false) == true
end
```

**Пример B.** Компактный квестовый счётчик с поддержкой старого поля `done`. В сохраняемую таблицу входит собственная версия и один счётчик.

```lua
local key = "my_mod_checkpoint_progress"
local count = 0

function record_checkpoint()
    count = count + 1
end

local function save_state(data)
    data[key] = {version = 2, count = count}
end

local function load_state(data)
    count = 0
    local state = data[key]
    if type(state) ~= "table" then return end

    if state.version == 2 and type(state.count) == "number" then
        count = state.count
    elseif state.version == 1 and state.done == true then
        count = 1
    end
end

local function new_game()
    count = 0
end

function on_game_start()
    RegisterScriptCallback("save_state", save_state)
    RegisterScriptCallback("load_state", load_state)
    RegisterScriptCallback("new_game_created", new_game)
end
```

См. [Переинициализация VM](../src/xrGame/alife_simulator.cpp#L29), [Акторское сохранение и загрузка](../gamedata/scripts/bind_stalker.script#L564), [Типы и упаковка pstor](../gamedata/scripts/xr_logic.script#L1356), [События save_state и load_state](../gamedata/scripts/alife_storage_manager.script#L54), [Запись Framework-хранилища](../gamedata/scripts/ixr_framework/modules/ixr_module_storage.script#L193), [Marshal-блок и порядок загрузки объектов](../src/xrGame/alife_storage_manager.cpp#L114).

### 14. Перенесите чтение и запись файлов за пределы update

Появилась функция экспорта диагностического отчёта. Она пишет отдельный файл по запросу. Правила игрового сохранения остаются в предыдущем рецепте, а частые обновления работают с готовыми данными.

Чтение файлов, обход каталогов, разбор JSON/XML и запись на диск занимают время в вызывающем Lua-контексте. Загружайте данные при включении функции, смене конфигурации или подготовке уровня. На тике используйте подготовленное представление в памяти. Ограничивайте объём, проверяйте ошибки, закрывайте файлы. Для игрового прогресса используйте жизненный цикл сохранений из рецепта 13, а для отчётов и экспорта выделяйте свой путь.

**Пример A · Lua. Чтение небольшого настроечного файла на границе загрузки.** `path` задаётся модом, `parse` принадлежит модулю и получает закрытый к этому моменту файл в виде строки. Состояние модуля заменяйте после успешного разбора.

```lua
local function load_config(path, parse)
    local limit = 64 * 1024
    local file, open_error = io.open(path, "rb")
    if not file then
        return nil, open_error
    end

    local data, read_error = file:read(limit + 1)
    file:close()
    if not data then
        return nil, read_error or "empty config"
    end
    if #data > limit then
        return nil, "config exceeds byte limit"
    end

    return parse(data)
end
```

**Пример B · Lua. Пакетная запись отчёта по запросу.** `rows` — уже ограниченный массив готовых строк диагностики, `path` — путь отдельного отчёта. Для штатного сохранения нужен собственный протокол совместимости, описанный в разделе о сохранениях.

```lua
local function append_report(path, rows)
    if #rows == 0 then
        return true
    end

    local payload = table.concat(rows, "\n") .. "\n"
    local file, open_error = io.open(path, "ab")
    if not file then
        return nil, open_error
    end

    local written, write_error = file:write(payload)
    local closed, close_error = file:close()
    if not written then
        return nil, write_error
    end
    if not closed then
        return nil, close_error
    end
    return true
end
```

См. [библиотеки VM](../src/xrScripts/script_storage.cpp#L63) и [файловые утилиты Framework](../gamedata/scripts/ixr_framework/utils/ffx_io_utils.script).

У панели появились границы жизни данных. Если даже после этого один update успевает взять слишком много работы, ограничим размер порции и проверим выбор структур данных.

## 5. Рост нагрузки

В версии выше один поиск отдаёт одно расстояние. Допустим, следующая версия панели готовит список точек снабжения для КПК, сверяет правила доступа и обновляет много строк. Один общий интервал теперь может лишь перенести длинную паузу на другой кадр. Здесь важно ограничить объём работы за вызов и выбрать структуру данных под задачу.

### 15. Ограничьте число задач за одно обновление

Допустим, КПК нужно подготовить 200 подписей. Если их можно добавлять постепенно, очередь распределит обработку, а лимит защитит от роста работы быстрее, чем она завершается.

Для фоновой работы задайте максимум элементов за вызов и ограничьте очередь. Держите один обработчик на подсистему и сохраняйте прогресс между вызовами. Если очередная порция всё ещё даёт пик, уменьшите размер порции или ограничьте время и стоимость отдельной задачи. Для необязательного периодического опроса группы NPC можно задать разные фазы. Разнос фаз снижает вероятность пика, а жёсткий предел обеспечивает очередь. Время реакции растёт с накоплением работы, поэтому срочные события оставляйте на их штатном пути.

`npc_on_update` и `monster_on_update` вызываются из биндеров отдельных объектов, `physic_object_on_update` дополнительно передаёт биндер и `delta`. Сигналы приходят с разной частотой. Ставьте дешёвый фильтр раньше получения позиции, запросов ALife и формирования текста. Проверяйте, нужна ли операция для каждого NPC, и учитывайте, что биндер может вызвать событие для мёртвого объекта.

**Пример A.** Очередь для предварительной загрузки имён секций из LTX. Внешний код передаёт имя из конечного списка секций подсистемы через `enqueue_section`. При заполнении очередь возвращает `false`, чтобы вызывающий код мог повторить попытку позже. Кэш рассчитан на неизменную конфигурацию. Если аддон перезагружает LTX, сбрасывайте его в этой точке.

```lua
local capacity, batch = 64, 4
local queue, pending, names = {}, {}, {}
local head, tail, count = 1, 1, 0

function enqueue_section(section)
    if names[section] ~= nil or pending[section] then return true end
    if count == capacity then return false end

    queue[tail], pending[section] = section, true
    tail = tail % capacity + 1
    count = count + 1
    return true
end

local function drain()
    if count == 0 then return end
    local ini = system_ini()
    for _ = 1, math.min(count, batch) do
        local section = queue[head]
        queue[head], pending[section] = nil, nil
        head = head % capacity + 1
        count = count - 1
        names[section] = ini:section_exist(section)
            and ini:line_exist(section, "inv_name")
            and ini:r_string(section, "inv_name")
            or false
    end
end

function get_preloaded_name(section)
    return names[section]
end

function on_game_start()
    RegisterScriptCallback("actor_on_update", drain)
end
```

**Пример B.** Периодический снимок здоровья живых NPC с разными начальными фазами. Поле живёт в online-хранилище биндера. Для дорогой операции такой опрос нужен только отобранным NPC.

```lua
local period_ms = 1000
local clock_modulus = 4294967296

local function sample_npc(npc, st)
    if not st then return end

    local now = time_global()
    local last = st.my_mod_last_sample
    if last == nil then
        local phase = (npc:id() * 97) % period_ms
        last = (now - period_ms + phase) % clock_modulus
        st.my_mod_last_sample = last
    end

    if (now - last) % clock_modulus < period_ms then return end
    st.my_mod_last_sample = now
    if npc:alive() then
        st.my_mod_sampled_health = npc.health
    else
        st.my_mod_sampled_health = nil
    end
end

function on_game_start()
    RegisterScriptCallback("npc_on_update", sample_npc)
end
```

См. [NPC update](../gamedata/scripts/xr_motivator.script#L523), [Update мутанта](../gamedata/scripts/bind_monster.script#L59), [Update физического объекта](../gamedata/scripts/bind_physic_object.script#L48), [Экспорт system_ini](../src/xrScripts/exports/script_ini_file_script.cpp#L166).

### 16. Продолжайте длинную работу на следующих обновлениях

Очередь продолжается на следующем callback. У уровня также есть отдельный путь для кооперативных задач через `main()`. Выбор зависит от того, где задача стартует и кто отменяет её при выгрузке.

Уровневый `CScriptProcess` создаёт корутину для `main()` из `[level_scripts] script` и на каждом своём обновлении по очереди возобновляет одну активную корутину. Каждый отрезок до `coroutine.yield()` блокирует выполнение этого участка GameThread. Движок ждёт окончания задач кадра перед `EndRender`. Делайте `yield` без аргументов и допускайте, что следующее возобновление наступит через несколько обновлений. Для обычного callback используйте состояние задачи и возвращайтесь из обработчика, чтобы продолжить работу при следующем вызове. Движок сам распределяет фазы между потоками, а Lua-корутины выполняются кооперативно.

**Пример A.** Для уровневого `main` берите стабильный плотный массив Lua-данных. `get_prepared_rows`, `index_row` и `publish_index` обозначают операции аддона. Снимок к началу уже готов, а опубликованный результат содержит завершённый индекс.

```lua
function main()
    local rows = get_prepared_rows() -- адаптер аддона
    local index = {}
    for i = 1, #rows do
        index_row(index, rows[i]) -- адаптер аддона
        if i % 32 == 0 and i < #rows then
            coroutine.yield()
        end
    end
    publish_index(index) -- адаптер аддона
end
```

**Пример B.** Из существующего callback обновления вызывайте `step`. Передайте стабильный массив и синхронную функцию обработки через `start`. Этот пример обрабатывает до 16 элементов за вызов, а при смене владельца или выгрузке `cancel` освобождает входные данные. Вторая задача принимается после завершения или явной отмены первой.

```lua
local job

function start(rows, apply)
    if job then
        return false
    end
    job = { rows = rows, apply = apply, cursor = 1, count = #rows }
    return true
end

function step()
    local current = job
    if not current then
        return
    end
    local last = math.min(current.cursor + 15, current.count)
    while job == current and current.cursor <= last do
        current.apply(current.rows[current.cursor])
        current.cursor = current.cursor + 1
    end
    if job == current and current.cursor > current.count then
        job = nil
    end
end

function cancel()
    job = nil
end
```

См. [Создание процесса уровня](../src/xrGame/Level_load.cpp#L178-L184), [round-robin](../src/xrScripts/script_process.cpp#L70-L90), [`main` и `yield`](../src/xrScripts/script_thread.cpp#L55-L68), [ограничение на значения `yield`](../src/xrScripts/script_thread.cpp#L94-L125), [фаза GameThread](../src/xrEngine/EngineThreading.cpp#L37-L65), [ожидание задач кадра](../src/xrEngine/device.cpp#L252-L254).

### 17. Подбирайте сервис Framework по его поведению

Когда задача уже привязана к игровым обновлениям, для простой периодичности или редкой реакции на близость можно взять готовый сервис Framework. Здесь важно заранее проверить его задержку, область действия и способ отмены.

`IsActionThrottled` возвращает `true`, когда вызов следует пропустить, включая отсутствие модуля. `IsNotActionThrottled` возвращает `true`, когда действие разрешено. Проверка потребляет разрешение и запоминает первый интервал для имени. Время обновляется отдельным подписчиком `actor_on_update`, поэтому внутри других подписчиков того же события оно может соответствовать предыдущему update. Берите несколько постоянных имён для необязательных подсистем. Модуль не очищает динамические ключи по уничтожению объекта.

`ixr_triggers` проверяет триггеры текущего уровня при выборочных обновлениях. У NPC и мутантов по одному общему таймеру на категорию, поэтому пропуск возможен даже у живых объектов, которые не попали в очередную выборку. Это годится для редких, терпимых к задержке действий. Для точной реакции на вход каждого объекта нужна механика с подходящим контрактом. `ixr_timers` перебирает активный список, хранит Lua-функции с аргументами и запускается после `actor_on_spawn` и сигнала готовности ввода на экране загрузки. В этой ревизии `timer_delete` обращается к `to_remove` без локального объявления, а массовое удаление истёкших одноразовых таймеров идёт по возрастающим индексам и может сдвинуть оставшиеся записи. Для новых массовых и отменяемых задач используйте ограниченный диспетчер со сроком и явной отменой. Простую периодичность можно задать проверкой времени из рецепта 3.

**Пример A.** Один необязательный снимок для HUD. Выполняйте ровно одну проверку для одного имени за попытку.

```lua
local cached_health

local function update_hud_sample(binder)
    if IsActionThrottled("my_mod.hud_health", 250) then return end
    cached_health = binder.object.health
end

local function reset_hud_sample()
    cached_health = nil
end

function on_game_start()
    RegisterScriptCallback("actor_on_update", update_hud_sample)
    RegisterScriptCallback("actor_on_net_destroy", reset_hud_sample)
end
```

**Пример B.** Одноразовый триггер для актёра. Вызов `arm_checkpoint` привяжите к активации задания и передайте `vector` центра из данных квеста. Возврат `true` из обработчика запрашивает снятие триггера после обхода.

```lua
local trigger_name = "my_mod.checkpoint"
local reached = false

local function on_contact(_, _, _, _, _, _, entered, _, _, source)
    if source == "actor" and entered then
        reached = true
        return true
    end
    return false
end

function arm_checkpoint(center)
    if not db.actor then return false end
    return TriggerCreate(trigger_name, level.name(), center, 3, on_contact)
end

local function reset()
    TriggerRemove(trigger_name)
    reached = false
end

function on_game_start()
    RegisterScriptCallback("actor_on_net_destroy", reset)
end
```

См. [Глобальные обёртки throttler](../gamedata/scripts/ixray_system/global.lua#L743), [Часы и ключи throttler](../gamedata/scripts/ixr_framework/modules/ixr_module_throttlers.script#L102), [Проверка триггеров](../gamedata/scripts/ixr_framework/modules/ixr_module_triggers.script#L121), [Такт таймеров](../gamedata/scripts/ixr_framework/modules/ixr_module_timers.script#L194), [Реализация timer_delete](../gamedata/scripts/ixr_framework/modules/ixr_module_timers.script#L401).

### 18. Выберите контейнер под операцию

Разбили обработку на порции, теперь проверим, что каждая порция делает минимум работы. Для списка точек снабжения поиск по секции и выбор ближайшей точки требуют разных структур.

Плотный массив удобен для последовательного обхода. Таблица по ключу удобна для проверки принадлежности, поиска и накопления. Стоимость построения индекса окупается при повторных запросах или когда один проход заменяет вложенный. Обновляйте индекс в точках добавления и удаления. Удаление с перестановкой последнего элемента подходит только там, где порядок свободный.

**Пример A · Lua. Индекс по секциям создаётся при загрузке правил.** Совпадение трактуется как принадлежность множеству. Массив исходных правил можно отдельно сохранить для отображения в исходном порядке.

```lua
local function make_section_set(sections)
    local allowed = {}
    for i = 1, #sections do
        allowed[sections[i]] = true
    end
    return allowed
end

local allowed = make_section_set({ "bandage", "medkit" })
assert(allowed.medkit and not allowed.ammo_9x18_fmj)
```

**Пример B · Lua. Один проход вместо сортировки для выбора лучшего.** При равном счёте остаётся первый элемент, что сохраняет приоритет входного массива. Для полного рейтинга используйте сортировку и задайте явное правило для равных ключей.

```lua
local function pick_best(candidates)
    local best, best_score
    for i = 1, #candidates do
        local item = candidates[i]
        local score = item.score
        if best_score == nil or score > best_score then
            best, best_score = item, score
        end
    end
    return best
end

local first = { id = 6, score = 10 }
assert(pick_best({ first, { id = 3, score = 10 } }) == first)
assert(pick_best({}) == nil)
```

### 19. Сократите временные таблицы и замыкания

Когда алгоритм уже проходит по данным один раз, смотрим, сколько таблиц и замыканий он создаёт на этот проход. Буфер результата полезен, если понятно, кто и сколько времени его читает.

Для горячей функции сначала попробуйте возвращать несколько скаляров. Если буфер всё же нужен, выделяйте его на владельца и очищайте использованный хвост. Повторное использование требует, чтобы потребитель закончил чтение до следующего заполнения. Для рекурсии, вложенных callback и coroutine нужны независимые буферы или результаты с собственным временем жизни. Оценивайте и объём удерживаемой памяти после пика. Для большого буфера известной формы проверьте `require("table.new")(array_capacity, hash_capacity)` на целевой сборке и сравните с обычной таблицей.

**Пример A · Lua. Пара чисел без таблицы результата.** На границе, где результат нужен дольше вызова, потребитель явно решает, сохранять ли пару.

```lua
local function min_max(values, count)
    if count == 0 then
        return nil, nil
    end
    local lo, hi = values[1], values[1]
    for i = 2, count do
        local value = values[i]
        if value < lo then lo = value end
        if value > hi then hi = value end
    end
    return lo, hi
end

local lo, hi = min_max({ 5, 3, 8 }, 3)
assert(lo == 3 and hi == 8)
```

**Пример B · Lua. Буфер выбора принадлежит одному экземпляру системы.** Храните одну функцию фильтра и её входные данные отдельно. Этот буфер содержит только ID.

```lua
local function collect_ready(records, scratch, old_count)
    local count = 0
    for i = 1, #records do
        local record = records[i]
        if record.ready then
            count = count + 1
            scratch[count] = record.id
        end
    end
    for i = count + 1, old_count do
        scratch[i] = nil
    end
    return count
end

local scratch = {}
local count = collect_ready({ { id = 9, ready = true } }, scratch, 0)
assert(count == 1 and scratch[1] == 9)
count = collect_ready({}, scratch, count)
assert(count == 0 and scratch[1] == nil)
```

### 20. Стройте строки к моменту использования

Следующая граница проходит между числами и строками. Изменение статуса панели, отчёт и диагностический лог могут использовать одни счётчики, но текст для них нужен в разные моменты.

Наращивание длинной строки через `text = text .. part` в цикле повторно копирует префикс. Для серии фрагментов используйте `table.concat` по известной длине. Короткое выражение из нескольких частей обычно читается лучше через `..`. Для диагностики ставьте условие **до** вычисления аргументов и пишите итог за окно. В базовом `printf` есть `debug.getinfo`, обработка имени файла и форматирование.

**Пример A · Lua. Построение строки отчёта за один проход.** Функцию вызывают при открытии страницы диагностики или экспорте. Элементы `labels` — готовые строки.

```lua
local function build_report(labels)
    local parts = {}
    for i = 1, #labels do
        parts[i] = string.format("%d. %s", i, labels[i])
    end
    return table.concat(parts, "\n")
end

assert(build_report({ "medkit", "bandage" }) == "1. medkit\n2. bandage")
```

**Пример B · Движок. Одна запись на окно диагностики.** Счётчик обновляется только в режиме диагностики. `report_window` вызывают с уже выбранной частотой, например раз в секунду или по завершении прогона.

```lua
local diagnostics = false
local misses = 0

local function record_miss()
    if diagnostics then
        misses = misses + 1
    end
end

local function report_window()
    if not diagnostics or misses == 0 then
        return
    end
    log1(string.format("my_mod missed lookups: %d", misses))
    misses = 0
end
```

См. [`printf`](../gamedata/scripts/_g.script#L81) и [экспорт `log1`](../src/xrScripts/script_engine_script.cpp#L341).

### 21. Разберите условия один раз, проверяйте их вовремя

У точки снабжения могут появиться условия доступности. Разобранные правила удобно хранить. Результат проверки зависит от того, чистая ли она, меняет ли состояние и делает ли случайный выбор.

`xr_logic.parse_condlist` разбирает строку в структуру. `pick_section_from_condlist` читает текущее состояние, может бросать случайный выбор и выполнять эффекты или менять инфопоршни. Кэшируйте разобранную конфигурацию на срок жизни схемы. Сохраняйте частоту, порядок и число проверок там, где они влияют на вероятность, переходы и побочные эффекты. Для собственных чистых предикатов возможна дополнительная фильтрация по изменению зависимостей.

**Пример A · Движок. Разбор condlist при конфигурации схемы.** `section` и `field` — реальные имена вашей секции и параметра, `source` — прочитанная строка. `state` принадлежит активной схеме. Вызывайте `choose_target` в прежней точке перехода и обрабатывайте её результат там же.

```lua
local function configure(state, npc, section, field, source)
    state.condlist = xr_logic.parse_condlist(npc, section, field, source)
end

local function choose_target(state, actor, npc)
    local conditions = state.condlist
    if actor and conditions then
        return xr_logic.pick_section_from_condlist(actor, npc, conditions)
    end
end
```

**Пример B · Lua. Пересчёт собственного чистого условия по версии.** `revision` увеличивается при изменении любого входа условия. При замене владельца создаётся новый `state`. Значение `false` сохраняется так же, как `true`.

```lua
local function can_use(state, revision, has_item, cooldown_ready)
    if state.checked_revision ~= revision then
        state.allowed = has_item and cooldown_ready
        state.checked_revision = revision
    end
    return state.allowed
end

local state = {}
assert(can_use(state, 0, true, false) == false)
assert(can_use(state, 1, true, true) == true)
```

См. [разбор](../gamedata/scripts/xr_logic.script#L1011), [оценку и эффекты](../gamedata/scripts/xr_logic.script#L591).

Повторите замер из первого рецепта. Если редкие тяжёлые кадры остались, следующая часть помогает установить, какой именно участок занимает кадр.

## 6. Разбор оставшихся пиков

Когда число операций уже ограничено, оставшийся пик требует другого объяснения. В этой ветке `CLevel::OnFrame` ставит `script_gc` в `seqParallel`. Там обновляется скриптовый процесс уровня и выполняется шаг сборщика мусора. Кадр ждёт GameThread перед завершением отрисовки, поэтому длинная Lua-работа в этой очереди отражается на времени кадра. Биндер и интерфейс имеют собственные точки входа. В профиле ищите дорожку, где отработал именно ваш код.

См. [обновление уровня](../src/xrGame/Level.cpp#L732), [фазу GameThread](../src/xrEngine/EngineThreading.cpp#L37), [ожидание задач кадра](../src/xrEngine/device.cpp#L252).

### 22. Найдите на трассе Tracy работу, совпавшую с пиком

Локальный замер показал, что отдельный вызов короткий, а в игре всё ещё бывают пики. В Tracy можно увидеть, совпал ли такой кадр с нашим поиском, обновлением других NPC или GC.

Lua-зоны доступны при сборке с `IXRAY_PROFILER_TRACY=ON`. Смотрите дорожки GameThread, основного потока и синхронизацию перед завершением кадра. Ставьте зоны вокруг логических фаз, например обновления схемы и запроса мира. Метка постоянна, время жизни зоны ограничено текущим вызовом, начало и конец сбалансированы. Сравнивайте замеры при одинаковой детализации инструментирования.

В этой ревизии `PROF_EVENT_CLOSURE` Framework при подключённом Tracy возвращает только первый результат, а ошибка внутри функции пропускает закрытие зоны. Для диагностики многозначного API используйте собственную обёртку с известным контрактом.

**Пример A · Движок. Зона для синхронной функции с двумя результатами.** Защищённая ветка предназначена для записи профиля. Она закрывает зону перед повторным выбросом Lua-ошибки.

```lua
local function sample_query(work, state)
    local profiler = rawget(_G, "tracy")
    if not profiler or not IsTracyConnected() then
        return work(state)
    end

    profiler.ZoneBeginN("my_mod.query")
    local ok, found, count = pcall(work, state)
    profiler.ZoneEnd()
    if not ok then
        error(found, 0)
    end
    return found, count
end
```

**Пример B · Движок. Зона вокруг чистой обработки данных.** Проверка соединения и вызовы профилировщика стоят за пределами цикла. `scores` содержит плотный массив чисел, `count` задаёт его длину.

```lua
local function sum_scores(scores, count)
    local profiler = rawget(_G, "tracy")
    local recording = profiler and IsTracyConnected()
    if recording then
        profiler.ZoneBeginN("my_mod.score_batch")
    end

    local sum = 0
    for i = 1, count do
        sum = sum + scores[i]
    end

    if recording then
        profiler.ZoneEnd()
    end
    return sum
end

assert(sum_scores({ 2, 3, 4 }, 3) == 9)
```

См. [опция CMake](../CMakeLists.txt#L20), [регистрация Tracy](../src/xrScripts/script_engine.cpp#L210), [экспорт `IsTracyConnected`](../src/xrScripts/script_engine_script.cpp#L318), [обёртки Framework](../gamedata/scripts/ixr_framework/public_components/ixr_tracy_profiler.script#L57).

### 23. Разделите время Lua и вызовов движка

Если трасса ведёт в Lua-обработчик, измерим его части отдельно. Тогда будет видно, даст ли ускорение чистого цикла эффект на фоне запроса уровня и обновления интерфейса.

JIT полезен для достаточно горячих участков с повторяемой формой данных. Короткий callback с вызовами luabind может проводить основное время на стороне C++. Измеряйте отдельно подготовку данных, вычислительное ядро и применение результата. Отключение JIT и перестройка трасс нужны для диагностического эксперимента, после которого исходную настройку восстанавливают. Отладочный хук `debug.sethook`, LuaPanda и пошаговый отладчик меняют условия замера.

**Пример A · Движок. Фазы считывания, вычисления и применения.** `read_state`, `calculate` и `apply` принадлежат вашей механике. Передавайте долгоживущие таймеры и логируйте сводку после прогона.

```lua
local function update_measured(actor, timers, read_state, calculate, apply)
    timers.read:start()
    local input = read_state(actor)
    timers.read:stop()

    timers.compute:start()
    local output = calculate(input)
    timers.compute:stop()

    timers.apply:start()
    apply(actor, output)
    timers.apply:stop()
end

local timers = {
    read = profile_timer(),
    compute = profile_timer(),
    apply = profile_timer(),
}
```

**Пример B · LuaJIT. Изолированный прогон двух режимов одной функции.** Запускайте по одному режиму в отдельных процессах LuaJIT, с одинаковым объёмом входа. В отдельном CLI `os.clock()` подходит для CPU-времени серии. В игре используйте рецепт 1 и кадры Tracy.

```lua
local function sum_squares(values, repetitions)
    local sum = 0
    for _ = 1, repetitions do
        for i = 1, #values do
            local value = values[i]
            sum = sum + value * value
        end
    end
    return sum
end

local function run(mode)
    jit.opt.start(2)
    if mode == "interpreter" then
        jit.off(sum_squares)
    end
    local values = { 1, 2, 3, 4 }
    assert(sum_squares(values, 5000) == 150000)
    local started = os.clock()
    assert(sum_squares(values, 500000) == 15000000)
    return os.clock() - started
end

print(run(arg and arg[1] or "jit"))
```

Для автономного CLI доступны диагностика трасс `-jv` и сэмплер `-jp` при наличии соответствующих модулей дистрибутива. В игровом процессе проверяйте наличие нужного модуля прежде, чем полагаться на него. [Параметры CLI LuaJIT](https://luajit.org/running.html#opt_j), [профилировщик LuaJIT](https://luajit.org/ext_profiler.html), [загрузка библиотек движком](../src/xrScripts/script_storage.cpp#L63).

### 24. Проверьте сборку мусора и удерживаемые ссылки

Если нагрузка растёт после нескольких открытий панели или смен уровня, посмотрим на Lua-память и подписки. Здесь интересуют повторяемые замеры до и после цикла и то, кто продолжает удерживать данные.

Уровень сам запускает инкрементальный шаг GC, в исходнике `psLUA_GCSTEP` по умолчанию равен 10. Отдельные сборки выполняют дополнительный шаг внутри скриптового процесса. При остановке уровня движок вызывает полный сбор. Ручной `collectgarbage("collect")` посреди обновления может создать длинный кадр, а `stop` и настройки GC меняют общую VM. Уменьшайте число временных объектов и очищайте удерживаемые ссылки в момент, когда они больше не нужны. Проверяйте `collectgarbage("count")` в сопоставимых точках цикла. Это снимок Lua-памяти в КиБ, который меняется и от фаз работы сборщика. Нативные ресурсы C++ нужно измерять отдельно.

**Пример A.** Снимайте показатели в отладочном проходе после прогрева. Передайте в `measure_heap_kib` повторяемый сценарий, например десять открытий и закрытий панели, и сравните следующие десять. Сопоставляйте серию результатов, поскольку одна дельта может включать сбор ранее созданных объектов.

```lua
local function measure_heap_kib(run_scenario)
    local before = collectgarbage("count")
    run_scenario()
    local after = collectgarbage("count")
    return before, after, after - before
end
```

**Пример B.** Снимайте подписку той же функцией, которую передали при регистрации, и отпускайте захваченные данные. Вызов `attach` выполняйте после инициализации сигналов IXR Framework. Передайте данные Lua и функцию вашего аддона. Вызов `detach` делает владелец при остановке функции вне рассылки этого события. [Регистрация и снятие подписки](../gamedata/scripts/ixray_system/dynamic_callbacks.lua#L41-L92), [источник события](../gamedata/scripts/bind_stalker.script#L556-L560).

```lua
local listener

function detach()
    if listener then
        UnregisterScriptCallback("actor_on_update", listener)
        listener = nil
    end
end

function attach(state, update_state)
    detach()
    listener = function()
        update_state(state)
    end
    RegisterScriptCallback("actor_on_update", listener)
end
```

См. [GC на обновлении](../src/xrGame/Level.cpp#L732-L766), [дополнительный шаг в `DEBUG`](../src/xrScripts/script_process.cpp#L98-L104), [сбор при остановке](../src/xrGame/Level_network.cpp#L162-L176), [полный сбор](../src/xrScripts/script_engine.cpp#L455-L458).

### 25. Подготовьте данные для чистого числового цикла

До чистых числовых циклов доходим, если замер выделил именно их. Например, КПК может пересчитывать сотни оценок расстояния после одного общего снимка координат.

Нормализуйте типы на входе, а затем обрабатывайте плотные массивы с единым контрактом. Выносите выбор режима, разбор строк и работу с userdata за пределы числового цикла, когда это сохраняет результат. Длину массива с дырками передавайте явно и определяйте смысл отсутствующего элемента. Применяйте локальный алиас к часто используемой стабильной функции, если он упрощает код или помогает по результатам замера. Для функции, которую Framework или аддон переопределяет, резолвинг должен сохранять эту возможность.

**Пример A · Lua. Преобразование и валидация чисел при установке настроек.** Плотный массив проходит валидацию один раз. `scale` — выбранное заранее число.

```lua
local function normalize(raw_values)
    local values = {}
    for i = 1, #raw_values do
        local value = tonumber(raw_values[i])
        assert(value and value == value and math.abs(value) < math.huge)
        values[i] = value
    end
    return values
end

local function weighted_total(values, scale)
    local total = 0
    for i = 1, #values do
        total = total + values[i] * scale
    end
    return total
end

assert(weighted_total(normalize({ "1.5", "2" }), 2) == 7)
```

**Пример B · Lua. Выбор вычисления на партию.** Если набор однороден, расчёт выполняет числовой цикл одного типа. В примере единицы входа определяются режимом всей партии.

```lua
local floor = math.floor

local function quantize(values, use_percent, output)
    local multiplier = use_percent and 100 or 1
    for i = 1, #values do
        output[i] = floor(values[i] * multiplier)
    end
    return #values
end

local output = {}
local count = quantize({ 0.25, 0.5 }, true, output)
assert(count == 2 and output[1] == 25 and output[2] == 50)
```

### 26. Проверьте возможности установленного LuaJIT

Последний шаг нужен для переносимости и редких узких мест. Перед использованием особенностей LuaJIT проверяем версию VM и доступные библиотеки целевой сборки.

Windows-сборка подключает пакет `lua51.dll`, в проверенном комплекте это LuaJIT 2.1.0-beta3 с API Lua 5.1. На Linux сборка получает форк `luajit-ixray`. Движок открывает `jit`, `bit` и `ffi` и в стандартном игровом ядре задаёт `jit.opt.start(2)`. Записывайте `_VERSION`, `jit.version` и `jit.status()` при профилировании. Используйте совместимый синтаксис Lua 5.1, а расширения проверяйте на фактическом рантайме. Например, в проверенном пакете есть глобальный `unpack` и модули `require("table.new")` и `require("table.clear")`. Поля `table.unpack`, `table.new` и `table.clear` в этом пакете отсутствуют. Имена библиотек, флаги JIT и профили трасс могут отличаться в другой сборке или при подключённом отладчике.

**Пример A.** Передавайте сохранённые аргументы диапазоном с явной длиной, чтобы сохранить `nil` в середине и в конце. Создавайте такую упаковку только для отложенного вызова. При немедленной передаче используйте `fn(...)`.

```lua
local unpack_range = unpack or table.unpack

local function pack_arguments(...)
    return { n = select("#", ...), ... }
end

local function call_later(fn, args)
    return fn(unpack_range(args, 1, args.n))
end
```

**Пример B.** Если измерения показывают выигрыш от плотного числового буфера, создайте тип FFI и сам буфер один раз для известного размера. Оба пути используют индексы с нуля и записывают все элементы до чтения. Пример рассчитан на один синхронный, невложенный вызов для этого буфера. Параметр `read_sample` обозначает вашу функцию. С FFI запись за границы массива обращается к чужой памяти. Объекты LuaBind оставляйте за их штатными API и учитывайте время жизни, ABI и переносимость при использовании FFI.

```lua
local ok, ffi = pcall(require, "ffi")
local double_array = ok and ffi.typeof("double[?]")

local size = 256
local samples = double_array and double_array(size) or {}

local function fill_and_sum(read_sample)
    local sum = 0
    for i = 0, size - 1 do
        samples[i] = read_sample(i)
    end
    for i = 0, size - 1 do
        sum = sum + samples[i]
    end
    return sum
end
```

См. [Пакет Windows и источник Linux](../cmake/modules/LuaJIT.cmake#L3-L35), [инициализация библиотек](../src/xrScripts/script_storage.cpp#L62-L78), [настройки JIT и отладчика](../gamedata/scripts/ixray_system/global.lua#L1-L33).

## Проверка результата

1. Сравните одну и ту же сцену до и после правки. Запишите число update, обходов и поисков, время кадра и редкие пики. Холодный старт и прогретый проход оценивайте отдельно.
2. Проверьте поведение по требованиям панели. Состав меняется к ближайшему акторскому update. Расстояние обновляется по своему периоду. Закрытая панель пропускает оба запроса.
3. Повторите открытие, закрытие, загрузку и переход уровня. Устаревший снимок должен исчезнуть, подписки должны срабатывать по одному разу. Сохранённая подсказка восстанавливается, рабочие кэши строятся заново.
4. Если остаётся задержка, найдите её на трассе и меняйте одну причину за проход. Так можно связать выигрыш с конкретной правкой.
