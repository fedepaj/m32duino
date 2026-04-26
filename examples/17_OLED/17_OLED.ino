#include <Arduino.h>
#include <U8g2lib.h>
#include <SPI.h>
#include <math.h>

// PIN CONFIGURATION (Komplete Kontrol M32 - STM32F103)
#define OLED_MOSI PB5
#define OLED_CLK  PB3
#define OLED_CS   PB15
#define OLED_DC   PB4
#define OLED_RES  PD2

// Inizializzazione U8G2 con rotazione 180° (R2)
U8G2_SSD1306_128X32_UNIVISION_F_4W_HW_SPI u8g2(U8G2_R2, OLED_CS, OLED_DC, OLED_RES);

// Variabili per la simulazione degli encoder
float cutoff = 64.0;
float resonance = 10.0;
float c_dir = 0.8; // Velocità animazione cutoff
float r_dir = 0.3; // Velocità animazione risonanza

void setup() {
  // Configurazione SPI hardware per STM32 Remap (PB3, PB5)
  SPI.setMOSI(OLED_MOSI);
  SPI.setSCLK(OLED_CLK);
  
  u8g2.begin();
}

/**
 * Disegna una curva di risposta di un filtro Passa-Basso
 * @param cut: posizione X del cutoff (0-127)
 * @param res: altezza del picco di risonanza (0-25)
 */
void drawFilterGraph(int cut, int res) {
  int prev_y = 31;
  
  for (int x = 0; x < 128; x++) {
    float y_func;
    
    if (x <= cut) {
      // Prima del cutoff: linea piatta (guadagno unitario)
      y_func = 18.0; 
    } else {
      // Dopo il cutoff: pendenza 24dB/ottava (approssimata con quadratica)
      float distance = (x - cut) * 0.4;
      y_func = 18.0 + (distance * distance);
    }

    // Aggiunta del picco di risonanza (Campana di Gauss centrata sul cutoff)
    // Più x è vicino a cut, più sottraiamo a y (alzando la linea verso l'alto)
    float bell = res * exp(-pow(x - cut, 2) / 30.0);
    y_func -= bell;

    // Vincoli per non uscire dallo schermo (0-31)
    int y_final = (int)y_func;
    if (y_final < 2)  y_final = 2;
    if (y_final > 31) y_final = 31;

    // Disegno della linea continua
    if (x > 0) {
      u8g2.drawLine(x - 1, prev_y, x, y_final);
      
      // Effetto "Area Riempita" opzionale (stile Arturia):
      // u8g2.drawVLine(x, y_final, 31 - y_final); 
    }
    prev_y = y_final;
  }
}

void loop() {
  u8g2.clearBuffer();

  // 1. Disegna l'interfaccia testuale
  u8g2.setFont(u8g2_font_04b_03_tr); // Font piccolo e tecnico
  u8g2.setCursor(2, 6);
  u8g2.print("CUT:"); u8g2.print((int)cutoff);
  u8g2.setCursor(90, 6);
  u8g2.print("RES:"); u8g2.print((int)resonance);

  // 2. Disegna la curva del filtro
  drawFilterGraph((int)cutoff, (int)resonance);

  // 3. Disegna una linea di base
  u8g2.drawHLine(0, 31, 128);

  u8g2.sendBuffer();

  // --- LOGICA DI ANIMAZIONE (Simula la rotazione degli encoder) ---
  
  cutoff += c_dir;
  if (cutoff > 110 || cutoff < 15) c_dir *= -1; // Rimbalzo cutoff

  resonance += r_dir;
  if (resonance > 22 || resonance < 2) r_dir *= -1; // Rimbalzo risonanza

  delay(16); // Circa 60 FPS
}