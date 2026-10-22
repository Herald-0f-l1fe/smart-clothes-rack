/*
 * AutoRack — РУЧНОЙ СТЕНД (Arduino Uno + NEMA 17 / DRV8825)
 * ==================================================================
 * Назначение: проверить ЖЕЛЕЗО без верёвки, нажимая концевики руками.
 *
 * Скетч НЕ запускает автомат сам. Ты даёшь команды в Serial Monitor,
 * мотор крутится в заданную сторону, а ты жмёшь нужный микрик рукой.
 * Как только микрик нажался — мотор БЕЗОПАСНО останавливается.
 *
 *   t  — крутить в сторону TIGHT  (пока не нажмёшь микрик D2)
 *   k  — крутить в сторону SLACK  (пока не нажмёшь микрик D4)
 *   s  — СТОП (остановить мотор сейчас)
 *   p  — напечатать состояние обоих микриков + текущий режим
 *   e  — включить драйвер (EN=LOW), без движения
 *   f  — выключить драйвер (EN=HIGH), без движения
 *
 * Защита: если мотор крутится дольше LIMIT_TIMEOUT_MS и микрик так и
 * не нажался — авто-стоп (чтобы не порвать верёвку / не сжечь мотор).
 *
 * ------------------------------------------------------------------
 * РАСПИНОВКА (как в mvp_rack, не меняем):
 *   STEP      -> A1
 *   DIR       -> A2
 *   EN        -> A3      LOW = драйвер включён
 *   LIMIT_TIGHT -> D2    INPUT_PULLUP, микрик на GND, LOW = нажат
 *   LIMIT_SLACK -> D4    INPUT_PULLUP, микрик на GND, LOW = нажат
 *   BUZZER    -> D8
 *   LED       -> D13
 *
 * Прошивка (из папки mvp_rack_manual):
 *   pio run -t upload
 *   pio device monitor
 *
 * Серийный монитор: 9600. Отправляй одну букву (t/k/s/p/e/f) + Enter.
 */

#include <Arduino.h>

// ================== КОНСТАНТЫ ==================
static const unsigned int  STEP_DELAY_US    = 2000;     // мкс/шаг
static const unsigned long LIMIT_TIMEOUT_MS = 30000UL;  // авто-стоп, если микрик не нажали
static const unsigned long STATUS_PERIOD_MS = 1000UL;   // "живой" лог во время движения

// ================== ПИНЫ ==================
#define stepPin       A1
#define dirPin        A2
#define ena           A3
#define LIMIT_TIGHT   2
#define LIMIT_SLACK   4
#define BUZZER_PIN    8
#define LED_PIN       13

// ================== СОСТОЯНИЕ ==================
enum Dir { DIR_IDLE, DIR_TO_TIGHT, DIR_TO_SLACK };
Dir            mode = DIR_IDLE;
unsigned long  modeStartedMs = 0;
unsigned long  lastStepUs    = 0;
unsigned long  stepCount     = 0;   // шагов в текущем движении
unsigned long  lastStatusMs  = 0;

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

void printLimits() {
  Serial.print(F("   TIGHT(D2)="));
  Serial.print(digitalRead(LIMIT_TIGHT) == LOW ? F("НАЖАТ") : F("отпущен"));
  Serial.print(F("   SLACK(D4)="));
  Serial.println(digitalRead(LIMIT_SLACK) == LOW ? F("НАЖАТ") : F("отпущен"));
}

void printMenu() {
  Serial.println();
  Serial.println(F("-----------------------------------------"));
  Serial.println(F(" Команды (одна буква + Enter):"));
  Serial.println(F("   t = крутить к TIGHT (D2)"));
  Serial.println(F("   k = крутить к SLACK (D4)"));
  Serial.println(F("   s = СТОП"));
  Serial.println(F("   p = печать состояния микриков"));
  Serial.println(F("   e = драйвер ВКЛ   f = драйвер ВЫКЛ"));
  Serial.println(F("-----------------------------------------"));
}

void stopMotor(const __FlashStringHelper* why) {
  mode = DIR_IDLE;
  motorEnable(false);           // после остановки обесточиваем (мотор не греется)
  Serial.print(F(" >> STOP ("));
  Serial.print(why);
  Serial.print(F("), шагов в движении="));
  Serial.println(stepCount);
  printLimits();
}

// ================== КОМАНДЫ ==================
void handleSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r' || c == '\n' || c == ' ' || c == '\t') continue;

    switch (c) {
      case 't': case 'T':
        // к TIGHT: крутим, пока не нажмут микрик D2
        digitalWrite(dirPin, HIGH);
        mode = DIR_TO_TIGHT;
        modeStartedMs = millis();
        lastStepUs = micros();
        stepCount = 0;
        motorEnable(true);
        Serial.println(F(" >> кручу к TIGHT (D2). Нажми микрик D2 руками."));
        break;

      case 'k': case 'K':
        // к SLACK: крутим, пока не нажмут микрик D4
        digitalWrite(dirPin, LOW);
        mode = DIR_TO_SLACK;
        modeStartedMs = millis();
        lastStepUs = micros();
        stepCount = 0;
        motorEnable(true);
        Serial.println(F(" >> кручу к SLACK (D4). Нажми микрик D4 руками."));
        break;

      case 's': case 'S':
        stopMotor(F("вручную"));
        break;

      case 'p': case 'P':
        Serial.print(F(" >> режим: "));
        Serial.print(mode == DIR_IDLE ? F("IDLE") :
                     mode == DIR_TO_TIGHT ? F("->TIGHT") : F("->SLACK"));
        Serial.print(F("  EN="));
        Serial.println(digitalRead(ena));
        printLimits();
        break;

      case 'e': case 'E':
        motorEnable(true);
        Serial.println(F(" >> драйвер ВКЛ (EN=LOW)"));
        break;

      case 'f': case 'F':
        motorEnable(false);
        Serial.println(F(" >> драйвер ВЫКЛ (EN=HIGH)"));
        break;

      default:
        Serial.print(F(" ?? неизвестная команда '"));
        Serial.print(c);
        Serial.println(F("'"));
        printMenu();
        break;
    }
  }
}

// ================== ДВИЖЕНИЕ ==================
void updateMotion() {
  if (mode == DIR_IDLE) return;

  unsigned long now = millis();
  unsigned long nowUs = micros();

  // 1) Проверка: нажался ли нужный концевик
  int limitPin = (mode == DIR_TO_TIGHT) ? LIMIT_TIGHT : LIMIT_SLACK;
  if (digitalRead(limitPin) == LOW) {
    stopMotor(F("концевик нажат"));
    return;
  }

  // 2) Защита от таймаута (без верёвки мотор будет крутиться вхолостую)
  if (now - modeStartedMs >= LIMIT_TIMEOUT_MS) {
    stopMotor(F("таймаут"));
    return;
  }

  // 3) Шаг по таймеру
  if (nowUs - lastStepUs >= (unsigned long)STEP_DELAY_US) {
    lastStepUs = nowUs;
    stepOnce();
  }

  // 4) Периодический лог, чтобы видеть, что мотор живой
  if (now - lastStatusMs >= STATUS_PERIOD_MS) {
    lastStatusMs = now;
    Serial.print(F("   ...кручу, шагов="));
    Serial.print(stepCount);
    printLimits();
  }
}

// ================== LED ==================
void updateLed(unsigned long now) {
  static unsigned long lastToggle = 0;
  static bool state = false;
  unsigned long period = (mode == DIR_IDLE) ? 800 : 150;  // быстрее = крутится
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

  digitalWrite(stepPin, LOW);
  digitalWrite(dirPin,  HIGH);
  motorEnable(false);

  delay(200);

  Serial.println();
  Serial.println(F("========================================="));
  Serial.println(F(" AutoRack — РУЧНОЙ СТЕНД (без автомата)"));
  Serial.println(F(" Mотор стоит, жду команд из Serial."));
  Serial.println(F("-----------------------------------------"));
  Serial.println(F(" STEP=A1 DIR=A2 EN=A3 TIGHT=D2 SLACK=D4"));
  Serial.println(F("========================================="));
  printLimits();
  printMenu();
}

// ================== LOOP ==================
void loop() {
  unsigned long now = millis();
  handleSerial();
  updateMotion();
  updateLed(now);
}