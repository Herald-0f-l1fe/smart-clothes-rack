/*
 * AutoRack — v2: автомат по PIR (Arduino Uno + NEMA 17 / DRV8825)
 * ==================================================================
 * Поведение простое, без таймеров ожидания:
 *
 *   PIR чувствует движение  -> крутим к TIGHT, пока не нажмётся D2 -> стоп
 *   PIR = тишина            -> крутим к SLACK, пока не нажмётся D4 -> стоп
 *
 * То есть: человек подошёл — пружина натянулась (крючки разъехались),
 * человек ушёл — пружина ослабла (крючки съехались).
 *
 * Отличие от mvp_rack: там был HOLD (30 с тишины) и HOME при старте.
 * Здесь этого нет — реагируем на PIR напрямую.
 *
 * Остановка — ТОЛЬКО по концевику. Дошли до нужного микрика: драйвер
 * обесточивается (мотор не греется, ничего не жжёт).
 *
 * Плюс ручные команды, чтобы вмешаться при отладке:
 *   t = крутить к TIGHT    k = крутить к SLACK
 *   s = СТОП               p = состояние микриков/PIR
 *   e = драйвер ВКЛ        f = драйвер ВЫКЛ
 *   a = авто-режим ВКЛ     m = авто-режим ВЫКЛ (только ручные команды)
 *
 * ------------------------------------------------------------------
 * РАСПИНОВКА (как в v1, не меняем):
 *   STEP        -> A1
 *   DIR         -> A2
 *   EN          -> A3      LOW = драйвер включён
 *   LIMIT_TIGHT -> D2      INPUT_PULLUP, микрик на GND, LOW = нажат
 *   LIMIT_SLACK -> D4      INPUT_PULLUP, микрик на GND, LOW = нажат
 *   BUZZER      -> D8
 *   LED         -> D13
 *   PIR OUT     -> A0
 *
 * Прошивка (из папки v2):
 *   pio run -t upload
 *   pio device monitor
 *
 * Серийный монитор: 9600, шли буквы + Enter.
 */

#include <Arduino.h>

// ================== КОНСТАНТЫ ==================
static const unsigned int  STEP_DELAY_US    = 2000;     // мкс/шаг
static const unsigned long PIR_WINDOW_MS    = 1000UL;  // окно голосования за движение
static const unsigned long PIR_HOLDOFF_MS   = 800UL;    // антидребезг одного импульса
static const unsigned long LIMIT_TIMEOUT_MS = 30000UL;  // авто-стоп, если микрик не нажался
static const unsigned long STATUS_PERIOD_MS = 1000UL;   // "живой" лог во время движения

// ================== ПИНЫ ==================
#define stepPin       A1
#define dirPin        A2
#define ena           A3
#define LIMIT_TIGHT   2
#define LIMIT_SLACK   4
#define BUZZER_PIN    8
#define LED_PIN       13
#define PIR_PIN       A0

// ================== СОСТОЯНИЕ ==================
enum Dir { DIR_IDLE, DIR_TO_TIGHT, DIR_TO_SLACK };

Dir            mode         = DIR_IDLE;   // что мотор делает сейчас
unsigned long  modeStartedMs= 0;
unsigned long  lastStepUs   = 0;
unsigned long  stepCount    = 0;
unsigned long  lastStatusMs = 0;
bool           modeDone     = false;      // true = последнее движение завершилось (стоп)

bool           autoMode     = true;       // PIR управляет мотором?
bool           motionNow    = false;      // итоговое решение: есть движение?

// --- Стартовый прогон: сначала к SLACK, потом к TIGHT, только затем авто ---
bool           startRun      = true;      // true = идёт стартовый прогон
int            startPhase    = 0;         // 0 = едем к SLACK, 1 = едем к TIGHT, 2 = готово

// PIR: голосование за окно PIR_WINDOW_MS.
// Интегратор: HIGH добавляет dt, LOW вычитает dt.
// motionNow = (integrator > 0) => за последние ~10 с движения БОЛЬШЕ, чем тишины.
int            pirRaw       = LOW;        // сырое состояние пина
unsigned long  pirRawSince  = 0;          // когда сырое состояние изменилось
long           pirIntegrator= 0;          // в мс, ограничен [-WINDOW ; +WINDOW]
unsigned long  pirLastSampleMs = 0;       // для расчёта dt

// ================== ХЕЛПЕРЫ ==================
void motorEnable(bool on) {
  digitalWrite(ena, on ? LOW : HIGH);
}

void stepOnce() {
  digitalWrite(stepPin, HIGH);
  delayMicroseconds(3);
  digitalWrite(stepPin, LOW);
  stepCount++;
}

void printState() {
  Serial.print(F("   TIGHT(D2)="));
  Serial.print(digitalRead(LIMIT_TIGHT) == LOW ? F("НАЖАТ") : F("отпущен"));
  Serial.print(F("   SLACK(D4)="));
  Serial.print(digitalRead(LIMIT_SLACK) == LOW ? F("НАЖАТ") : F("отпущен"));
  Serial.print(F("   PIR="));
  Serial.print(motionNow ? F("ДВИЖЕНИЕ") : F("тишина"));
  Serial.print(F("  (баланс "));
  Serial.print(pirIntegrator / 1000);
  Serial.print(F("/"));
  Serial.print((long)PIR_WINDOW_MS / 1000);
  Serial.print(F("с)"));
  Serial.print(F("   auto="));
  Serial.println(autoMode ? F("ВКЛ") : F("ВЫКЛ"));
}

// Запуск движения в заданную сторону (с обесточиванием при достижении лимита)
void startMove(Dir d, const __FlashStringHelper* reason) {
  if (mode == d) return;                  // уже едем туда — ничего не меняем
  mode = d;
  modeDone = false;
  modeStartedMs = millis();
  lastStepUs = micros();
  stepCount = 0;
  digitalWrite(dirPin, (d == DIR_TO_TIGHT) ? HIGH : LOW);
  motorEnable(true);
  Serial.print(F(" >> START "));
  Serial.print(d == DIR_TO_TIGHT ? F("->TIGHT") : F("->SLACK"));
  Serial.print(F("  ("));
  Serial.print(reason);
  Serial.println(F(")"));
}

void stopMotor(const __FlashStringHelper* why) {
  if (mode == DIR_IDLE) return;
  modeDone = true;
  mode = DIR_IDLE;
  motorEnable(false);
  Serial.print(F(" >> STOP ("));
  Serial.print(why);
  Serial.print(F("), шагов="));
  Serial.println(stepCount);
  printState();
}

// true = последнее движение завершилось остановкой (по концевику/таймауту)
bool modeFinished() {
  return modeDone;
}

// ================== PIR ==================
void pollPir(unsigned long now) {
  // dt с прошлого опроса (мс). Первый вызов — 0.
  unsigned long dt = now - pirLastSampleMs;
  pirLastSampleMs = now;

  // Антидребезг: меняем pirRaw, только если пин держится в новом состоянии > PIR_HOLDOFF_MS.
  int p = digitalRead(PIR_PIN);
  if (p != pirRaw && (now - pirRawSince) > PIR_HOLDOFF_MS) {
    pirRaw = p;
    pirRawSince = now;
  }

  // Интегратор: движение прибавляет, тишина вычитает.
  if (pirRaw == HIGH) pirIntegrator += (long)dt;
  else                pirIntegrator -= (long)dt;

  // Ограничитель окна: не выходим за [-PIR_WINDOW_MS ; +PIR_WINDOW_MS]
  if (pirIntegrator >  (long)PIR_WINDOW_MS) pirIntegrator =  (long)PIR_WINDOW_MS;
  if (pirIntegrator < -(long)PIR_WINDOW_MS) pirIntegrator = -(long)PIR_WINDOW_MS;

  // Решение: движение, если баланс за движение.
  motionNow = (pirIntegrator > 0);
}

// ================== КОМАНДЫ ==================
void handleSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r' || c == '\n' || c == ' ' || c == '\t') continue;

    switch (c) {
      case 't': case 'T':
        motorEnable(true);
        startMove(DIR_TO_TIGHT, F("вручную"));
        break;
      case 'k': case 'K':
        motorEnable(true);
        startMove(DIR_TO_SLACK, F("вручную"));
        break;
      case 's': case 'S':
        stopMotor(F("вручную"));
        break;
      case 'p': case 'P':
        printState();
        break;
      case 'e': case 'E':
        motorEnable(true);
        Serial.println(F(" >> драйвер ВКЛ (EN=LOW)"));
        break;
      case 'f': case 'F':
        motorEnable(false);
        Serial.println(F(" >> драйвер ВЫКЛ (EN=HIGH)"));
        break;
      case 'a': case 'A':
        autoMode = true;
        Serial.println(F(" >> авто-режим ВКЛ (реагирую на PIR)"));
        break;
      case 'm': case 'M':
        autoMode = false;
        Serial.println(F(" >> авто-режим ВЫКЛ (только ручные команды)"));
        break;
      default:
        Serial.print(F(" ?? неизвестная команда '"));
        Serial.print(c);
        Serial.println(F("'"));
        break;
    }
  }
}

// ================== ДВИЖЕНИЕ ==================
void updateMotion() {
  if (mode == DIR_IDLE) return;

  unsigned long now = millis();
  unsigned long nowUs = micros();

  // 1) Проверка нужного концевика
  int limitPin = (mode == DIR_TO_TIGHT) ? LIMIT_TIGHT : LIMIT_SLACK;
  if (digitalRead(limitPin) == LOW) {
    stopMotor(F("концевик нажат"));
    return;
  }

  // 2) Защита по таймауту
  if (now - modeStartedMs >= LIMIT_TIMEOUT_MS) {
    stopMotor(F("таймаут"));
    return;
  }

  // 3) Шаг
  if (nowUs - lastStepUs >= (unsigned long)STEP_DELAY_US) {
    lastStepUs = nowUs;
    stepOnce();
  }

  // 4) Лог
  if (now - lastStatusMs >= STATUS_PERIOD_MS) {
    lastStatusMs = now;
    Serial.print(F("   ...кручу, шагов="));
    Serial.println(stepCount);
  }
}

// ================== СТАРТОВЫЙ ПРОГОН ==================
// При включении: сначала крутим к SLACK, затем к TIGHT, затем передаём
// управление автомату (PIR). Остановка — по соответствующему концевику.
void updateStartRun() {
  if (!startRun) return;

  // Нужное направление текущей фазы и его концевик
  Dir want      = (startPhase == 0) ? DIR_TO_SLACK : DIR_TO_TIGHT;
  int wantLimit = (startPhase == 0) ? LIMIT_SLACK   : LIMIT_TIGHT;

  // Дошли до нужного концевика (нажат) — фаза завершена
  if (digitalRead(wantLimit) == LOW) {
    if (mode != DIR_IDLE) stopMotor(F("selftest: концевик"));  // на всякий случай
    if (startPhase == 0) {
      Serial.println(F(" >> старт: SLACK достигнут"));
      startPhase = 1;
      modeDone = false;               // начинаем новую фазу «чисто»
    } else {
      Serial.println(F(" >> старт: TIGHT достигнут"));
      startPhase = 2;
    }
    return;
  }

  // Концевик не нажат: если никуда не едем — начинаем двигаться к цели фазы
  if (mode == DIR_IDLE) {
    startMove(want, (startPhase == 0) ? F("старт: к SLACK") : F("старт: к TIGHT"));
  }
  // Если уже едем — updateMotion() доведёт; таймаут/ошибку он же обработает.
}

// ================== АВТО по PIR ==================
void updateAuto() {
  if (!autoMode) return;
  if (startRun) return;             // пока идёт стартовый прогон — авто молчит
  if (motionNow) {
    startMove(DIR_TO_TIGHT, F("PIR: движение"));
  } else {
    startMove(DIR_TO_SLACK, F("PIR: тишина"));
  }
}

// ================== LED ==================
void updateLed(unsigned long now) {
  static unsigned long lastToggle = 0;
  static bool state = false;
  unsigned long period = (mode == DIR_IDLE) ? 800 : 150;
  if (now - lastToggle >= period) {
    lastToggle = now;
    state = !state;
    digitalWrite(LED_PIN, state ? HIGH : LOW);
  }
}

// ================== SETUP ==================
void setup() {
  Serial.begin(9600);

  pinMode(stepPin, OUTPUT);
  pinMode(dirPin,  OUTPUT);
  pinMode(ena,     OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(LIMIT_TIGHT, INPUT_PULLUP);
  pinMode(LIMIT_SLACK, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(PIR_PIN, INPUT);

  digitalWrite(stepPin, LOW);
  digitalWrite(dirPin,  HIGH);
  motorEnable(false);

  delay(200);

  Serial.println();
  Serial.println(F("========================================="));
  Serial.println(F(" AutoRack — v2: автомат по PIR"));
  Serial.println(F(" движение->TIGHT, тишина->SLACK"));
  Serial.println(F("-----------------------------------------"));
  Serial.println(F(" STEP=A1 DIR=A2 EN=A3 TIGHT=D2 SLACK=D4 PIR=A0"));
  Serial.println(F(" t/k/s/p/e/f  a=авто ВКЛ  m=авто ВЫКЛ"));
  Serial.println(F("========================================="));
  printState();
}

// ================== LOOP ==================
void loop() {
  unsigned long now = millis();
  handleSerial();
  pollPir(now);
  updateStartRun(); // стартовый прогон SLACK->TIGHT (пока не завершён)
  updateAuto();     // авто-режим выбирает направление (молчит во время прогона)
  updateMotion();   // физическое движение и остановка по лимиту
  updateLed(now);
}