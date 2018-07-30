
// *****************************
// *
// *   CHOCADEIRA INTELIGENTE
// *
// *****************************
//
// Controla a temperatura da chocadeira, por meio da verificação pelo sensor DHT12 e ligagem e desligagem do aquecedor (lâmpada encandecente)
// por meio de RELÉ. Verifica, também, a umidade da chocadeira (pelo sensor DHT12) e avisa inadequação por meio de bipes num buzzer.
// Envia seu estado (temperatura e umidade) pela internet, por meio de um broker MQTT, para o aplicativo específico. Recebe suas configurações
// também pela internet, enviadas pelo aplicativo.
//
// PINOS (MÓDULO / NODEMCU):
// * DISPLAY:
// Vcc / 5volts
// Gnd / Gnd
// SDA / D2
// SCL / D1
//
// * DHT12:
// 01 / 3.3volts
// 02 / D2
// 03 / Gnd
// 04 / D1
//
// * BUZZER:
// Vcc / D3
// Gnd / Gnd
//
// * RELÉ:
// In  / D0
// Gnd / Gnd
// Vcc / Vin



// Bibliotecas
#include "Arduino.h"
#include <DHT12.h>
#include <Wire.h> 
#include <LiquidCrystal_I2C.h>
#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include "WiFiManager.h"          //https://github.com/tzapu/WiFiManager
#include <PubSubClient.h>
#include <FS.h>
#include <ArduinoJson.h>

// Definir constantes
#define RELAY D0

#define BUZZER D3
#define BUZZER_OUT 17
#define BUZZER_TIME 200

#define DISPLAY_ROUTINE 45000
#define DISPLAY_TIME 10000
#define DELAY_ALL 1000

#define SSID_THIS "Chocadeira"
#define PW_THIS   "12345678"
#define MINIMUM_SIGNAL 30

#define MQTT_ADDRESS   "broker.hivemq.com"
#define MQTT_PORT      1883
#define MQTT_TOPIC_IN  "davimagales/chocadeira/in"
#define MQTT_TOPIC_OUT "davimagales/chocadeira/out"

#define FILE_SETTINGS          "/settings.txt"
#define DEFAULT_TEMPERATUREMAX 37.8
#define DEFAULT_TEMPERATUREMIN 37.4
#define DEFAULT_HUMIDITYMAX 70
#define DEFAULT_HUMIDITYMIN 60

// Estruturas para os dados
struct SettingStructure {
  float temperatureMax;
  float temperatureMin;
  float humidityMax;
  float humidityMin;
  bool  scrolling;
};
union Data {
  SettingStructure settings;
  byte bytes[17];
};



// Dados globais
Data data;
DHT12 dht12;
LiquidCrystal_I2C lcd(0x27, 20, 4);
WiFiClient espClient;
PubSubClient mqtt(espClient);

bool displaySettingsShowing = false;
unsigned long displayRoutineTime = millis();

float temperature;
float humidity;



// ###################################################################### INTERNET / MQTT

// Criar ponto de acesso / Conexão prévia falhou
void configModeCallback(WiFiManager *myWiFiManager) {
  Serial.println("WifiManager entered config mode");
  Serial.println(WiFi.softAPIP());
  Serial.println(myWiFiManager->getConfigPortalSSID());
}

// Conectado com sucesso
void saveConfigCallback () {
  Serial.println("WifiManager should save config");
}

// Conectar a internet e ao servidor MQTT
void connectServer() {
  WiFiManager wifiManager;
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.setMinimumSignalQuality(MINIMUM_SIGNAL);
  wifiManager.setAPStaticIPConfig(IPAddress(10,0,1,1), IPAddress(10,0,1,1), IPAddress(255,255,255,0)); // Endereço: http://10.0.1.1

  if(!wifiManager.autoConnect(SSID_THIS, PW_THIS)) {
    Serial.println("WifiManager failed to connect and hit timeout");
    ESP.reset();
    delay(1000);
  }
  Serial.println("Connected WIFI");

  mqtt.setServer(MQTT_ADDRESS, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
}

// Callback MQTT
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String received = "";
  for(int i=0; i<length; i++)                              // Converter para string
    received += (char) payload[i];
  
  Serial.print("MQTT message received [");                 // Debbuging
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(received);

  DynamicJsonBuffer jsonBuffer;                            // Converter JSON para dados utilizáveis
  JsonObject& root = jsonBuffer.parseObject(received);

  float temperatureMax = root[String("temperatureMax")];   // Verificar consistência do JSON
  float temperatureMin = root[String("temperatureMin")];
  float humidityMax    = root[String("humidityMax")];
  float humidityMin    = root[String("humidityMin")];
  data.settings.temperatureMax = temperatureMax != 0.0f ? temperatureMax : DEFAULT_TEMPERATUREMAX;
  data.settings.temperatureMin = temperatureMin != 0.0f ? temperatureMin : DEFAULT_TEMPERATUREMIN;
  data.settings.humidityMax    = humidityMax != 0.0f ? humidityMax : DEFAULT_HUMIDITYMAX;
  data.settings.humidityMin    = humidityMin != 0.0f ? humidityMin : DEFAULT_HUMIDITYMIN;

  if(temperatureMax == 0.0f || temperatureMin == 0.0f || humidityMax == 0.0f || humidityMin == 0.0f) {
    root[String("temperatureMax")] = data.settings.temperatureMax;
    root[String("temperatureMin")] = data.settings.temperatureMin;
    root[String("humidityMax")]    = data.settings.humidityMax;
    root[String("humidityMin")]    = data.settings.humidityMin;
    root.printTo(received);
  }

  SPIFFS.begin();                                          // Salvar na memória
  File file;
  file = SPIFFS.open(FILE_SETTINGS, "r+");
  file.print(received +'\n');
  file.close();
  SPIFFS.end();

  buzzer();                                                // Notificar
}

// Reconectar ao servidor MQTT
void mqttReconnect() {
  String clientId = WiFi.softAPmacAddress();

  while(!mqtt.connected()) {
    Serial.print("Attempting MQTT connection... ");
    
    if (mqtt.connect(clientId.c_str())) {
      Serial.println("MQTT connected");

      mqtt.subscribe(MQTT_TOPIC_IN);
      Serial.println("MQTT subscribe topic in");
    } else {
      Serial.print("MQTT failed, rc=");
      Serial.print(mqtt.state());
      Serial.println("MQTT try again in 5 seconds");
      delay(5000);
    }
  }
}

// Rotina MQTT de verificação da conexão, recebimento de mensagens e envio do estado atual para o aplicativo móvel
void mqttRoutine() {
  if(!mqtt.connected()) {
    mqttReconnect();
  }
  mqtt.loop();

  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();
  root["temperature"]    = temperature;
  root["humidity"]       = humidity;
  root["temperatureMax"] = data.settings.temperatureMax;
  root["temperatureMin"] = data.settings.temperatureMin;
  root["humidityMax"]    = data.settings.humidityMax;
  root["humidityMin"]    = data.settings.humidityMin;
  root["heating"]        = (bool) digitalRead(RELAY);

  String msg;
  root.printTo(msg);

  Serial.print("MQTT publish [");
  Serial.print(MQTT_TOPIC_OUT);
  Serial.print("]: ");
  Serial.println(msg);
  mqtt.publish(MQTT_TOPIC_OUT, msg.c_str());
}

// ###################################################################### DISPLAY

// Iniciar o display
void displayStart() {
  lcd.init();
  lcd.backlight();
}

// Configuração base do display
void displayBase() {
  lcd.setCursor(0, 0);
  lcd.print("     CHOCADEIRA     ");
  lcd.setCursor(0, 1);
  lcd.print("     AUTOMATICA     ");
  lcd.setCursor(0, 2);
  lcd.print("Temp.:");
  lcd.setCursor(6, 2);
  lcd.print("             C");
  lcd.setCursor(0, 3);
  lcd.print("Umid.:");
  lcd.setCursor(6, 3);
  lcd.print("             %");
}

// Atualizar display com a situação
void displayStatus() {
  displayBase();
  
  lcd.setCursor(14, 2);
  lcd.print(String(temperature, 1));
  lcd.setCursor(14, 3);
  lcd.print(String(humidity, 1));
}

// Exibir configurações atuais
void displaySettings() {
  displayBase();

  lcd.setCursor(12, 2);
  lcd.print("-");
  lcd.setCursor(12, 3);
  lcd.print("-");
      
  lcd.setCursor(7, 2);
  lcd.print(String(data.settings.temperatureMin, 1));
  lcd.setCursor(14, 2);
  lcd.print(String(data.settings.temperatureMax, 1));
  lcd.setCursor(7, 3);
  lcd.print(String(data.settings.humidityMin, 1));
  lcd.setCursor(14, 3);
  lcd.print(String(data.settings.humidityMax, 1));
}

// Rotina de verificação da exibição das configurações atuais
// Atualizar a cada DISPLAY_ROUTINE a flag do display para exibir as configurações pelo tempo DISPLAY_TIME
void displayRoutine() {
  unsigned long currentTime = millis();
  
  if(currentTime >= displayRoutineTime + DISPLAY_ROUTINE) {   
    if(currentTime <= displayRoutineTime + DISPLAY_ROUTINE + DISPLAY_TIME) {
      displaySettingsShowing = true;
    } else {
      displaySettingsShowing = false;
      displayRoutineTime = currentTime;
    }
  }

  if(displaySettingsShowing)
    displaySettings();
  else
    displayStatus();
}

// ###################################################################### CHOCADEIRA

// Ler as configurações prévias salvas na memória
void settingsStart() {
  String settings;
  
  SPIFFS.begin();
  File file;
  if(!SPIFFS.exists(FILE_SETTINGS)) {
    file = SPIFFS.open(FILE_SETTINGS, "w+");
    file.print('\n');
    file.close();
  }
  file = SPIFFS.open(FILE_SETTINGS, "r+");
  settings = file.readStringUntil('\n');
  file.close();
  SPIFFS.end();

  Serial.print("Readed settings: ");
  Serial.println(settings.length() > 0 ? settings : "DEFAULT");

  DynamicJsonBuffer jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject(settings);

  float temperatureMax = root[String("temperatureMax")];   // Verifica consistência do JSON
  float temperatureMin = root[String("temperatureMin")];
  float humidityMax    = root[String("humidityMax")];
  float humidityMin    = root[String("humidityMin")];
  data.settings.temperatureMax = temperatureMax != 0.0f ? temperatureMax : DEFAULT_TEMPERATUREMAX;
  data.settings.temperatureMin = temperatureMin != 0.0f ? temperatureMin : DEFAULT_TEMPERATUREMIN;
  data.settings.humidityMax    = humidityMax != 0.0f ? humidityMax : DEFAULT_HUMIDITYMAX;
  data.settings.humidityMin    = humidityMin != 0.0f ? humidityMin : DEFAULT_HUMIDITYMIN;
}

// Definir lâmpada/aquecedor para ligada ou desligar para aquecer ou parar de aquecer, respectivamente
void setHeater(bool s) {
  digitalWrite(RELAY, s);
}

// Buzzer
void buzzer() {
  analogWrite(BUZZER, BUZZER_OUT);
  delay(BUZZER_TIME);
  analogWrite(BUZZER, LOW);
}

// Rotina de verificação da TEMPERATURA
// Pega a temperatura e verifica se está conforme a configuração, dependendo do caso define o aquecedor
void temperatureRoutine() {
  temperature = dht12.readTemperature();

  if(!isnan(temperature) && !isnan(humidity)) {
    if(temperature >= data.settings.temperatureMax) {
      setHeater(false);
    } else
    if(temperature <= data.settings.temperatureMin) {
      setHeater(true);
    }
  }
}

// Rotina de verificação da HUMIDADE
// Pega a umidade e avisa, por meio de bipes num buzzer, caso careça de atenção
void humidityRoutine() {
  humidity = dht12.readHumidity();
  
  if(humidity > data.settings.humidityMax || humidity < data.settings.humidityMin)
    buzzer();
}



// ###################################################################### INÍCIO
// Definir os pinos; definir configurações e iniciar display
void setup() {
  Serial.begin(115200);
  Serial.println("\n...STARTING...");
  
  pinMode(RELAY, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  digitalWrite(RELAY, LOW);
  digitalWrite(BUZZER, LOW);

  displayStart();
  displayBase();

  settingsStart();
  connectServer();
}

// ###################################################################### ROTINAS
void loop() {
  temperatureRoutine();
  
  humidityRoutine();
  
  displayRoutine();
  
  mqttRoutine();
  
  delay(DELAY_ALL);
}

