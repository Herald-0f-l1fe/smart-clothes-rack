/*
 * AutoRack — тест 03: PIR-датчик (HC-SR501)
 * ------------------------------------------------------------
 * Цель: проверить, что ESP32 читает PIR и что датчик ловит
 *       движение. BH1750 в этом тесте НЕ используется (его пока нет).
 *
 * Подключение:
 *   PIR HC-SR501:
 *     VCC -> +5В
 *     GND -> GND
 *     OUT -> GPIO34   (input-only пин, для логического входа подходит)
 *
 * ВАЖНО:
 *   - После включения PIR нужно 30-60 секунд "прогрева".
 *     Первую минуту может срабатывать хаотично — это нормально.
 *   - В покое OUT = LOW (0), при движении OUT = HIGH (3.3В).
 *
 * Что делает скетч:
 *   1. Ждёт прогрева и подсказывает об этом в Serial.
 *   2. Следит за состоянием PIR, при появлении движения пишет
 *      строку "[PIR] ДВИЖЕНИЕ".
 *   3. Раз в секунду печатает "сырое" состояние входа.
 *
 * Питание: ESP32 от USB. 12 В НЕ подавать.
 * Serial Monitor: 115200.
 */

#define PIR_PIN      34    // выход PIR, OUT -> GPIO34
#define DEBOUNCE_MS  200   // подавление быстрых повторов

int lastState = LOW;
unsigned long lastChange = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("=========================================");
  Serial.println(" AutoRack — тест 03: PIR HC-SR501");
  Serial.println("-----------------------------------------");
  Serial.println(" PIR OUT -> GPIO34  (VCC->5В, GND->GND)");
  Serial.println(" ЖДЁМ 30-60 СЕК ПРОГРЕВА датчика...");
  Serial.println(" Первую минуту возможны ложные срабатывания.");
  Serial.println("=========================================");
  Serial.println();

  // GPIO34 — input-only, подтяжка не нужна: PIR выдаёт активный уровень.
  pinMode(PIR_PIN, INPUT);

  // Дадим датчику прогреться, чтобы вывод не путал нас.
  Serial.println("Прогрев...");
  delay(30000);  // 30 секунд. Можно уменьшить, если не терпится.
  Serial.println("Прогрев завершён. Маши рукой перед датчиком.");
  Serial.println();

  lastState = digitalRead(PIR_PIN);
}

void loop() {
  unsigned long now = millis();
  int cur = digitalRead(PIR_PIN);

  if (cur != lastState && (now - lastChange) > DEBOUNCE_MS) {
    lastChange = now;
    lastState = cur;
    if (cur == HIGH) {
      Serial.print("[PIR] ДВИЖЕНИЕ обнаружено  | up: ");
      Serial.print(now / 1000.0, 1);
      Serial.println(" s");
    } else {
      Serial.println("[PIR] движение прекратилось");
    }
  }

  // Раз в секунду — «сырое» состояние входа.
  static unsigned long lastPrint = 0;
  if (now - lastPrint > 1000) {
    lastPrint = now;
    Serial.printf("  состояние PIR = %s\n",
                  digitalRead(PIR_PIN) == HIGH ? "HIGH (движение)" : "LOW  (тихо)");
  }
}
