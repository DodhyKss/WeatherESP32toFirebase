#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h> 
#include <LiquidCrystal_I2C.h>
#include "DHT.h"
#include <Arduino.h>
#if defined(ESP32)
  #include <WiFi.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
#endif
#include <Firebase_ESP_Client.h>

#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>



#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

LiquidCrystal_I2C lcd(0x27, 16, 2);
WiFiClientSecure client;

//wifi
const char *ssid = "SmartFlood-V1";
const char *password = "SmartFloodV1";

//firebase
#define API_KEY "AIzaSyAWMe2mORCVw-3AjpcfcKWFW1h1BRKLgDg"
#define DATABASE_URL "https://curahhujan-be104-default-rtdb.firebaseio.com/"

//telegram
#define BOTtoken "6926413039:AAGqlcZfjGc---rww7ahwlXwvUHkz7z77YI"
#define CHAT_ID "-1002142533343"

UniversalTelegramBot bot(BOTtoken, client);

FirebaseData fbdo;

FirebaseAuth auth;
FirebaseConfig config;

bool signupOK = false;

#define sensor_ketinggian_air 34
#define DHTPIN 14
#define DHTTYPE DHT22

const int buttonPin1 = 17; // Pin push button 1
const int buttonPin2 = 16; // Pin push button 2

DHT dht(DHTPIN, DHTTYPE);


unsigned long previousMillis = 0;
const long botRequestDelay = 1000;
unsigned long lastTimeBotRan;

volatile bool button1Pressed = false;
volatile bool button2Pressed = false;

int ketinggianAir = 0;
String peringatan = " ";
float h;
float t;
float rain;
const char* description;
float windSpeed;
float pressure;
int menu = 1;
String statusKetinggianAir="";


void setup() {
  Serial.begin(115200);
  lcd.begin();
  dht.begin();
  pinMode(sensor_ketinggian_air, INPUT);
  pinMode(buttonPin1, INPUT_PULLUP);
  pinMode(buttonPin2, INPUT_PULLUP);
  // Turn on the blacklight and print a message.
  lcd.backlight();
  connectToWiFi();

  configTime(0, 0, "pool.ntp.org");
  client.setCACert(TELEGRAM_CERTIFICATE_ROOT);
  
    /* Assign the api key (required) */
  config.api_key = API_KEY;

  /* Assign the RTDB URL (required) */
  config.database_url = DATABASE_URL;

  /* Sign up */
  if (Firebase.signUp(&config, &auth, "", "")){
    Serial.println("ok");
    signupOK = true;
  }
  else{
    Serial.printf("%s\n", config.signer.signupError.message.c_str());
  }

  /* Assign the callback function for the long running token generation task */
  config.token_status_callback = tokenStatusCallback; //see addons/TokenHelper.h
  
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  attachInterrupt(digitalPinToInterrupt(buttonPin1), button1Interrupt, FALLING);
  attachInterrupt(digitalPinToInterrupt(buttonPin2), button2Interrupt, FALLING);

}

void loop() {
    getWeatherData();
    getDhtSensor();
    deteksiStatusBanjir();
    tampilan_menu();
    sendToDbase();
    sendNotification();
    delay(10000); // Ambil data cuaca setiap 5 menit (50000 ms)
}


void tampilan_menu() {
  Serial.println(menu);
  if(menu == 1){
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print(description);
    lcd.setCursor(0,1);
    lcd.print("Status: ");
    lcd.print(peringatan);
  }else if(menu == 2){
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("TU: ");
    lcd.print(pressure);
    lcd.print(" hPa");
    lcd.setCursor(0,1);
    lcd.print("WS: ");
    lcd.print(windSpeed);
    lcd.print(" m/s");
  }
}

// Fungsi interupsi untuk tombol 1
void button1Interrupt() {
  menu = 1;
}

// Fungsi interupsi untuk tombol 2
void button2Interrupt() {
  menu = 2;
}


void sendNotification() {
  if (millis() > lastTimeBotRan + botRequestDelay) {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    while (numNewMessages) {
      Serial.println("got response");
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }
    lastTimeBotRan = millis();
  }
}


void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = String(bot.messages[i].chat_id);
    if (chat_id != CHAT_ID) {
      bot.sendMessage(chat_id, "Unauthorized user", "");
      continue;
    }

    String text = bot.messages[i].text;
    Serial.println(text);

    String from_name = bot.messages[i].from_name;

    if (text == "/start") {
      String welcome = "Welcome, " + from_name + ",\n";
      welcome += "Gunakan perintah berikut untuk mendapatkan pesan\n";
      welcome += "/get\n";
      welcome += "/quit";
      bot.sendMessage(chat_id, welcome, "");
    }

    if (text == "/get") {
      bot.sendMessage(chat_id, "Notifikasi ON", "");
      bot.sendMessage(chat_id, "Status Banjir: "+peringatan, "");
      bot.sendMessage(chat_id, "Ketinggian Air: "+statusKetinggianAir, "");
      while (1) {
        unsigned long currentMillis = millis();
        if (currentMillis - previousMillis > 3600000) {
          bot.sendMessage(chat_id, "Status Banjir: "+peringatan, "");
          bot.sendMessage(chat_id, "Ketinggian Air: "+statusKetinggianAir, "");

          previousMillis = currentMillis;
        }
        bot.getUpdates(bot.last_message_received + 1);
        if (bot.messages[i].text == "/quit") {
          Serial.println("Quit dari LOOP");
          bot.sendMessage(chat_id, "Notifikasi OFF", "");
          break;
        }
        getWeatherData();
        getDhtSensor();
        deteksiStatusBanjir();
        tampilan_menu();
        sendToDbase();
        delay(5000);
      }
    }
  }
}


void deteksiStatusBanjir() {
  if(pressure < 980 && rain > 0 && h > 70 && t < 28 && description == "moderate rain" || description == "thunderstorm with rain"){
    peringatan = "Waspada Banjir";
  }
  else if (pressure < 980 && rain > 0 && ketinggianAir > 500 && h > 70 && t < 28 || description == "heavy intensity rain" || description == "thunderstorm with heavy rain" || description == "moderate rain" || description == "thunderstorm with rain"){
    peringatan = "Bahaya Banjir";
  }
  else {
    peringatan = "Aman";
  }
}


void sendToDbase() {
  if (Firebase.ready() && signupOK){
    if(Firebase.RTDB.setString(&fbdo, "weather_data/status", peringatan)){
      Serial.println("Data status terkirim");
    }
    if(Firebase.RTDB.setString(&fbdo, "weather_data/kelembaban", String(h))){
      Serial.println("Data kelembaban terkirim");
    }
    if(Firebase.RTDB.setString(&fbdo, "weather_data/suhu", String(t))){
      Serial.println("Data suhu terkirim");
    }
  }
}

void getDhtSensor() {
  ketinggianAir = analogRead(sensor_ketinggian_air);
  if(ketinggianAir > 100){
    statusKetinggianAir = "1 Meter";
  }else {
    statusKetinggianAir = "0 Meter";
  }
  Serial.println(ketinggianAir);
  h = dht.readHumidity();
  // Read temperature as Celsius (the default)
  t = dht.readTemperature();

  Serial.print(F("Humidity: "));
  Serial.print(h);
  Serial.print(F("%  Temperature: "));
  Serial.print(t);
  Serial.println(F("°C "));
}


void connectToWiFi() {
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting...");
    lcd.setCursor(0,0);
    lcd.print("Connecting...");
  }
  Serial.println("Connected to WiFi");
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("WiFi Connected!");
}

void getWeatherData() {
  Serial.println("Getting weather data...");
  HTTPClient http;
  http.begin("http://api.openweathermap.org/data/2.5/weather?q=Makassar&appid=43dd03f41055fb674d81425bff0de0b1"); // Ganti "Jakarta" dengan kota yang diinginkan dan "API_KEY" dengan kunci API Anda
  int httpResponseCode = http.GET();
  
  if (httpResponseCode > 0) {
    String payload = http.getString();
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, payload);
    const char* city = doc["name"];
//    float temperature = doc["main"]["temp"];
//    float humidity = doc["main"]["humidity"];
    pressure = doc["main"]["pressure"];
    rain = doc["rain"]["1h"]; // Mendapatkan curah hujan dalam satu jam
    description = doc["weather"][0]["description"]; // Deskripsi cuaca
    windSpeed = doc["wind"]["speed"]; // Kecepatan angin
    Serial.printf("Weather in %s:\n", city);
    Serial.printf("Description: %s\n", description);
//    Serial.printf("Temperature: %.2f°C\n", temperature - 273.15); // Konversi Kelvin ke Celsius
//    Serial.printf("Humidity: %.2f%%\n", humidity);
    Serial.printf("Pressure: %.2f hPa\n", pressure); //tekanan udara
    Serial.printf("Rain (1h): %.2f mm\n", rain); // Menampilkan curah hujan dalam satu jam
    Serial.printf("Wind Speed: %.2f m/s\n", windSpeed); // Menampilkan kecepatan angin dalam meter per detik
  } else {
    Serial.print("Error in HTTP request: ");
    Serial.println(httpResponseCode);
  }
  http.end();
}
