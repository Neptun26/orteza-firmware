#include <Wire.h>
#include <Adafruit_NeoPixel.h> 
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <SparkFun_BNO080_Arduino_Library.h> 

#define SDA_I2C 19
#define SCL_I2C 20
#define MUX_ADDRESS 0x70

#define LED_STRIP_PIN  7  
#define NUM_LEDS       60 
#define BUZZER_PIN     2  
#define RGB_LED_PIN    4  

#define CANAL_BNO4          0  // Senzor Superior (C7-T1)
#define CANAL_BNO6          2  // Senzor Median (T7)
#define CANAL_BNO7          3  // Senzor Inferior (L3)

const float ALPHA = 0.15; 

struct DateSenzor {
  int16_t pitch; 
  int16_t roll;  
};

Adafruit_NeoPixel banda(NUM_LEDS, LED_STRIP_PIN, NEO_GRB + NEO_KHZ800);

BNO080 mpu4;
BNO080 mpu6;
BNO080 mpu7;

// --- CONFIGURARE BLE ---
BLEServer *pServer = NULL;
BLECharacteristic *pTxCharacteristic;
bool dispozitivConectat = false;

#define SERVICE_UUID           "0000ffe0-0000-1000-8000-00805f9b34fb"
#define TX_CHARACTERISTIC_UUID "0000ffe1-0000-1000-8000-00805f9b34fb"

class ServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      dispozitivConectat = true;
    };
    void onDisconnect(BLEServer* pServer) {
      dispozitivConectat = false;
      delay(500);
      pServer->startAdvertising(); 
    }
};

void bluetoothPrintln(String text) {
  Serial.println(text); 
  if (dispozitivConectat) {
    text += "\n"; 
    pTxCharacteristic->setValue(text.c_str());
    pTxCharacteristic->notify();
    delay(10); 
  }
}

DateSenzor S4 = {0, 0}, S6 = {0, 0}, S7 = {0, 0};
DateSenzor posturaIdeala4 = {0, 0}, posturaIdeala6 = {0, 0}, posturaIdeala7 = {0, 0}; 

unsigned long timpPosturaIncorecta = 0;
const int TIMP_DECLANSARE_MS = 4000; 

unsigned long timpPornireBeepScurt = 0;
bool beepScurtInDesfasurare = false;

int16_t diferentaUnghiularaNormalizata(int16_t unghiA, int16_t unghiB) {
  int16_t diferenta = unghiA - unghiB;
  while (diferenta < -180) diferenta += 360;
  while (diferenta > 180)  diferenta -= 360;
  return diferenta;
}

void selectMux(uint8_t channel) {
  if (channel > 7) return;
  Wire.beginTransmission(MUX_ADDRESS);
  Wire.write(1 << channel);
  Wire.endTransmission();
  delay(1); 
}

bool initBNOOnChannel(BNO080 &senzor, uint8_t canal) {
  selectMux(canal);
  delay(10);
  
  if (senzor.begin(0x4B, Wire) == false) {
    char eroare[50];
    sprintf(eroare, "[HARDWARE] Senzor %d offline!", canal);
    bluetoothPrintln(eroare);
    return false;
  }
  
  senzor.enableRotationVector(20); 
  return true;
}

bool citesteDateBNO(uint8_t canal, BNO080 &senzor, DateSenzor &dateIesire) {
  selectMux(canal); 
  
  if (senzor.dataAvailable() == true) {
    float rollRad  = senzor.getRoll();
    float pitchRad = senzor.getPitch();
    
    dateIesire.roll  = (int16_t)(rollRad * 57.29577f);
    dateIesire.pitch = (int16_t)(pitchRad * 57.29577f);
    return true;
  }
  return false; 
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  BLEDevice::init("Orteza_Smart_BNO"); 
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);
  pTxCharacteristic = pService->createCharacteristic(
                        TX_CHARACTERISTIC_UUID, 
                        BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
                      );
  pTxCharacteristic->addDescriptor(new BLE2902());

  pService->start();
  pServer->getAdvertising()->start();
  
  Serial.println("\n=== MONITORIZARE ORTEZA PRODUCTION V20.0 ===");

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RGB_LED_PIN, OUTPUT);

  banda.begin();
  banda.setBrightness(25);
  banda.clear(); banda.show();

  for (int i = 0; i < 5; i++) banda.setPixelColor(i, banda.Color(100, 50, 0));
  banda.show();

  while (!dispozitivConectat) {
    delay(200); 
  }

  delay(1000); 
  bluetoothPrintln("\n[BLE] Conectat! Configurare I2C...");

  Wire.begin(SDA_I2C, SCL_I2C);
  Wire.setClock(100000);
  delay(500);

  initBNOOnChannel(mpu4, CANAL_BNO4);
  initBNOOnChannel(mpu6, CANAL_BNO6);
  initBNOOnChannel(mpu7, CANAL_BNO7);
  
  bluetoothPrintln("\n[TEST] Pregatire calibrare. Stati drept...");
  
  unsigned long startTranzitie = millis();
  const unsigned long DURATA_TEST_MS = 5000; 
  unsigned long ultimulUpdateSecunde = 0;

  while (millis() - startTranzitie < DURATA_TEST_MS) {
    unsigned long timpScurs = millis() - startTranzitie;
    unsigned long timpRamasSecunde = (DURATA_TEST_MS - timpScurs) / 1000;
    
    if (timpRamasSecunde != ultimulUpdateSecunde) {
      ultimulUpdateSecunde = timpRamasSecunde;
      
      char msg[60];
      sprintf(msg, "[TEST] Calibrare in: %lu secunde...", timpRamasSecunde + 1);
      bluetoothPrintln(msg);
      
      int leduriActive = map(timpScurs, 0, DURATA_TEST_MS, NUM_LEDS, 0);
      banda.clear();
      for (int i = 0; i < leduriActive; i++) {
        banda.setPixelColor(i, banda.Color(0, 0, 150)); 
      }
      banda.show();
      
      digitalWrite(BUZZER_PIN, HIGH); delay(40); digitalWrite(BUZZER_PIN, LOW);
    }
    delay(50); 
  }

  digitalWrite(BUZZER_PIN, HIGH); delay(600); digitalWrite(BUZZER_PIN, LOW); 
  bluetoothPrintln("\n[FAZA 2] Colectez amprenta geometrica...");
  
  for(int c=0; c<3; c++) {
    for (int i = 0; i < NUM_LEDS; i++) banda.setPixelColor(i, banda.Color(150, 0, 150)); banda.show(); delay(200);
    banda.clear(); banda.show(); delay(200);
  }

  for (int i = 0; i < NUM_LEDS; i++) banda.setPixelColor(i, banda.Color(0, 150, 200)); 
  banda.show(); 

  // --- IMPLEMENTARE FAZĂ CALIBRARE ROBUSTĂ ---
  int32_t s4P = 0, s4R = 0, s6P = 0, s6R = 0, s7P = 0, s7R = 0;
  int probe4 = 0, probe6 = 0, probe7 = 0;
  unsigned long start = millis();
  
  while (millis() - start < 4000) { 
    DateSenzor r4, r6, r7;
    
    if (citesteDateBNO(CANAL_BNO4, mpu4, r4)) { s4P += r4.pitch; s4R += r4.roll; probe4++; }
    delay(5);
    if (citesteDateBNO(CANAL_BNO6, mpu6, r6)) { s6P += r6.pitch; s6R += r6.roll; probe6++; }
    delay(5);
    if (citesteDateBNO(CANAL_BNO7, mpu7, r7)) { s7P += r7.pitch; s7R += r7.roll; probe7++; }
    delay(5);
  }

  if (probe4 > 0) { posturaIdeala4.pitch = s4P / probe4; posturaIdeala4.roll = s4R / probe4; }
  if (probe6 > 0) { posturaIdeala6.pitch = s6P / probe6; posturaIdeala6.roll = s6R / probe6; }
  if (probe7 > 0) { posturaIdeala7.pitch = s7P / probe7; posturaIdeala7.roll = s7R / probe7; }

  S4 = posturaIdeala4;
  S6 = posturaIdeala6;
  S7 = posturaIdeala7;

  for(int i=0; i<3; i++) { digitalWrite(BUZZER_PIN, HIGH); delay(70); digitalWrite(BUZZER_PIN, LOW); delay(70); }
  banda.clear(); banda.show();
  digitalWrite(RGB_LED_PIN, HIGH);
  bluetoothPrintln("\n[MONITORIZARE] Sistem optimizat operational!");
}

void loop() {
  DateSenzor raw4, raw6, raw7;

  if (citesteDateBNO(CANAL_BNO4, mpu4, raw4)) {
    S4.pitch = (int16_t)((S4.pitch * (1.0 - ALPHA)) + (raw4.pitch * ALPHA));
    S4.roll  = (int16_t)((S4.roll * (1.0 - ALPHA)) + (raw4.roll * ALPHA));
  }
  delay(10); 

  if (citesteDateBNO(CANAL_BNO6, mpu6, raw6)) {
    S6.pitch = (int16_t)((S6.pitch * (1.0 - ALPHA)) + (raw6.pitch * ALPHA));
    S6.roll  = (int16_t)((S6.roll * (1.0 - ALPHA)) + (raw6.roll * ALPHA));
  }
  delay(10);

  if (citesteDateBNO(CANAL_BNO7, mpu7, raw7)) {
    S7.pitch = (int16_t)((S7.pitch * (1.0 - ALPHA)) + (raw7.pitch * ALPHA));
    S7.roll  = (int16_t)((S7.roll * (1.0 - ALPHA)) + (raw7.roll * ALPHA));
  }
  delay(10);

  int16_t angulatieSagitalaSupIdeala = diferentaUnghiularaNormalizata(posturaIdeala4.pitch, posturaIdeala6.pitch);
  int16_t angulatieSagitalaInfIdeala = diferentaUnghiularaNormalizata(posturaIdeala6.pitch, posturaIdeala7.pitch);
  int16_t angulatieFrontalaIdeala    = diferentaUnghiularaNormalizata(posturaIdeala4.roll, posturaIdeala6.roll); 

  int16_t angulatieSagitalaSupCurenta = diferentaUnghiularaNormalizata(S4.pitch, S6.pitch);
  int16_t angulatieSagitalaInfCurenta = diferentaUnghiularaNormalizata(S6.pitch, S7.pitch);
  int16_t angulatieFrontalaCurenta    = diferentaUnghiularaNormalizata(S4.roll, S6.roll);

  int16_t deviatieSagitalaSup = abs(diferentaUnghiularaNormalizata(angulatieSagitalaSupCurenta, angulatieSagitalaSupIdeala));
  int16_t deviatieSagitalaInf = abs(diferentaUnghiularaNormalizata(angulatieSagitalaInfCurenta, angulatieSagitalaInfIdeala));
  int16_t deviatieFrontala    = abs(diferentaUnghiularaNormalizata(angulatieFrontalaCurenta, angulatieFrontalaIdeala));

  bool pozitieIncorecta = false;
  
  const int PRAG_SAGITAL_SUP = 5;     
  const int PRAG_SAGITAL_INF = 5; 
  const int PRAG_FRONTAL     = 5; 

  if (deviatieSagitalaSup > PRAG_SAGITAL_SUP || deviatieSagitalaInf > PRAG_SAGITAL_INF || deviatieFrontala > PRAG_FRONTAL) {
    pozitieIncorecta = true;
  }

  if (pozitieIncorecta) {
    if (timpPosturaIncorecta == 0) {
      timpPosturaIncorecta = millis();
      digitalWrite(BUZZER_PIN, HIGH);
      timpPornireBeepScurt = millis();
      beepScurtInDesfasurare = true;
    }
    
    banda.clear();
    for (int i = 0; i < NUM_LEDS; i++) banda.setPixelColor(i, banda.Color(255, 0, 0)); 
    banda.show();
    
    if (millis() - timpPosturaIncorecta > TIMP_DECLANSARE_MS) {
      digitalWrite(BUZZER_PIN, (millis() / 150) % 2); 
    }
  } else {
    timpPosturaIncorecta = 0;
    if (!beepScurtInDesfasurare) {
      digitalWrite(BUZZER_PIN, LOW);
    }
    
    banda.clear();
    for (int i = 0; i < 15; i++) banda.setPixelColor(i, banda.Color(0, 180, 40)); 
    banda.show();
  }

  if (beepScurtInDesfasurare && (millis() - timpPornireBeepScurt > 60)) {
    digitalWrite(BUZZER_PIN, LOW);
    beepScurtInDesfasurare = false;
  }

  char buff[150];
  sprintf(buff, "DEV -> DevSag_Sup: %d | DevSag_Inf: %d | DevFront_Lat: %d | Status: %s", 
          deviatieSagitalaSup, deviatieSagitalaInf, deviatieFrontala, pozitieIncorecta ? "INCORECT" : "OK");
  Serial.println(buff);

  static unsigned long ultimulMilisBLE = 0;
  if (millis() - ultimulMilisBLE > 500) {
    if (dispozitivConectat) { 
      char buffBLE[60];
      sprintf(buffBLE, "%d,%d,%d,%d", deviatieSagitalaSup, deviatieSagitalaInf, deviatieFrontala, pozitieIncorecta ? 1 : 0);
      
      String textBLE = String(buffBLE) + "\n";
      pTxCharacteristic->setValue(textBLE.c_str());
      pTxCharacteristic->notify();
      delay(10); 
    }
    ultimulMilisBLE = millis();
  }

  delay(30); 
}