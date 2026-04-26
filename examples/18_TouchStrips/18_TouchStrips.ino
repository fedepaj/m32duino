#include <Arduino.h>

// Usiamo i nomi dei pin standard di Arduino/STM32 (es. PC1 invece di solo 1)
// Pitch strip
#define PITCH_SCL PC1
#define PITCH_SDA PC0
// Mod strip  
#define MOD_SCL   PC3
#define MOD_SDA   PC2

// Reset pins
#define RESET_A PA9
#define RESET_B PA10

#define TS_ADDR 0x15  // 7-bit, stesso per entrambe le strip

// Struttura per contenere i pin del bus I2C
struct I2CBus {
  uint8_t scl_pin, sda_pin;
};

// Funzioni helper per pilotare i pin usando le API standard di Arduino
static inline void pin_hi(uint8_t pin) { digitalWrite(pin, HIGH); }
static inline void pin_lo(uint8_t pin) { digitalWrite(pin, LOW); }
static inline bool pin_rd(uint8_t pin) { return digitalRead(pin) == HIGH; }

// Uso delayMicroseconds invece dei cicli a vuoto per maggiore affidabilità
static inline void i2c_delay() { delayMicroseconds(2); }

void bus_init(I2CBus& b) {
  // OUTPUT_OPEN_DRAIN imposta il pin correttamente per I2C e abilita il clock!
  pinMode(b.scl_pin, OUTPUT_OPEN_DRAIN);
  pinMode(b.sda_pin, OUTPUT_OPEN_DRAIN);
  
  // Condizione di riposo del bus I2C: entrambe le linee ALTE
  pin_hi(b.scl_pin); 
  pin_hi(b.sda_pin);
}

void bus_start(I2CBus& b) {
  pin_hi(b.sda_pin); pin_hi(b.scl_pin); i2c_delay();
  pin_lo(b.sda_pin); i2c_delay();
  pin_lo(b.scl_pin); i2c_delay();
}

void bus_stop(I2CBus& b) {
  pin_lo(b.sda_pin); i2c_delay();
  pin_hi(b.scl_pin); i2c_delay();
  pin_hi(b.sda_pin); i2c_delay();
}

bool bus_writeByte(I2CBus& b, uint8_t v) {
  for (int i = 7; i >= 0; i--) {
    if (v & (1 << i)) pin_hi(b.sda_pin); else pin_lo(b.sda_pin);
    i2c_delay();
    pin_hi(b.scl_pin); i2c_delay();
    pin_lo(b.scl_pin); i2c_delay();
  }
  pin_hi(b.sda_pin); i2c_delay(); // Rilascia SDA per leggere l'ACK
  pin_hi(b.scl_pin); i2c_delay(); // Clock alto
  bool ack = !pin_rd(b.sda_pin);  // Se leggiamo LOW, lo slave ha mandato l'ACK
  pin_lo(b.scl_pin); i2c_delay();
  return ack;
}

uint8_t bus_readByte(I2CBus& b, bool ack) {
  pin_hi(b.sda_pin);  // Assicurati che SDA sia rilasciato (alto)
  uint8_t v = 0;
  for (int i = 7; i >= 0; i--) {
    i2c_delay();
    pin_hi(b.scl_pin); i2c_delay();
    if (pin_rd(b.sda_pin)) v |= (1 << i);
    pin_lo(b.scl_pin); i2c_delay();
  }
  // Invia ACK o NACK al dispositivo
  if (ack) pin_lo(b.sda_pin); else pin_hi(b.sda_pin);
  i2c_delay();
  pin_hi(b.scl_pin); i2c_delay();
  pin_lo(b.scl_pin); i2c_delay();
  return v;
}

// Lettura dalla strip: scrittura indirizzo, scrittura registro 0x00, repeated start e lettura 3 byte
bool readStrip(I2CBus& b, uint8_t* out) {
  bus_start(b);
  if (!bus_writeByte(b, TS_ADDR << 1)) { bus_stop(b); return false; }
  if (!bus_writeByte(b, 0x00)) { bus_stop(b); return false; }
  
  bus_start(b);  // Repeated start
  
  if (!bus_writeByte(b, (TS_ADDR << 1) | 1)) { bus_stop(b); return false; }
  out[0] = bus_readByte(b, true);
  out[1] = bus_readByte(b, true);
  out[2] = bus_readByte(b, false); // NACK sull'ultimo byte come da specifiche I2C
  bus_stop(b);
  
  return true;
}

// Inizializzazione dei bus con le definizioni dei pin
I2CBus pitchBus = {PITCH_SCL, PITCH_SDA};
I2CBus modBus   = {MOD_SCL, MOD_SDA};

void setup() {
  
  Serial.begin(115200);
  // Attesa sicura della Seriale (timeout dopo 3 secondi per evitare blocchi se non collegato via USB)
  uint32_t t = millis();
  while (!Serial && (millis() - t < 3000)) delay(10); 
  
  // Reset hardware dei chip
  pinMode(RESET_A, OUTPUT);
  pinMode(RESET_B, OUTPUT);
  
  digitalWrite(RESET_A, HIGH);
  digitalWrite(RESET_B, HIGH);
  delay(10);
  digitalWrite(RESET_A, LOW);
  digitalWrite(RESET_B, LOW);
  delay(50);
  digitalWrite(RESET_A, HIGH);
  digitalWrite(RESET_B, HIGH);
  delay(200);  // Attesa per il boot dei sensori
  
  bus_init(pitchBus);
  bus_init(modBus);
  
  Serial.println(">>> Touch strip test (Arduino API) <<<");
}

void loop() {
  uint8_t p[3] = {0}, m[3] = {0}; // Inizializza a zero per sicurezza
  
  bool okP = readStrip(pitchBus, p);
  delay(3); // Piccolo respiro tra una lettura e l'altra
  bool okM = readStrip(modBus, m);
  delay(3);
  
  static uint32_t lastDbg = 0;
  if (millis() - lastDbg >= 500) {
    lastDbg = millis();
    
    Serial.print("PITCH ok="); Serial.print(okP);
    Serial.print(" data=");
    Serial.print(p[0], HEX); Serial.print(" ");
    Serial.print(p[1], HEX); Serial.print(" ");
    Serial.print(p[2], HEX);
    
    Serial.print("  |  MOD ok="); Serial.print(okM);
    Serial.print(" data=");
    Serial.print(m[0], HEX); Serial.print(" ");
    Serial.print(m[1], HEX); Serial.print(" ");
    Serial.println(m[2], HEX);
  }
  
  delay(30); // Loop a circa 30Hz
}