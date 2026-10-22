/*
 * AutoRack — тест 01: проверка ESP32
 * ------------------------------------------------------------
 * Цель: убедиться, что плата ESP32 живая, прошивается и что
 *       Arduino IDE видит её корректно.
 *
 * Что должно произойти:
 *   - Встроенный светодиод (GPIO2) мигает раз в секунду.
 *   - В Serial Monitor раз в секунду печатается счётчик и
 *     количество секунд с момента старта.
 *
 * Подключение:
 *   Ничего дополнительно подключать НЕ нужно.
 *   Питание — только по USB от компьютера.
 *   ВАЖНО: 12 В на этом этапе НЕ подавать.
 *
 * Настройки Arduino IDE:
 *   - Board: "ESP32 Dev Module" (или "ESP32 DevKit V1")
 *   - Upload Speed: 115200
 *   - Serial Monitor: 115200 baud
 */

// Пин встроенного светодиода.
// На большинстве ESP32 DevKit V1 это GPIO2.
// Если твоя плата мигает другим светодиодом — замени номер пина.
#define LED_PIN 2

// Счётчик циклов мигания — чтобы видеть, что скетч реально идёт.
uint32_t blinkCount = 0;

void setup() {
  // Открываем Serial для отладки.
  Serial.begin(115200);

  // Небольшая пауза, чтобы Serial успел подняться после ресета.
  delay(1000);

  Serial.println();
  Serial.println("=========================================");
  Serial.println(" AutoRack — тест 01: blink");
  Serial.println("-----------------------------------------");
  Serial.println(" Если ты это видишь — ESP32 живой.");
  Serial.println(" Светодиод на GPIO2 должен мигать раз в секунду.");
  Serial.println("=========================================");
  Serial.println();

  pinMode(LED_PIN, OUTPUT);
}

void loop() {
  // Зажигаем светодиод.
  digitalWrite(LED_PIN, HIGH);
  Serial.print("[");
  Serial.print(blinkCount);
  Serial.print("] LED ON   | up: ");
  Serial.print(millis() / 1000.0, 1);
  Serial.println(" s");
  delay(500);

  // Гасим светодиод.
  digitalWrite(LED_PIN, LOW);
  Serial.print("[");
  Serial.print(blinkCount);
  Serial.print("] LED OFF  | up: ");
  Serial.print(millis() / 1000.0, 1);
  Serial.println(" s");
  delay(500);

  blinkCount++;
}
