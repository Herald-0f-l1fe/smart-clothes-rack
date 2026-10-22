# Прошивка AutoRack

Код для микроконтроллеров. Проект переведён **только на Arduino Uno** (ESP32
больше не используется). Все новые скетчи — PlatformIO-проекты на Uno.

## Структура

```
firmware/
├── README.md
└── arduino/
    ├── PINOUT.md                  общая распиновка (держать одинаковой!)
    ├── .gitignore                 .pio и артефакты сборки
    │
    ├── tests/                     проверочные программы
    │   ├── uno_all_components/    всё кроме NEMA 17: зуммер, микрики, PIR, 28BYJ-48
    │   ├── nema_test/             NEMA 17 + DRV8825 (команды по Serial)
    │   └── legacy_nema_test/      старый тест NEMA (история)
    │
    └── working/                   рабочие программы
        ├── nema_spin/             вращение NEMA интервалами (проверка механики)
        ├── mvp_rack/              MVP: автомат с HOME + HOLD 30 с
        ├── v1/                    ручной стенд: крутить мотор и жать микрики руками
        └── v2/                    автомат по PIR (движение->TIGHT, тишина->SLACK)
```

Каждая программа — отдельный PlatformIO-проект: папка содержит
`platformio.ini` и `src/main.cpp` с `setup()`/`loop()`.

## Как собрать и залить (PlatformIO)

Зайти в папку нужной программы и выполнить:

```bash
pio run            # собрать
pio run -t upload  # залить в Arduino Uno
pio device monitor # лог, 9600 (для nema_test — см. его шапку)
```

Например:

```bash
cd firmware/arduino/working/mvp_rack
pio run -t upload
```

> `pio` лежит в `~/.platformio/penv/Scripts/pio.exe`, если не в PATH.

## MVP (`working/mvp_rack`) — что уже работает

Минимальная демонстрируемая версия: **один NEMA 17 растягивает пружину,
когда человек подходит, и ослабляет, когда уходит.** Крючки разъезжаются /
съезжаются.

Конечный автомат (неблокирующий, без `delay()` в работе):

```
BOOT ──HOME(к SLACK)──► SLEEP
SLEEP ──PIR=HIGH─────► DISTRIBUTE (к TIGHT)
DISTRIBUTE ──TIGHT───► HOLD (бип)
HOLD ──тишина 30 с───► COOLDOWN (к SLACK)
COOLDOWN ──SLACK─────► SLEEP (мотор обесточен)
```

- Остановка **только по концевикам**, шаги для остановки не считаются —
  перезагрузка Uno или проскальзывание не «сдвигают» систему.
- Ручные команды по Serial: `h`=HOME, `s`=STOP→SLEEP, `d`=DISTRIBUTE, `r`=RESET.
- Защита: если концевик не сработал за 20 с — состояние `ERROR`, мотор выключен.

Основные параметры — вверху файла `working/mvp_rack/src/main.cpp`
(`STEP_DELAY_US`, `HOLD_MS`).

## Распиновка

Полная таблица — в [`arduino/PINOUT.md`](arduino/PINOUT.md). Кратко для MVP:

| A1 | A2 | A3 | D2 | D4 | D8 | D13 | A0 |
|---|---|---|---|---|---|---|---|
| STEP | DIR | EN | TIGHT | SLACK | зуммер | LED | PIR |

## Дальнейшие шаги

1. Отладить MVP на столе с двумя микриками.
2. Добавить LED-ленту (WS2812B) — осторожно с RAM на Uno (2 КБ).
3. Добавить FSR через CD74HC4067 (занятость крючков) — после MVP.
4. Вернуть в целевую архитектуру состояние `HINT` (локальный раздвиг
   верхней кареткой 28BYJ-48).