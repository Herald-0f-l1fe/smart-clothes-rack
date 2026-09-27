/*
 * AutoRack — тест 02: зуммер + концевики (микрики)
 * ------------------------------------------------------------
 * Цель: проверить, что ESP32 управляет зуммером и читает
 *       концевики KW11-3Z. LED-лента в этом тесте НЕ используется
 *       (подключим отдельно).
 *
 * Подключение (как у тебя сейчас):
 *   Зуммер:
 *     + / сигнал  -> GPIO4
 *     − / GND     -> GND
 *   Концевик 1 (KW11-3Z):
 *     NO  -> GPIO35
 *     COM -> GND          (используем INPUT_PULLUP, резисторы не нужны)
 *   Концевик 2 (KW11-3Z):
 *     NO  -> GPIO39
 *     COM -> GND
 *   Если второй концевик ещё не подключён — ничего страшного,
 *   вход просто будет читаться как "не нажат".
 *
 * Что делает скетч:
 *   1. На старте — короткая "мелодия" из 3 нот (проверка зуммера).
 *   2. В цикле читает оба концевика.
 *   3. При нажатии любого — Serial-сообщение + короткий "бип".
 *   4. С антидребезгом (debounce).
 *
 * Питание: ESP32 от USB. 12 В НЕ подавать.
 * Serial Monitor: 115200.
 */

#define BUZZER_PIN   4     // зуммер (пассивный)
#define SWITCH1_PIN  35    // концевик 1, NO -> GPIO35, COM -> GND
#define SWITCH2_PIN  39    // концевик 2, NO -> GPIO39, COM -> GND

#define DEBOUNCE_MS  30    // антидребезг, мс

// Предыдущее устойчивое состояние (INPUT_PULLUP: не нажат = HIGH)
int last1 = HIGH;
int last2 = HIGH;

// Время последнего изменения для простого debounce
unsigned long lastChange1 = 0;
unsigned long lastChange2 = 0;

// Короткий "бип" зуммером.
void beep(unsigned int freq, unsigned long ms) {
  tone(BUZZER_PIN, freq);
  delay(ms);
  noTone(BUZZER_PIN);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("=========================================");
  Serial.println(" AutoRack — тест 02: зуммер + концевики");
  Serial.println("-----------------------------------------");
  Serial.println(" Зуммер:  GPIO4");
  Serial.println(" Микрик1: GPIO35  (NO->35, COM->GND)");
  Serial.println(" Микрик2: GPIO39  (NO->39, COM->GND)");
  Serial.println(" Нажми на концевики — должны быть 'бипы'.");
  Serial.println("=========================================");
  Serial.println();

  // Концевики: подтяжка к питанию внутри. Нажат -> LOW.
  pinMode(SWITCH1_PIN, INPUT_PULLUP);
  pinMode(SWITCH2_PIN, INPUT_PULLUP);

  // Стартовая проверка зуммера — три ноты.
  Serial.println("Проверка зуммера: 3 ноты...");
  beep(600, 150);  delay(100);
  beep(900, 150);  delay(100);
  beep(1200, 150); delay(200);
  Serial.println("Если ты слышал три ноты — зуммер подключён верно.");
  Serial.println();

  // Запоминаем стартовое состояние концевиков.
  last1 = digitalRead(SWITCH1_PIN);
  last2 = digitalRead(SWITCH2_PIN);
}

void loop() {
  unsigned long now = millis();

  // ---- Концевик 1 ----
  int cur1 = digitalRead(SWITCH1_PIN);
  if (cur1 != last1 && (now - lastChange1) > DEBOUNCE_MS) {
    lastChange1 = now;
    last1 = cur1;
    if (cur1 == LOW) {
      Serial.println("[SW1] НАЖАТ  (доехали до конца хода -> стоп мотора)");
      beep(1000, 60);
    } else {
      Serial.println("[SW1] отпущен");
    }
  }

  // ---- Концевик 2 ----
  int cur2 = digitalRead(SWITCH2_PIN);
  if (cur2 != last2 && (now - lastChange2) > DEBOUNCE_MS) {
    lastChange2 = now;
    last2 = cur2;
    if (cur2 == LOW) {
      Serial.println("[SW2] НАЖАТ  (доехали до конца хода -> стоп мотора)");
      beep(1400, 60);
    } else {
      Serial.println("[SW2] отпущен");
    }
  }

  // Раз в секунду показываем "сырое" состояние обоих входов,
  // чтобы видеть, что вообще приходит с пинов.
  static unsigned long lastPrint = 0;
  if (now - lastPrint > 1000) {
    lastPrint = now;
    Serial.printf("  состояние: SW1=%s  SW2=%s\n",
                  digitalRead(SWITCH1_PIN) == LOW ? "нажат" : "отп.",
                  digitalRead(SWITCH2_PIN) == LOW ? "нажат" : "отп.");
  }
}
