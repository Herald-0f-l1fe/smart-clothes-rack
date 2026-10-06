#define homeSwitch 5  // управляющий контакт нулевого ("домашнего") положения вала ШД
#define ena 4         // управляющий контакт включения драйвера шагового двигателя (E)
#define stepPin 3     // управляющий контакт импульса шага (S)
#define dirPin 2      // управляющий контакт направления вращения (D). по часовой - 1, против - 0

int steps = 0;            // текущее значение количества пройденных шагов
int dir = 1;              // CW = 1 / CCW = 0 // переменная направления вращения вала ШД
int rotationCounter = 0;  // счётчик количества оборотов вала вокруг своей оси
int currentDelay = 10000; // время задержки переключения управляющего контакта stepPin
int steps2go = 100;       // количество шагов, которое необходимо пройти от нулевой позиции вала
String dataReceived = ""; // переменная для хранения данных полученных из Serial


// функция возвращающая значение true при достижении заданного количества шагов
boolean performAStep(int goTherePlease) { 
  while (1) { // бесконечный цикл
    if (goTherePlease == steps) { // если количество заданых и пройденых шагов совпадают
      Serial.println("Motor is already at step number " + (String)steps); // вывод информации
      break; // выход из бесконечного цикла
    }
    else if (goTherePlease < steps) { // если количество заданых шагов меньше количества пройденых шагов
      if (digitalRead(homeSwitch)) { // чтение состояния управляющего контакта и проверка условия наличия логической единицы
        rotationCounter--; // уменьшение значения переменной на 1
      }
      Serial.println("- direction " + (String)!dir); // вывод информации
      // digitalWrite(dirPin, LOW);
      digitalWrite(dirPin, !dir); // установка направления вращения вала с инверсией значения
      steps--; // уменьшение значения переменной на 1
    }
    else if (goTherePlease > steps) { // если количество заданых шагов больше количества пройденых шагов
      if (digitalRead(homeSwitch)) { // чтение состояния управляющего контакта и проверка условия наличия логической единицы
        rotationCounter++; // увеличение значения переменной на 1
      }
      Serial.println("+ direction " + (String)dir); // вывод информации
      // digitalWrite(dirPin, HIGH);
      digitalWrite(dirPin, dir); // установка направления вращения вала
      steps++; // увеличение значения переменной на 1
    }
    
    digitalWrite(stepPin, HIGH); // включение ШД
    delayMicroseconds(currentDelay); // задержка в микросекундах
    digitalWrite(stepPin, LOW); // выключение ШД
    delayMicroseconds(currentDelay); // задержка в микросекундах
    
    Serial.println("Circles done " + (String)rotationCounter); // вывод информации
    Serial.println("step " + (String)steps); // вывод информации
    break; // выход из бесконечного цикла
  }
  return true; // возвращение результата выполнения функции
}

// функция возвращения системы в "домашнее" (нулевое) положение
void goHome() { 
  Serial.println("System is trying to go home."); // вывод информации
  while (1) { // бесконечный цикл
    while (!digitalRead(homeSwitch)) { // чтение состояния управляющего контакта и проверка условия отсутствия логической единицы
      performAStep(-65535); // вызов функции с указанием числового значения в качестве параметра
    }
    Serial.println("system is in home " + (String)digitalRead(homeSwitch)); // вывод информации
    Serial.println("current delay set to " + (String)currentDelay + " in microseconds"); // вывод информации о времени задержки
    Serial.println("set step is " + (String)steps2go); // вывод информации о целевом значении количества шагов
    Serial.println("steps " + (String)steps); // вывод информации текущем положении вала ШД
    steps = 0; // установка значения переменной
    rotationCounter = 0; // установка значения переменной
    break; // выход из бесконечного цикла
  }
  steps2go = 3; // установка значения переменной
}

// функция задачи начальных параметров системы при загрузке микроконтроллера
void setup() { 
  Serial.begin(115200); // установка скорости обмена данными с Serial
  pinMode(ena, OUTPUT); // режима работы управляющего контакта ena на вывод данных
  pinMode(stepPin, OUTPUT); // режима работы управляющего контакта stepPin на вывод данных
  pinMode(dirPin, OUTPUT); // режима работы управляющего контакта dirPin на вывод данных
  pinMode(homeSwitch, INPUT_PULLUP); // управляющего контакта homeSwitch на ввод данных с подтяжкой
  delay(100); // задержка выполнения программы на 100 миллисекунд
  digitalWrite(ena, LOW); // включение драйвера ШД (EN активен низким уровнем)
  digitalWrite(dirPin, dir); // установка направления вращения вала ШД
  
  Serial.println("direction " + (String)dir); // вывод информации о направлении вращения
  Serial.println("current delay set to " + (String)currentDelay + " in microseconds"); // вывод информации о времени задержки
  Serial.println("current angle set to " + (String)steps2go + " in steps"); // вывод информации
  Serial.println(digitalRead(homeSwitch)); // вывод информации о состоянии контакта homeSwitch
  // goHome();
  Serial.println("Setup done!"); // задача начальных параметров системы завершения
}

// циклическая функция
void loop() { 
  while (Serial.available()) // условие при котором Serial доступен для обмена данными
  {
    dataReceived = Serial.readString(); // установка значения переменной в полученное значение из Serial
    dataReceived.trim(); // удаление пробелов из переменной
    int index = dataReceived.indexOf("="); // поиск символа по индексу и запись в соответствующую переменную
    String cmd = dataReceived.substring(0, index); // команда
    String dataToPrint = dataReceived.substring(index + 1, dataReceived.length()); // данные команды
    
    if (cmd == "delay") { // условие, при котором значение переменной соответствует указанной строке
      Serial.println("cmd: " + cmd); // вывод информации
      Serial.println("Delay: " + dataToPrint); // вывод информации
      currentDelay = dataToPrint.toInt(); // установка значение переменной
    }
    else if (cmd == "home") { // условие, при котором значение переменной соответствует указанной строке
      goHome(); // вызов функции без параметров
    }
    else if (cmd == "stop") { // условие, при котором значение переменной соответствует указанной строке
      steps2go = steps; // установка значение переменной
      performAStep(steps); // вызов функции с указанием переменной в качестве параметра
    }
    else { // условие, при котором значение переменной не соответствует ни одной указанной строке
      steps2go = dataReceived.toInt(); // установка значение переменной
      Serial.println("The motor is going to step: " + (String)steps2go); // вывод информации
    }
  }
  
  if (steps2go != steps) { // если количество заданых и пройденых шагов не совпадают
    performAStep(steps2go); // вызов функции с указанием переменной в качестве параметра
  }
} // завершение циклической функции
