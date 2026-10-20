/*
 * AutoRack — НЕПРЕРЫВНОЕ ВРАЩЕНИЕ NEMA 17 / DRV8825 (Arduino Uno)
 * ==================================================================
 * Мотор крутится интервалами: 10 секунд вращение -> 10 секунд пауза,
 * и так по кругу. Направление чередуется каждый цикл (CW/CCW),
 * чтобы вал не "залипал" в одном положении.
 *
 * Настройки — в блоке КОНСТАНТЫ ниже:
 *   SPIN_MS   — длительность вращения (мс)      [по умолчанию 10000 = 10 c]
 *   PAUSE_MS  — длительность паузы (мс)         [по умолчанию 10000 = 10 c]
 *   STEP_DELAY_US — пауза между шагами (мкс)    [меньше = быстрее]
 *   ALTERNATE_DIR — 1 = чередовать направление каждый цикл
 *
 * Если PAUSE_MS = 0 — мотор крутится НЕПРЕРЫВНО без остановок.
 *
 * ------------------------------------------------------------------
 * РАСПИНОВКА (Uno + DRV8825):
 *   STEP -> A1 , DIR -> A2 , EN -> A3 (LOW = вкл)
 *   VDD  -> 5V , GND -> GND Uno + GND БП (общий!)
 *   VMOT -> +12В + конденсатор 100..220 мкФ
 *   !! DRV8825: RST и SLP соединить перемычкой, иначе "спит".
 *   Ток: I = Vref * 2 (Rsense = 0.1). Для теста Vref 0.3..0.4 В.
 *
 * Прошивка:  pio run -e uno_spin -t upload
 * Монитор:   pio device monitor -b 9600 --eol LF
 */

#include <Arduino.h>

// ================== КОНСТАНТЫ (настрой под себя) ==================
static const unsigned long SPIN_MS        = 10000UL;  // вращение 10 c
static const unsigned long PAUSE_MS       = 10000UL;  // пауза   10 c
static const unsigned int  STEP_DELAY_US  = 2000;     // мкс/шаг (меньше = быстрее)
static const bool          ALTERNATE_DIR  = true;     // чередовать направление

// ================== ПИНЫ ==================
#define stepPin  A1
#define dirPin   A2
#define ena      A3      // LOW = вкл у DRV8825

// ================== СОСТОЯНИЕ ==================
enum Phase { SPIN, PAUSE };
Phase         phase      = SPIN;
unsigned long phaseStart = 0;
bool          dirCw      = true;     // текущее направление
unsigned long lastStepUs = 0;
unsigned long stepCount  = 0;        // общий счётчик шагов (для лога)

// ================== ШАГ ==================
void stepOnce() {
  digitalWrite(dirPin, dirCw ? HIGH : LOW);
  digitalWrite(stepPin, HIGH);
  delayMicroseconds(3);              // DRV8825: STEP >= 1.9 мкс
  digitalWrite(stepPin, LOW);
}

// ================== SETUP ==================
void setup() {
  Serial.begin(9600);

  pinMode(stepPin, OUTPUT);
  pinMode(dirPin,  OUTPUT);
  pinMode(ena,     OUTPUT);

  digitalWrite(ena, LOW);            // вкл драйвер
  digitalWrite(stepPin, LOW);
  digitalWrite(dirPin, HIGH);

  delay(200);

  Serial.println();
  Serial.println(F("========================================="));
  Serial.println(F(" AutoRack — НЕПРЕРЫВНОЕ вращение NEMA"));
  Serial.println(F("-----------------------------------------"));
  Serial.print(F(" SPIN  = ")); Serial.print(SPIN_MS);  Serial.println(F(" ms"));
  Serial.print(F(" PAUSE = ")); Serial.print(PAUSE_MS); Serial.println(F(" ms"));
  Serial.print(F(" STEP_DELAY_US = ")); Serial.println(STEP_DELAY_US);
  Serial.print(F(" ALTERNATE_DIR = ")); Serial.println(ALTERNATE_DIR ? 1 : 0);
  Serial.println(F(" !! DRV8825: RST+SLP перемычка, Vref 0.3-0.4 В"));
  Serial.println(F("========================================="));

  phase      = SPIN;
  phaseStart = millis();
  dirCw      = true;
  Serial.println(F("START: вращение..."));
}

// ================== LOOP ==================
void loop() {
  unsigned long nowMs = millis();
  unsigned long nowUs = micros();

  if (phase == SPIN) {
    // крутим, пока не вышло SPIN_MS
    if (nowMs - phaseStart >= SPIN_MS) {
      // конец вращения -> пауза (если PAUSE_MS > 0) или сразу новый цикл
      phase = PAUSE;
      phaseStart = nowMs;
      if (ALTERNATE_DIR) dirCw = !dirCw;   // смена направления
      Serial.print(F("PAUSE. steps="));
      Serial.println(stepCount);
      if (PAUSE_MS == 0) {
        phase = SPIN;                       // непрерывный режим
        phaseStart = nowMs;
        Serial.println(F("SPIN (continuous)..."));
      }
      return;
    }

    // неблокирующий шаг по таймеру
    if (nowUs - lastStepUs >= (unsigned long)STEP_DELAY_US) {
      lastStepUs = nowUs;
      stepOnce();
      stepCount++;
    }

  } else { // PAUSE
    if (nowMs - phaseStart >= PAUSE_MS) {
      phase = SPIN;
      phaseStart = nowMs;
      Serial.print(F("SPIN dir="));
      Serial.println(dirCw ? F("CW") : F("CCW"));
    }
    // на паузе обмотки под током (удержание). Чтобы обесточить — digitalWrite(ena, HIGH);
  }
}
