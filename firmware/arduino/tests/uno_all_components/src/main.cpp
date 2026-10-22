/*
 * AutoRack — Firmware Rack (Arduino Uno) — ЕДИНЫЙ ТЕСТ КОМПОНЕНТОВ
 * ==================================================================
 * Порт ESP32-тестов (01–05) на Arduino Uno R3.
 * Тестируется ВСЁ, КРОМЕ шагового двигателя NEMA 17 / DRV8825
 * (для него отдельный скетч — см. nema_test.cpp).
 *
 * Что тестируется здесь:
 *   - Зуммер (пассивный)
 *   - Концевики 1 и 2 (KW11-3Z, микрики)
 *   - PIR HC-SR501
 *   - Шаговый двигатель 28BYJ-48 через драйвер ULN2003
 *   - Встроенный светодиод (D13)
 *
 * ------------------------------------------------------------------
 * РАСПИНОВКА (Arduino Uno R3):
 *
 *   Зуммер   + / S   -> D8
 *            − / GND -> GND
 *   Концевик 1 NO    -> D2      COM -> GND   (INPUT_PULLUP)
 *   Концевик 2 NO    -> D4      COM -> GND   (INPUT_PULLUP)
 *   PIR HC-SR501 OUT -> A0      VCC -> 5В, GND -> GND
 *   ULN2003  IN1     -> D5
 *            IN2     -> D6
 *            IN3     -> D7
 *            IN4     -> D10
 *            VCC     -> +5В (внешний, >=1А)
 *            GND     -> GND (общий с Uno!)
 *   5-pin разъём     -> 28BYJ-48
 *   Встроенный LED   -> D13 (уже на плате)
 *
 *   NEMA 17 / DRV8825 — в этом скетче НЕ используется,
 *   см. отдельный nema_test.cpp (STEP/DIR/EN/endstop).
 *
 * ------------------------------------------------------------------
 * WHY (отличия от ESP32-версии):
 *   - У Uno нет Serial.printf -> используем print + F() (экономия RAM).
 *   - Пины ESP32 (25/26/27/32/33/34/...) на Uno отсутствуют
 *     -> переназначены на существующие цифровые/аналоговые.
 *   - tone() на Uno занимает таймер2 (D3/D11) -> под датчики
 *     эти пины не занимаем (PIR на A0), чтобы не было наводок.
 *
 * Питание: Uno от USB. 12В на Uno НЕ подавать.
 * Serial Monitor: 9600.
 */

#include <Arduino.h>
#include <Stepper.h>

// ================== ПИНЫ ==================
#define BUZZER_PIN    8
#define SWITCH1_PIN   2
#define SWITCH2_PIN   4
#define PIR_PIN       A0
#define LED_PIN       13

#define IN1  5
#define IN2  6
#define IN3  7
#define IN4  10

// ================== НАСТРОЙКИ ==================
#define STEPS_PER_REV   2048
#define MOTOR_RPM       8       // рабочая скорость 28BYJ-48
#define STEPS_PER_TICK  8       // порция шагов за "тик"
#define DEBOUNCE_MS     30
#define STATUS_PERIOD_MS 1000
#define PIR_WARMUP_MS   8000    // для теста: 8с, в реальности 30-60с

Stepper motor(STEPS_PER_REV, IN1, IN3, IN2, IN4);

// ================== РЕЖИМЫ ==================
// 0 = всё сразу (интеграция), 1 = зуммер, 2 = концевики,
// 3 = PIR, 4 = 28BYJ-48
int g_mode = 0;

// ================== СОСТОЯНИЕ ==================
int direction = +1;
int sw1Last = HIGH, sw2Last = HIGH;
unsigned long sw1Change = 0, sw2Change = 0;
unsigned long pirLastChange = 0;

bool beepActive = false;
unsigned long beepStopAt = 0;

unsigned long lastStatus = 0;
unsigned long lastStepTime = 0;
unsigned long lastLedBlink = 0;
bool ledState = false;

// ================== ХЕЛПЕРЫ ==================

// Неблокирующий "бип".
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

// Мигание встроенным LED (1 Гц), чтобы видеть что скетч жив.
void updateLed(unsigned long now) {
  if (now - lastLedBlink >= 500) {
    lastLedBlink = now;
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState ? HIGH : LOW);
  }
}

void printMenu() {
  Serial.println();
  Serial.println(F("======== МЕНЮ ТЕСТА (введи цифру) ========"));
  Serial.println(F(" 0 - ВСЁ СРАЗУ (интеграция, как тест 05)"));
  Serial.println(F(" 1 - Зуммер"));
  Serial.println(F(" 2 - Концевики"));
  Serial.println(F(" 3 - PIR HC-SR501"));
  Serial.println(F(" 4 - 28BYJ-48 (ULN2003)"));
  Serial.println(F("=========================================="));
}

void printHeader() {
  Serial.println();
  Serial.println(F("========================================="));
  Serial.println(F(" AutoRack — тест компонентов (Uno R3)"));
  Serial.println(F("-----------------------------------------"));
  Serial.println(F(" Зуммер:  D8"));
  Serial.println(F(" Микрик1: D2    Микрик2: D4"));
  Serial.println(F(" PIR:     A0"));
  Serial.println(F(" ULN2003: 5/6/7/10 -> 28BYJ-48"));
  Serial.println(F(" LED:     D13 (мигает)"));
  Serial.println(F("-----------------------------------------"));
  Serial.println(F(" NEMA17/DRV8825 — отдельный скетч!"));
  Serial.println(F("========================================="));
}

// ---- Обработка Serial-команд (выбор режима) ----
void handleSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c >= '0' && c <= '4') {
      g_mode = c - '0';
      // сброс состояния при смене режима
      direction = +1;
      Serial.println();
      Serial.print(F(">> Режим "));
      Serial.print(g_mode);
      switch (g_mode) {
        case 0: Serial.println(F(": ВСЁ СРАЗУ")); break;
        case 1: Serial.println(F(": Зуммер (играю 3 ноты)")); break;
        case 2: Serial.println(F(": Концевики — дави микрики")); break;
        case 3: Serial.println(F(": PIR — маши рукой, нужен прогрев!")); break;
        case 4: Serial.println(F(": 28BYJ-48 — мотор вперёд/назад")); break;
      }
      if (g_mode == 1) {
        // тест зуммера: 3 ноты
        startBeep(600, 150);
        delay(200); startBeep(900, 150);
        delay(200); startBeep(1200, 200);
        Serial.println(F(" Если слышал 3 ноты — зуммер ОК."));
      }
      if (g_mode == 3) {
        Serial.println(F(" Прогрев PIR, ждём несколько секунд..."));
      }
      if (g_mode == 4) {
        motor.setSpeed(MOTOR_RPM);
      }
      printMenu();
    }
  }
}

// ================== ТЕСТЫ ПО ОТДЕЛЬНОСТИ ==================

void testBuzzer(unsigned long now) {
  // Каждые 2 секунды короткий бип, чтобы слышать что зуммер жив.
  static unsigned long last = 0;
  if (!beepActive && now - last >= 2000) {
    last = now;
    startBeep(1000, 100);
    Serial.println(F("[BUZZER] бип"));
  }
}

void testSwitches(unsigned long now) {
  int s1 = digitalRead(SWITCH1_PIN);
  if (s1 != sw1Last && (now - sw1Change) > DEBOUNCE_MS) {
    sw1Change = now; sw1Last = s1;
    Serial.print(F("[SW1 @D2] "));
    Serial.println(s1 == LOW ? F("НАЖАТ") : F("отпущен"));
    if (s1 == LOW) startBeep(1000, 60);
  }
  int s2 = digitalRead(SWITCH2_PIN);
  if (s2 != sw2Last && (now - sw2Change) > DEBOUNCE_MS) {
    sw2Change = now; sw2Last = s2;
    Serial.print(F("[SW2 @D4] "));
    Serial.println(s2 == LOW ? F("НАЖАТ") : F("отпущен"));
    if (s2 == LOW) startBeep(1400, 60);
  }
}

void testPir(unsigned long now) {
  static int pirLast = LOW;
  static bool warmedUp = false;
  static unsigned long startTime = 0;

  if (!warmedUp) {
    if (startTime == 0) startTime = now;
    if (now - startTime >= PIR_WARMUP_MS) {
      warmedUp = true;
      Serial.println(F("[PIR] прогрев завершён, маши рукой"));
    }
    return;
  }

  int p = digitalRead(PIR_PIN);
  if (p != pirLast && (now - pirLastChange) > 200) {
    pirLastChange = now; pirLast = p;
    Serial.println(p == HIGH ? F("[PIR] ДВИЖЕНИЕ") : F("[PIR] тихо"));
    if (p == HIGH) startBeep(600, 50);
  }
}

void testStepper(unsigned long now) {
  // Неблокирующее движение порциями + реверс по концевикам.
  if (now - lastStepTime >= 2) {   // ~500 шаг/сек
    lastStepTime = now;
    motor.step(direction * STEPS_PER_TICK);
  }
  int s1 = digitalRead(SWITCH1_PIN);
  if (s1 == LOW) { direction = -1; }
  int s2 = digitalRead(SWITCH2_PIN);
  if (s2 == LOW) { direction = +1; }
}

void testIntegration(unsigned long now) {
  // Двигаем мотор по концевикам
  if (now - lastStepTime >= 2) {
    lastStepTime = now;
    motor.step(direction * STEPS_PER_TICK);
  }

  int s1 = digitalRead(SWITCH1_PIN);
  if (s1 != sw1Last && (now - sw1Change) > DEBOUNCE_MS) {
    sw1Change = now; sw1Last = s1;
    if (s1 == LOW) {
      Serial.println(F("[SW1 @D2] НАЖАТ -> РЕВЕРС + бип"));
      startBeep(1000, 70);
      direction = -1;
    }
  }
  int s2 = digitalRead(SWITCH2_PIN);
  if (s2 != sw2Last && (now - sw2Change) > DEBOUNCE_MS) {
    sw2Change = now; sw2Last = s2;
    if (s2 == LOW) {
      Serial.println(F("[SW2 @D4] НАЖАТ -> РЕВЕРС + бип"));
      startBeep(1400, 70);
      direction = +1;
    }
  }

  // PIR
  int p = digitalRead(PIR_PIN);
  static int pirLast = LOW;
  if (p != pirLast && (now - pirLastChange) > 200) {
    pirLastChange = now; pirLast = p;
    if (p == HIGH) {
      Serial.println(F("[PIR @A0] ДВИЖЕНИЕ"));
      startBeep(600, 50);
    } else {
      Serial.println(F("[PIR @A0] движение прекратилось"));
    }
  }

  // Сводный статус раз в секунду
  if (now - lastStatus >= STATUS_PERIOD_MS) {
    lastStatus = now;
    Serial.println(F("----------- STATUS -----------"));
    Serial.print(F(" Мотор:    "));
    Serial.println(direction > 0 ? F("вперёд") : F("назад"));
    Serial.print(F(" Микрик1:  "));
    Serial.println(digitalRead(SWITCH1_PIN) == LOW ? F("НАЖАТ") : F("отп."));
    Serial.print(F(" Микрик2:  "));
    Serial.println(digitalRead(SWITCH2_PIN) == LOW ? F("НАЖАТ") : F("отп."));
    Serial.print(F(" PIR:      "));
    Serial.println(digitalRead(PIR_PIN) == HIGH ? F("движение") : F("тихо"));
    Serial.print(F(" Аптайм:   "));
    Serial.print(now / 1000);
    Serial.println(F(" s"));
    Serial.println(F("------------------------------"));
  }
}

// ================== SETUP / LOOP ==================

void setup() {
  Serial.begin(9600);

  pinMode(LED_PIN, OUTPUT);
  pinMode(SWITCH1_PIN, INPUT_PULLUP);
  pinMode(SWITCH2_PIN, INPUT_PULLUP);
  pinMode(PIR_PIN, INPUT);

  motor.setSpeed(MOTOR_RPM);

  printHeader();
  Serial.println(F(" Стартовый бип зуммера..."));
  startBeep(800, 150);

  sw1Last = digitalRead(SWITCH1_PIN);
  sw2Last = digitalRead(SWITCH2_PIN);

  printMenu();
  Serial.println(F(" (по умолчанию режим 0 = ВСЁ СРАЗУ)"));
}

void loop() {
  unsigned long now = millis();

  handleSerial();
  updateBeep();
  updateLed(now);

  switch (g_mode) {
    case 1: testBuzzer(now); break;
    case 2: testSwitches(now); break;
    case 3: testPir(now); break;
    case 4: testStepper(now); break;
    default: testIntegration(now); break;
  }
}
