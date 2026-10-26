/*
 * AutoRack — v3: цикл по обнаружению движения (Arduino Uno + NEMA 17 / DRV8825)
 * ==================================================================
 * Логика (одно срабатывание = один полный цикл):
 *
 *   [ждём движения]
 *        │  PIR: появилось ДВИЖЕНИЕ (фронт LOW->HIGH)
 *        ▼
 *   вперёд 10000 шагов  (то же направление, что и при запуске: DIR=HIGH)
 *        │
 *        ▼
 *   "ничего" 10 секунд (мотор обесточен)
 *        │
 *        ▼
 *   назад 10000 шагов   (DIR=LOW)
 *        │
 *        ▼
 *   [снова ждём движения]  ... и так каждый раз
 *
 * ВАЖНО: во время цикла новые срабатывания PIR игнорируются.
 * Цикл запускается только по НОВОМУ движению (фронт), а не по уровню.
 *
 * Остановка по концевикам, таймаут и ручные команды НЕ используются —
 * движение всегда строго на 1000 шагов в каждую сторону.
 *
 * Ручные команды (для отладки, работают всегда):
 *   r  — запустить цикл вручную (вперёд 10000 -> пауза 10с -> назад 10000)
 *   s  — СТОП (прервать цикл сейчас)
 *   p  — напечатать состояние
 *   e  — драйвер ВКЛ      f — драйвер ВЫКЛ
 *   m  — авто по PIR ВЫКЛ (только ручной r)
 *   a  — авто по PIR ВКЛ
 *
 * ------------------------------------------------------------------
 * РАСПИНОВКА (как в v1/v2, не меняем):
 *   STEP     -> A1
 *   DIR      -> A2
 *   EN       -> A3      LOW = драйвер включён
 *   BUZZER   -> D8
 *   LED      -> D13
 *   PIR OUT  -> A0
 *
 * Прошивка (из папки v3):
 *   pio run -t upload
 *   pio device monitor
 *
 * Серийный монитор: 9600, шли буквы + Enter.
 */

#include <Arduino.h>

// ================== КОНСТАНТЫ ==================
static const unsigned int  STEP_DELAY_US   = 2000;      // мкс/шаг (~500 шагов/с)
static const unsigned long STEPS_PER_LEG   = 10000UL;   // шагов в каждую сторону
static const unsigned long PAUSE_MS        = 10000UL;   // "ничего" между движениями
static const unsigned long PIR_HOLDOFF_MS  = 500UL;     // антидребезг по PIR
static const unsigned long STATUS_PERIOD_MS= 1000UL;    // лог раз в секунду

// ================== ПИНЫ ==================
#define stepPin       A1
#define dirPin        A2
#define ena           A3
#define BUZZER_PIN    8
#define LED_PIN       13
#define PIR_PIN       A0

// ================== СОСТОЯНИЕ ==================
// Фазы одного цикла
enum Phase {
  PH_WAIT,        // ждём нового движения
  PH_FORWARD,     // крутим вперёд 10000 шагов
  PH_PAUSE,       // ничего 10 секунд
  PH_BACKWARD,    // крутим назад 10000 шагов
  PH_DONE         // цикл завершён (промежуточное, сразу -> PH_WAIT)
};

Phase          phase          = PH_WAIT;
unsigned long  phaseStartedMs = 0;
unsigned long  lastStepUs     = 0;
unsigned long  stepsThisLeg   = 0;     // сделано шагов в текущем движении
unsigned long  lastStatusMs   = 0;

bool           autoMode       = true;  // реагировать ли на PIR

// PIR: отслеживаем фронт LOW -> HIGH
int            pirRaw         = LOW;   // фильтрованное состояние
int            pirPinPrev     = LOW;   // для отслеживания изменения на пине
unsigned long  pirRawSince    = 0;

// ================== ХЕЛПЕРЫ ==================
void motorEnable(bool on) {
  digitalWrite(ena, on ? LOW : HIGH);
}

void stepOnce() {
  digitalWrite(stepPin, HIGH);
  delayMicroseconds(3);
  digitalWrite(stepPin, LOW);
}

void printState() {
  const char* ph;
  switch (phase) {
    case PH_WAIT:     ph = "WAIT";     break;
    case PH_FORWARD:  ph = "FORWARD";  break;
    case PH_PAUSE:    ph = "PAUSE";    break;
    case PH_BACKWARD: ph = "BACKWARD"; break;
    default:          ph = "DONE";     break;
  }
  Serial.print(F("   phase="));
  Serial.print(ph);
  Serial.print(F("  steps="));
  Serial.print(stepsThisLeg);
  Serial.print(F("/"));
  Serial.print(STEPS_PER_LEG);
  Serial.print(F("  PIR="));
  Serial.print(pirRaw == HIGH ? F("ДВИЖЕНИЕ") : F("тишина"));
  Serial.print(F("  auto="));
  Serial.println(autoMode ? F("ВКЛ") : F("ВЫКЛ"));
}

// Начать цикл с нуля: вперёд 100000
void beginCycle(const __FlashStringHelper* why) {
  Serial.print(F(" >> CYCLE START ("));
  Serial.print(why);
  Serial.println(F(")"));

  stepsThisLeg = 0;
  phase = PH_FORWARD;
  phaseStartedMs = millis();
  lastStepUs = micros();

  digitalWrite(dirPin, LOW);    // "вперёд" = направление при запуске (LOW)
  motorEnable(true);
}

// СТОП: прервать цикл и обесточить мотор
void abortCycle(const __FlashStringHelper* why) {
  if (phase == PH_WAIT || phase == PH_DONE) return;
  phase = PH_WAIT;
  motorEnable(false);
  Serial.print(F(" >> ABORT ("));
  Serial.print(why);
  Serial.println(F(")"));
}

// ================== PIR: фронт движения ==================
// Возвращает true, если за этот вызов появился НОВЫЙ фронт движения.
bool pollPirForRising(unsigned long now) {
  bool rising = false;

  int p = digitalRead(PIR_PIN);

  // Фильтр: фиксируем новое состояние только если пин держится > PIR_HOLDOFF_MS
  if (p != pirRaw && (now - pirRawSince) > PIR_HOLDOFF_MS) {
    if (p == HIGH && pirRaw == LOW) {
      rising = true;                 // только что появилось движение
    }
    pirRaw = p;
    pirRawSince = now;
  } else if (p != pirPinPrev) {
    // пин дёрнулся, но ещё не прошёл фильтр — запомним момент
    pirRawSince = now;
  }
  pirPinPrev = p;

  return rising;
}

// ================== КОМАНДЫ ==================
void handleSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r' || c == '\n' || c == ' ' || c == '\t') continue;

    switch (c) {
      case 'r': case 'R':
        abortCycle(F("новый ручной цикл"));
        beginCycle(F("вручную"));
        break;
      case 's': case 'S':
        abortCycle(F("вручную"));
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
        Serial.println(F(" >> авто по PIR ВКЛ"));
        break;
      case 'm': case 'M':
        autoMode = false;
        Serial.println(F(" >> авто по PIR ВЫКЛ (только ручной r)"));
        break;
      default:
        Serial.print(F(" ?? неизвестная команда '"));
        Serial.print(c);
        Serial.println(F("'"));
        break;
    }
  }
}

// ================== ЦИКЛ ==================
void updateCycle() {
  unsigned long now = millis();
  unsigned long nowUs = micros();

  switch (phase) {

    case PH_WAIT:
      // Ждём новое движение (фронт). Если пришло — начинаем цикл.
      if (autoMode && pollPirForRising(now)) {
        beginCycle(F("PIR: движение"));
      }
      break;

    case PH_FORWARD:
      if (nowUs - lastStepUs >= (unsigned long)STEP_DELAY_US) {
        lastStepUs = nowUs;
        stepOnce();
        stepsThisLeg++;

        if (stepsThisLeg >= STEPS_PER_LEG) {
          // Вперёд доехали — стоп, пауза 10 с
          Serial.println(F(" >> вперёд готово, пауза 10 с"));
          motorEnable(false);
          phase = PH_PAUSE;
          phaseStartedMs = now;
        }
      }
      break;

    case PH_PAUSE:
      if (now - phaseStartedMs >= PAUSE_MS) {
        // Пауза кончилась — назад 1000
        Serial.println(F(" >> пауза кончилась, назад 10000 шагов"));
        stepsThisLeg = 0;
        phase = PH_BACKWARD;
        lastStepUs = micros();
        digitalWrite(dirPin, HIGH);  // обратное направление (поменялись)
        motorEnable(true);
      }
      break;

    case PH_BACKWARD:
      if (nowUs - lastStepUs >= (unsigned long)STEP_DELAY_US) {
        lastStepUs = nowUs;
        stepOnce();
        stepsThisLeg++;

        if (stepsThisLeg >= STEPS_PER_LEG) {
          Serial.println(F(" >> назад готово, цикл завершён"));
          motorEnable(false);
          phase = PH_DONE;
        }
      }
      break;

    case PH_DONE:
      // Сразу возвращаемся к ожиданию; считаем это «нормальным» входом
      phase = PH_WAIT;
      break;
  }

  // Периодический лог, пока идёт движение или пауза
  if (phase != PH_WAIT && now - lastStatusMs >= STATUS_PERIOD_MS) {
    lastStatusMs = now;
    Serial.print(F("   ..."));
    printState();
  }
}

// ================== LED ==================
void updateLed(unsigned long now) {
  static unsigned long lastToggle = 0;
  static bool state = false;
  unsigned long period = (phase == PH_WAIT) ? 800 : 150;  // быстрее = цикл идёт
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
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(PIR_PIN, INPUT);

  digitalWrite(stepPin, LOW);
  digitalWrite(dirPin,  HIGH);
  motorEnable(false);

  delay(200);

  Serial.println();
  Serial.println(F("========================================="));
  Serial.println(F(" AutoRack — v3: цикл по движению"));
  Serial.println(F(" движение -> вперёд 10000 -> пауза 10с -> назад 1000"));
  Serial.println(F("-----------------------------------------"));
  Serial.println(F(" STEP=A1 DIR=A2 EN=A3 PIR=A0"));
  Serial.println(F(" r=цикл  s=стоп  p=состояние  e/f=драйвер  a/m=авто"));
  Serial.println(F("========================================="));
  printState();
}

// ================== LOOP ==================
void loop() {
  unsigned long now = millis();
  handleSerial();
  updateCycle();
  updateLed(now);
}
