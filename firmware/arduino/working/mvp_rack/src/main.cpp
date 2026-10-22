/*
 * AutoRack — MVP: автоматическое натяжение пружины (Arduino Uno)
 * ==================================================================
 * Минимальная рабочая версия: ОДИН шаговый двигатель NEMA 17 (DRV8825),
 * который растягивает пружину, когда человек подходит, и ослабляет её,
 * когда человек ушёл. Крючки разъезжаются при натяжении и съезжаются
 * при ослаблении.
 *
 * Конечный автомат (всё неблокирующее, без delay() в работе):
 *
 *   BOOT --HOME(крутим к SLACK)--> SLEEP
 *   SLEEP --PIR=HIGH-------------> DISTRIBUTE (крутим к TIGHT)
 *   DISTRIBUTE --LIMIT_TIGHT-----> HOLD (бип: крючки разъехались)
 *   HOLD --тишина HOLD_MS--------> COOLDOWN (крутим к SLACK)
 *   COOLDOWN --LIMIT_SLACK-------> SLEEP (мотор обесточен)
 *
 * Надёжность: мы НЕ считаем шаги для остановки. Останавливаемся только
 * по концевикам. Пропуск шагов от натянутой пружины или перезагрузка
 * Uno не "сдвигают" систему: каждый цикл начинается и заканчивается
 * в физически известном положении (защёлкнутый концевик).
 *
 * ------------------------------------------------------------------
 * РАСПИНОВКА (как в nema_test / uno_all_components — не меняем):
 *   STEP      -> A1
 *   DIR       -> A2
 *   EN        -> A3      LOW = драйвер включён
 *   LIMIT_TIGHT -> D2    INPUT_PULLUP, микрик на GND, LOW = нажат
 *   LIMIT_SLACK -> D4    INPUT_PULLUP, микрик на GND, LOW = нажат
 *   BUZZER    -> D8      пассивный зуммер (tone() / noTone())
 *   LED       -> D13     статус (встроенный на плате)
 *   PIR OUT   -> A0      (как в uno_all_components)
 *
 *   VDD  -> 5V , GND -> GND Uno + GND БП (общий!)
 *   VMOT -> +12В + конденсатор 100..220 мкФ
 *   !! DRV8825: RST и SLP соединить перемычкой, иначе "спит".
 *   Ток: I = Vref * 2 (Rsense = 0.1). Для теста Vref 0.3..0.4 В.
 *
 * ЛОГИКА КОНЦЕВИКОВ (см. firmware/arduino/PINOUT.md):
 *   Микрик стоит в крайнем положении. Когда верёвка в этом положении —
 *   микрик нажат (LOW). При уходе из положения — отпущен (HIGH).
 *   Поэтому для остановки ждём НАЖАТИЕ (LOW) нужного концевика.
 *
 * ПИТАНИЕ: Uno от USB, 12В на NEMA через драйвер. 12В на Uno НЕ подавать.
 * Serial Monitor: 9600, окончание строки LF.
 *
 * Прошивка (из папки firmware/arduino/working/mvp_rack):
 *   pio run -t upload
 *   pio device monitor
 */

#include <Arduino.h>

// ================== КОНСТАНТЫ (настрой под себя) ==================
static const unsigned int  STEP_DELAY_US = 2000;    // мкс/шаг (меньше = быстрее)
static const unsigned long HOLD_MS       = 30000UL; // ждём "тишину" после движения
static const unsigned long PIR_HOLDOFF_MS= 1500UL;  // антидребезг PIR
static const unsigned long LED_BLINK_MS  = 400UL;   // мигание LED в движении

// ================== ПИНЫ ==================
#define stepPin       A1
#define dirPin        A2
#define ena           A3      // LOW = вкл
#define LIMIT_TIGHT   2       // D2 — натянуто (крючки разъехались)
#define LIMIT_SLACK   4       // D4 — ослаблено (крючки съехались)
#define BUZZER_PIN    8
#define LED_PIN       13
#define PIR_PIN       A0

// ================== СОСТОЯНИЯ ==================
enum State {
  ST_BOOT,       // инициализация
  ST_HOME,       // крутим к SLACK, чтобы знать точку отсчёта
  ST_SLEEP,      // мотор обесточен, ждём PIR
  ST_DISTRIBUTE, // крутим к TIGHT
  ST_HOLD,       // натянуто, ждём пока человек уйдёт
  ST_COOLDOWN,   // крутим к SLACK, потом обесточиваем
  ST_ERROR       // концевик не сработал (защита от зависания)
};
State state = ST_BOOT;

// ================== СОСТОЯНИЕ ==================
unsigned long stateEnteredMs = 0;
unsigned long lastStepUs     = 0;
unsigned long stepCount      = 0;

bool  dirCw = true;            // true = натягивать (к TIGHT)

// PIR
int           pirLast = LOW;
unsigned long pirLastChange = 0;
unsigned long lastMotionMs  = 0;   // последний момент, когда PIR был HIGH

// LED
unsigned long ledLastToggle = 0;
bool          ledState      = false;

// Защита от "мотор крутит, а концевик не реагирует"
unsigned long phaseStartMs   = 0;
static const unsigned long PHASE_TIMEOUT_MS = 20000UL;

// ================== ЗУММЕР ==================
bool          beepActive = false;
unsigned long beepStopAt = 0;

void startBeep(unsigned int freq, unsigned long ms) {
  tone(BUZZER_PIN, freq);
  beepActive = true;
  beepStopAt = millis() + ms;
}

void updateBeep() {
  if (beepActive && millis() >= beepStopAt) {
    noTone(BUZZER_PIN);
    beepActive = false;
  }
}

// ================== УПРАВЛЕНИЕ МОТОРОМ ==================
void motorEnable(bool on) {
  digitalWrite(ena, on ? LOW : HIGH);
}

void stepOnce() {
  digitalWrite(dirPin, dirCw ? HIGH : LOW);
  digitalWrite(stepPin, HIGH);
  delayMicroseconds(3);       // DRV8825: STEP >= 1.9 мкс
  digitalWrite(stepPin, LOW);
  stepCount++;
}

// Крутим по таймеру в заданном направлении.
// Возвращает true, если нужный концевик нажат (дошли).
bool driveToLimit(int limitPin) {
  unsigned long nowUs = micros();
  if (nowUs - lastStepUs >= (unsigned long)STEP_DELAY_US) {
    lastStepUs = nowUs;
    stepOnce();
  }
  return digitalRead(limitPin) == LOW;   // LOW = нажат
}

// ================== PIR ==================
// Обновляем "последнее движение". Возвращает true, если сейчас движение.
bool pollPir(unsigned long now) {
  int p = digitalRead(PIR_PIN);
  if (p != pirLast && (now - pirLastChange) > PIR_HOLDOFF_MS) {
    pirLastChange = now;
    pirLast = p;
  }
  if (pirLast == HIGH) lastMotionMs = now;
  return pirLast == HIGH;
}

// ================== LED ==================
// SLEEP: медленно мигает; движение: быстрее; error: быстро.
void updateLed(unsigned long now) {
  unsigned long period;
  switch (state) {
    case ST_ERROR: period = 100;  break;   // быстро мигает
    case ST_SLEEP: period = 1000; break;   // редко
    default:       period = LED_BLINK_MS; break;
  }
  if (now - ledLastToggle >= period) {
    ledLastToggle = now;
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState ? HIGH : LOW);
  }
}

// ================== ПЕРЕХОДЫ ==================
const __FlashStringHelper* stateName(State s) {
  switch (s) {
    case ST_BOOT:       return F("BOOT");
    case ST_HOME:       return F("HOME");
    case ST_SLEEP:      return F("SLEEP");
    case ST_DISTRIBUTE: return F("DISTRIBUTE");
    case ST_HOLD:       return F("HOLD");
    case ST_COOLDOWN:   return F("COOLDOWN");
    case ST_ERROR:      return F("ERROR");
  }
  return F("?");
}

void goTo(State s, unsigned long now) {
  Serial.print(F("  ["));
  Serial.print(stateName(state));
  Serial.print(F("] -> ["));
  Serial.print(stateName(s));
  Serial.println(F("]"));
  state = s;
  stateEnteredMs = now;
  phaseStartMs   = now;

  switch (s) {
    case ST_HOME:
      motorEnable(true);
      dirCw = false;               // ослаблять, чтобы найти SLACK
      break;

    case ST_SLEEP:
      motorEnable(false);          // не жжём ток
      lastMotionMs = now;
      Serial.println(F("     мотор обесточен, жду PIR"));
      break;

    case ST_DISTRIBUTE:
      motorEnable(true);
      dirCw = true;                // натягивать
      Serial.println(F("     натягиваю пружину"));
      break;

    case ST_HOLD:
      motorEnable(true);           // удерживаем натяжение
      Serial.println(F("     крючки разъехались, ждём человека"));
      break;

    case ST_COOLDOWN:
      motorEnable(true);
      dirCw = false;               // ослаблять
      Serial.println(F("     ослабляю пружину"));
      break;

    case ST_ERROR:
      motorEnable(false);
      Serial.println(F("     !! концевик не сработал"));
      break;

    default: break;
  }
}

// ================== ОБРАБОТКА Serial (ручные команды) ==================
void handleSerial(unsigned long now) {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == ' ' || c == '\r' || c == '\n') continue;
    if (c == 'h' || c == 'H') {
      Serial.println(F(">> ручной HOME"));
      goTo(ST_HOME, now);
    } else if (c == 's' || c == 'S') {
      Serial.println(F(">> ручной STOP -> SLEEP"));
      goTo(ST_SLEEP, now);
    } else if (c == 'd' || c == 'D') {
      Serial.println(F(">> ручной DISTRIBUTE"));
      goTo(ST_DISTRIBUTE, now);
    } else if (c == 'r' || c == 'R') {
      Serial.println(F(">> сброс в HOME"));
      goTo(ST_HOME, now);
    }
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
  pinMode(PIR_PIN, INPUT);

  digitalWrite(stepPin, LOW);
  digitalWrite(dirPin,  HIGH);
  motorEnable(false);   // на старте мотор выключен

  delay(200);

  Serial.println();
  Serial.println(F("========================================="));
  Serial.println(F(" AutoRack — MVP (NEMA 17 / DRV8825)"));
  Serial.println(F("-----------------------------------------"));
  Serial.println(F(" STEP=A1 DIR=A2 EN=A3"));
  Serial.println(F(" TIGHT=D2 SLACK=D4  PIR=A0  BUZZER=D8"));
  Serial.println(F(" Команды: h=HOME  s=STOP  d=DISTRIBUTE  r=RESET"));
  Serial.println(F(" !! DRV8825: RST+SLP перемычка, Vref 0.3-0.4 В"));
  Serial.println(F("========================================="));
  Serial.print(F(" стартовые концевики: TIGHT="));
  Serial.print(digitalRead(LIMIT_TIGHT) == LOW ? F("нажат") : F("отпущен"));
  Serial.print(F("  SLACK="));
  Serial.println(digitalRead(LIMIT_SLACK) == LOW ? F("нажат") : F("отпущен"));

  state = ST_BOOT;
  goTo(ST_HOME, millis());
}

// ================== LOOP ==================
void loop() {
  unsigned long now = millis();

  handleSerial(now);
  updateBeep();
  updateLed(now);

  // Считываем PIR (движение регистрирует lastMotionMs)
  bool motion = pollPir(now);

  switch (state) {

    case ST_HOME: {
      if (driveToLimit(LIMIT_SLACK)) {
        Serial.print(F("     HOME достигнут, шагов="));
        Serial.println(stepCount);
        stepCount = 0;
        startBeep(900, 120);
        goTo(ST_SLEEP, now);
      } else if (now - phaseStartMs > PHASE_TIMEOUT_MS) {
        goTo(ST_ERROR, now);
      }
      break;
    }

    case ST_SLEEP: {
      if (motion) {
        startBeep(600, 60);
        goTo(ST_DISTRIBUTE, now);
      }
      break;
    }

    case ST_DISTRIBUTE: {
      if (driveToLimit(LIMIT_TIGHT)) {
        Serial.print(F("     TIGHT достигнут, шагов="));
        Serial.println(stepCount);
        startBeep(1200, 150);
        goTo(ST_HOLD, now);
      } else if (now - phaseStartMs > PHASE_TIMEOUT_MS) {
        goTo(ST_ERROR, now);
      }
      break;
    }

    case ST_HOLD: {
      if (motion) {
        lastMotionMs = now;      // движение продолжается — продлеваем таймер
      }
      if (now - lastMotionMs >= HOLD_MS) {
        goTo(ST_COOLDOWN, now);
      }
      break;
    }

    case ST_COOLDOWN: {
      if (driveToLimit(LIMIT_SLACK)) {
        Serial.print(F("     SLACK достигнут, шагов="));
        Serial.println(stepCount);
        stepCount = 0;
        startBeep(500, 100);
        goTo(ST_SLEEP, now);
      } else if (now - phaseStartMs > PHASE_TIMEOUT_MS) {
        goTo(ST_ERROR, now);
      }
      break;
    }

    case ST_ERROR: {
      static unsigned long lastErrLog = 0;
      if (now - lastErrLog >= 3000) {
        lastErrLog = now;
        Serial.print(F(" !! ERROR: TIGHT="));
        Serial.print(digitalRead(LIMIT_TIGHT) == LOW ? F("нажат") : F("отп."));
        Serial.print(F(" SLACK="));
        Serial.print(digitalRead(LIMIT_SLACK) == LOW ? F("нажат") : F("отп."));
        Serial.println(F("  (команда r = RESET)"));
      }
      break;
    }

    case ST_BOOT:
    default:
      goTo(ST_HOME, now);
      break;
  }
}