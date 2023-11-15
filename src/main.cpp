#include <Arduino.h>
#include <WiFiMulti.h>
#include <Keypad.h>
#include <HTTPClient.h>
#include <ezBuzzer.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>

using namespace std;

#define WIFI_SSID "secret"
#define WIFI_PASSWORD "secret"

#define PIN_1 13
#define PIN_2 12
#define PIN_3 14
#define PIN_4 27
#define PIN_5 26
#define PIN_6 25
#define PIN_7 33
#define BUZZER_PIN 15

void connect_wifi();

// consts
const long interval = 500; // ms
const long clear_interval = 20000; // ms

// globals
String buffer = "";
String room = "";
String room_candidate = "";
String payload = "";
int response_code = -1;
bool lcd_changed = true;
bool please_wait = false;
unsigned long previousMillis = 0;
unsigned long last_button_press_millis = 0;

// http client
HTTPClient http;

// endpoints
const String BASE_URL = "https://bramka/api";
const String CALL_SUFFIX = "/call";
const String PIN_SUFFIX = "/lock/pin";
const String PEEP_SUFFIX = "/audio/play/peep";

// keypad
const byte ROWS = 4; 
const byte COLS = 3;
char keys2[ROWS][COLS] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'},
  {'*','0','#'}
};
byte rowPins[ROWS] = {PIN_7, PIN_6, PIN_5, PIN_4}; 
byte colPins[COLS] = {PIN_1, PIN_2, PIN_3};

Keypad keypad = Keypad(makeKeymap(keys2), rowPins, colPins, ROWS, COLS);

// wifi
WiFiMulti wifiMulti;

// buzzer
ezBuzzer buzzer(BUZZER_PIN); 

// LCD
LiquidCrystal_I2C lcd(0x27, 16, 2);

// fun defs
int call(String room, bool ongoing);
void pin_open(String pin);
void handle_keys(char key);
void print_lcd(unsigned long currentMillis);

// # SETUP 
void setup()
{
  Serial.begin(921600);
  pinMode(LED_BUILTIN, OUTPUT);

  pinMode(PIN_1, INPUT);
  pinMode(PIN_2, INPUT);
  pinMode(PIN_3, INPUT);
  pinMode(PIN_4, INPUT);
  pinMode(PIN_5, INPUT);
  pinMode(PIN_6, INPUT);
  pinMode(PIN_7, INPUT);

  digitalWrite(LED_BUILTIN, WiFi.status() == WL_CONNECTED);

  keypad.setDebounceTime(50); // ms
  
  connect_wifi();

  lcd.init();
  lcd.backlight();

  last_button_press_millis = millis();
}

// # LOOP
void loop() {
  buzzer.loop();
  digitalWrite(LED_BUILTIN, WiFi.status() == WL_CONNECTED); // blue diode on wifi connected

  char key = keypad.getKey();
  if (key != NO_KEY){
    buzzer.beep(100);
    handle_keys(key);
  }
  unsigned long current_millis = millis();
  print_lcd(current_millis);

  if(current_millis - last_button_press_millis >= clear_interval) {
    buffer = "";
    last_button_press_millis = current_millis;
  } 
}

void print_lcd(unsigned long currentMillis) {
  const String LCD_INSTRUCTIONS_ROW_CALL = "[#]DZWON[*]KASUJ"; 
  const String LCD_INSTRUCTIONS_ROW_PIN = "[#]PIN  [*]KASUJ"; 
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    int len = buffer.length();

    // upper row
    lcd.setCursor(0, 0);
    if(please_wait) {
      lcd.print(": PROSZE CZEKAC");
    } else if(buffer != "") {
      String lcd_line = ": ";
      int spaces_len = 12 - len;
      lcd_line += buffer;

      for (int i = 0; i < spaces_len; ++i) {
        lcd_line += ' ';
      }
      lcd.print(lcd_line);
    } else {
      lcd.print(": WPISZ KOD    ");
    }

    // lower row
    lcd.setCursor(0, 1);
    if(len < 4) {
      lcd.print(LCD_INSTRUCTIONS_ROW_CALL);
    } else {
      lcd.print(LCD_INSTRUCTIONS_ROW_PIN);
    }
  } 
}

void handle_keys(char key) {
  int buffer_len = buffer.length();
  last_button_press_millis = millis();
  if(key == '#' && !please_wait) {
      if(buffer_len < 4) {
        Serial.print("Calling room: ");
        Serial.println(buffer.c_str());

        room_candidate = buffer;
        buffer = "";
  
        call(room_candidate, true);
      } else {
        Serial.println(buffer.c_str());
        String pin = buffer;

        buffer = "";

        pin_open(pin);
      }
    } else if(key == '*') {
      Serial.print("Deleting buffer: ");
      Serial.println(buffer.c_str());

      buffer = "";

      if(room != "") {
        Serial.print("Cancelling call for a room: ");
        Serial.println(room.c_str());

        while(please_wait) {
          delay(100);
        }
        call(room, false);

        room = "";
        room_candidate = "";
      }
    } else { 
      buffer += key;
    }
}

void connect_wifi() {
  wifiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);

  while (wifiMulti.run() != WL_CONNECTED) {
    delay(1000);
  }
  Serial.println("Connected");
}

void postTask(void* parameter) {
  please_wait = true;
  int rc = -1;
  do {
    rc = http.POST(payload.c_str());
  } while (rc < 100);
  response_code = rc;
  Serial.print("Response code: ");
  Serial.println(response_code);

  if(response_code == 201) {
    room = room_candidate;
  }

  please_wait = false;
  vTaskDelete(NULL);
}

int call(String room, bool ongoing) {
  if(WiFi.status()== WL_CONNECTED){
    String url = BASE_URL + CALL_SUFFIX;
    http.begin(url.c_str());
    payload = "{\"room\": " + room + ",\"call_status\": \"" + (ongoing ? "NOT_ANSWERED" : "INACTIVE") +"\"}";
    http.addHeader("Content-Type", "application/json");
    xTaskCreatePinnedToCore(postTask, "PostTask", 10000, NULL, 1, NULL, 0);
  }
  return -1;
}

void pin_open(String pin) {
  if(WiFi.status()== WL_CONNECTED){
    String url = BASE_URL + PIN_SUFFIX;
    http.begin(url.c_str());
    payload = "{\"pin\": " + pin + "}";
    http.addHeader("Content-Type", "application/json");
    xTaskCreatePinnedToCore(postTask, "PostTask", 10000, NULL, 1, NULL, 0);
  }
}