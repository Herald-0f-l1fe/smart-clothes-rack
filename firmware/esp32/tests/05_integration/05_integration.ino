/*
 * AutoRack — тест 05: ИНТЕГРАЦИЯ (01 + 02 + 03 + 04 в одном скетче)
 * ==================================================================
 * Цель: собрать ВСЁ, что уже проверено, и смотреть за всем сразу
 *       в одном Serial Monitor. Это заготовка конечного автомата.
 *
 * ВАЖНО про "одновременно":
 *   ESP32 - однопоточный (точнее, без RTOS-задач). Настоящей
 *   параллельности нет. Чтобы мотор крутился И датчики опрашивались,
 *   мотор здесь шагает МАЛЕНЬКИМИ ПОРЦИЯМИ (по STEPS_PER_TICK),
 *   а между порциями мы опрашиваем концевики и PIR.
 *   Поэтому движение плавно "вплетено" в общий цикл.
 *
 * ------------------------------------------------------------------
 * ИТОГОВАЯ РАСПИНОВКА (проверь, что всё так подключено):
 *
 *   Зуммер        + / S  -> GPIO4
 *                 - / GND-> GND
 *   Концевик 1    NO     -> GPIO14
 *                 COM    -> GND
 *   Концевик 2    NO     -> GPIO27
 *                 COM    -> GND
 *   PIR HC-SR501  OUT    -> GPIO34   (VCC->5В, GND->GND)
 *   ULN2003       IN1    -> GPIO32
 *                 IN2    -> GPIO33
 *                 IN3    -> GPIO25
 *                 IN4    -> GPIO26
 *                 VCC    -> +5В (внешний >=1А)
 *                 GND    -> GND (общий с ESP32!)
 *   5-pin разъём  -> 28BYJ-48
 *
 *   (DRV8825 + NEMA 17 подключаются на СЛЕДУЮЩЕМ этапе:
 *    STEP->13, DIR->16, EN->17. Здесь ещё НЕ используется.)
 * ------------------------------------------------------------------
 *
 * Что делает скетч каждый цикл:
 *   1. Шагает мотор каретки на маленькую порцию (не блокирующе).
 *   2. Читает оба концевика -> если сработал, реверс движения + бип.
 *   3. Читает PIR -> отмечает "движение" в статусе.
 *   4. Раз в секунду печатает СВОДНЫЙ статус всех узлов.
 *
 * Serial Monitor: 115200.
 */

#include <Stepper.h>

// ---------- Пины ----------
#define BUZZER_PIN    4
#define SWITCH1_PIN   14
#define SWITCH2_PIN   27
#define PIR_PIN       34

#define IN1 32
#define IN2 33
#define IN3 25
#define IN4 26

#define STEPS_PER_REV 2048

Stepper motor(STEPS_PER_REV, IN1, IN3, IN2, IN4);

// ---------- Настройки ----------
#define MOTOR_RPM         8      // рабочая скорость каретки (медленно и надёжно)
#define STEPS_PER_TICK    16     // сколько шагов делаем за один "тик" (порция)
#define DEBOUNCE_MS       30     // антидребезг концевиков
#define STATUS_PERIOD_MS  1000   // как часто печатать сводный статус

// ---------- Состояние ----------
int direction = +1;              // +1 вперёд, -1 назад
int sw1Last = HIGH, sw2Last = HIGH;
unsigned long sw1Change = 0, sw2Change = 0;

// PIR
int pirLast = LOW;
unsigned long pirLastChange = 0;
bool pirMotion = false;
unsigned long pirLastMotionAt = 0;

// Зуммер (неблокирующий "бип")
bool beepActive = false;
unsigned long beepStopAt = 0;

// Время для периодических задач
unsigned long lastStatus = 0;
unsigned long lastStepTime = 0;
float stepIntervalUs = 0;

// ---------- Хелперы ----------

// Запустить короткий "бип" (не блокирует цикл).
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

// Интервал между шагами в микросекундах по заданному RPM.
void recalcStepInterval() {
  // шагов/сек = RPM * STEPS_PER_REV / 60
  float stepsPerSec = (float)MOTOR_RPM * STEPS_PER_REV / 60.0f;
  stepIntervalUs = 1000000.0f / stepsPerSec;
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("=========================================");
  Serial.println(" AutoRack — тест 05: ИНТЕГРАЦИЯ");
  Serial.println(" 01(blink) + 02(зуммер/концевики) +");
  Serial.println(" 03(PIR) + 04(28BYJ-48) в одном скетче");
  Serial.println("-----------------------------------------");
  Serial.println(" Зуммер:  GPIO4");
  Serial.println(" Микрик1: GPIO14   Микрик2: GPIO27");
  Serial.println(" PIR:     GPIO34");
  Serial.println(" ULN2003: 32/33/25/26 -> 28BYJ-48");
  Serial.println("-----------------------------------------");
  Serial.println(" Мотор каретки ходит вперёд-назад,");
  Serial.println(" концевики реверсируют движение,");
  Serial.println(" PIR отмечает движение человека.");
  Serial.println("=========================================");
  Serial.println();

  pinMode(SWITCH1_PIN, INPUT_PULLUP);
  pinMode(SWITCH2_PIN, INPUT_PULLUP);
  pinMode(PIR_PIN, INPUT);

  motor.setSpeed(MOTOR_RPM);
  recalcStepInterval();

  // Стартовая мелодия — подтверждение, что зуммер жив.
  Serial.println("Зуммер: стартовые ноты...");
  startBeep(800, 120);

  sw1Last = digitalRead(SWITCH1_PIN);
  sw2Last = digitalRead(SWITCH2_PIN);
  pirLast = digitalRead(PIR_PIN);
}

void loop() {
  unsigned long now = millis();

  updateBeep();

  // ================= 1. МОТОР (порциями, неблокирующе) =================
  // Делаем маленькую порцию шагов, если пришло время.
  if (now - lastStepTime >= (unsigned long)(stepIntervalUs / 1000.0f)) {
    lastStepTime = now;
    motor.step(direction * STEPS_PER_TICK);
  }

  // ================= 2. КОНЦЕВИКИ =================
  int s1 = digitalRead(SWITCH1_PIN);
  if (s1 != sw1Last && (now - sw1Change) > DEBOUNCE_MS) {
    sw1Change = now;
    sw1Last = s1;
    if (s1 == LOW) {
      Serial.println("[SW1 @GPIO14] НАЖАТ -> РЕВЕРС + бип");
      startBeep(1000, 70);
      direction = -1;                 // доехали до края — едем назад
    }
  }

  int s2 = digitalRead(SWITCH2_PIN);
  if (s2 != sw2Last && (now - sw2Change) > DEBOUNCE_MS) {
    sw2Change = now;
    sw2Last = s2;
    if (s2 == LOW) {
      Serial.println("[SW2 @GPIO27] НАЖАТ -> РЕВЕРС + бип");
      startBeep(1400, 70);
      direction = +1;                 // доехали до другого края — едем вперёд
    }
  }

  // ================= 3. PIR =================
  int p = digitalRead(PIR_PIN);
  if (p != pirLast && (now - pirLastChange) > 200) {
    pirLastChange = now;
    pirLast = p;
    if (p == HIGH) {
      pirMotion = true;
      pirLastMotionAt = now;
      Serial.println("[PIR @GPIO34] ДВИЖЕНИЕ (человек подошёл)");
      startBeep(600, 50);
    } else {
      Serial.println("[PIR @GPIO34] движение прекратилось");
    }
  }
  // Считаем движение "актуальным" 3 секунды.
  if (pirMotion && (now - pirLastMotionAt) > 3000) {
    pirMotion = false;
  }

  // ================= 4. СВОДНЫЙ СТАТУС раз в секунду =================
  if (now - lastStatus >= STATUS_PERIOD_MS) {
    lastStatus = now;

    Serial.println("----------- STATUS -----------");
    Serial.printf(" Мотор:    %s  (dir=%s, %d RPM)\n",
                  direction > 0 ? "вперёд" : "назад",
                  direction > 0 ? "+" : "-",
                  MOTOR_RPM);
    Serial.printf(" Микрик1:  %s\n",
                  digitalRead(SWITCH1_PIN) == LOW ? "НАЖАТ" : "отп.");
    Serial.printf(" Микрик2:  %s\n",
                  digitalRead(SWITCH2_PIN) == LOW ? "НАЖАТ" : "отп.");
    Serial.printf(" PIR:      %s\n",
                  pirMotion ? "ЕСТЬ ДВИЖЕНИЕ" : "тихо");
    Serial.printf(" Аптайм:   %.1f s\n", now / 1000.0);
    Serial.println("------------------------------");
  }
}
