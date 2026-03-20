/*
 * Cardputer Audio Visualizer v17 FINAL
 * ─────────────────────────────────────
 * - Rainbow splash screen on boot
 * - 13 modes, switch with left/right arrow keys
 *     0  - Bars          (rainbow spectrum analyzer)
 *     1  - Waveform      (thick rainbow oscilloscope)
 *     2  - Radial        (circle/radial bars)
 *     3  - Dot Matrix    (frequency dots)
 *     4  - Matrix Rain   (rainbow digital rain)
 *     5  - Lissajous     (X/Y curve, rotates with volume)
 *     6  - Fireworks     (rainbow explosions, density = volume)
 *     7  - Mirror        (4-way mirrored kaleidoscope bars)
 *     8  - Kaleidoscope  (organic blobs, frequency + volume reactive)
 *     9  - Polygons      (bouncing mixed polygons, cycling rainbow colors)
 *     10 - Wave Grid     (flowing sine wave lines, color shifts with volume)
 *     11 - Ripple        (rainbow rings expanding from center, volume reactive)
 *     12 - Inverse Bars  (rainbow background, black bars cut through it)
 * - LED reacts to volume in all modes
 * - Mode name shown briefly on switch in auto-sized color-cycling box
 *
 * Libraries required:
 *   M5Cardputer
 *   arduinoFFT  (by Enrique Condes, v2.x)
 *   Adafruit NeoPixel
 */

#include <M5Cardputer.h>
#include <arduinoFFT.h>
#include <Adafruit_NeoPixel.h>
#include <math.h>

// ── Display ───────────────────────────────────────────────────────────────────
#define SCREEN_W  240
#define SCREEN_H  135
#define CX        120
#define CY        67

// ── FFT ───────────────────────────────────────────────────────────────────────
#define SAMPLES     1024
#define SAMPLE_RATE 16000
#define FFT_SIZE    (SAMPLES / 2)

// ── Bars ──────────────────────────────────────────────────────────────────────
#define NUM_BARS  48
#define BAR_W     (SCREEN_W / NUM_BARS)
#define BAR_GAP   1
#define BAR_MAX_H (SCREEN_H - 2)

// ── Frequency range ───────────────────────────────────────────────────────────
#define FREQ_MIN  20.0f
#define FREQ_MAX  12000.0f

// ── LED ───────────────────────────────────────────────────────────────────────
#define LED_PIN   21
#define LED_COUNT 1
Adafruit_NeoPixel led(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// ── Tuning ────────────────────────────────────────────────────────────────────
float SCALE          = 8000.0f;
float NOISE_FLOOR    = 1200.0f;
const float RISE     = 0.80f;
const float DECAY    = 0.18f;
const float PEAK_DEC = 0.030f;
const int   PEAK_HLD = 30;

// ── Mode ──────────────────────────────────────────────────────────────────────
int  currentMode    = 0;
const int NUM_MODES = 13;
const char* modeNames[NUM_MODES] = {
  "BARS", "WAVEFORM", "RADIAL", "DOT MATRIX",
  "MATRIX RAIN", "LISSAJOUS", "FIREWORKS", "MIRROR",
  "KALEIDOSCOPE", "POLYGONS", "WAVE GRID", "RIPPLE",
  "INVERSE BARS"
};
unsigned long modeNameTimer = 0;
const int MODE_NAME_MS = 1500;

// ── Forward declarations ──────────────────────────────────────────────────────
uint16_t hueToColor(float hue);
extern float barH[];

// ── Matrix Rain ───────────────────────────────────────────────────────────────
#define MATRIX_COLS   30
#define MATRIX_CHAR_W 8
#define MATRIX_CHAR_H 8
#define MATRIX_ROWS   (SCREEN_H / MATRIX_CHAR_H)

int   matrixHead[MATRIX_COLS];
int   matrixLen[MATRIX_COLS];
float matrixSpeed[MATRIX_COLS];
float matrixSpeedInc[MATRIX_COLS];
char  matrixChars[MATRIX_COLS][MATRIX_ROWS];
int   matrixHue[MATRIX_COLS];
bool  matrixActive[MATRIX_COLS];

const char MATRIX_CHARSET[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
  "0123456789"
  "!@#$%^&*()<>?/\\|[]{}~`";
const int CHARSET_LEN = sizeof(MATRIX_CHARSET) - 1;

char randomMatrixChar() { return MATRIX_CHARSET[random(CHARSET_LEN)]; }

void initMatrixCol(int col, float vol) {
  matrixHead[col]     = 0;
  matrixLen[col]      = 4 + random(8) + (int)(vol * 8);
  matrixSpeed[col]    = 0;
  matrixSpeedInc[col] = 0.3f + vol * 1.5f + (float)random(100) / 200.0f;
  matrixHue[col]      = random(360);
  matrixActive[col]   = true;
  for (int r = 0; r < MATRIX_ROWS; r++) matrixChars[col][r] = randomMatrixChar();
}

void initMatrix() {
  for (int c = 0; c < MATRIX_COLS; c++) {
    matrixActive[c] = false;
    matrixHead[c]   = -1;
    for (int r = 0; r < MATRIX_ROWS; r++) matrixChars[c][r] = randomMatrixChar();
  }
}

void updateMatrix(float vol) {
  float spawnChance = 0.05f + vol * 0.4f;
  for (int c = 0; c < MATRIX_COLS; c++) {
    if (!matrixActive[c] && (float)random(1000) / 1000.0f < spawnChance)
      initMatrixCol(c, vol);
  }
  for (int c = 0; c < MATRIX_COLS; c++) {
    if (!matrixActive[c]) continue;
    matrixSpeed[c] += matrixSpeedInc[c] * (1.0f + vol * 2.0f);
    while (matrixSpeed[c] >= 1.0f) {
      matrixSpeed[c] -= 1.0f;
      matrixHead[c]++;
      int trailRow = matrixHead[c] - random(matrixLen[c]);
      if (trailRow >= 0 && trailRow < MATRIX_ROWS)
        matrixChars[c][trailRow] = randomMatrixChar();
      if (matrixHead[c] >= MATRIX_ROWS + matrixLen[c])
        matrixActive[c] = false;
    }
  }
}

void drawMatrix(float vol) {
  M5Cardputer.Display.startWrite();
  M5Cardputer.Display.fillScreen(TFT_BLACK);
  M5Cardputer.Display.setTextSize(1);
  for (int c = 0; c < MATRIX_COLS; c++) {
    if (!matrixActive[c]) continue;
    int x = c * MATRIX_CHAR_W;
    for (int r = 0; r < MATRIX_ROWS; r++) {
      int distFromHead = matrixHead[c] - r;
      if (distFromHead < 0 || distFromHead >= matrixLen[c]) continue;
      int y = r * MATRIX_CHAR_H;
      if (distFromHead == 0) {
        M5Cardputer.Display.setTextColor(TFT_WHITE);
      } else {
        float brightness = 1.0f - (float)distFromHead / matrixLen[c];
        float hue = fmod(matrixHue[c] + millis() / 20.0f, 360.0f);
        uint16_t col = hueToColor(hue);
        uint8_t r8 = ((col >> 11) & 0x1F) << 3;
        uint8_t g8 = ((col >> 5)  & 0x3F) << 2;
        uint8_t b8 = (col & 0x1F) << 3;
        M5Cardputer.Display.setTextColor(M5Cardputer.Display.color565(
          (uint8_t)(r8 * brightness),
          (uint8_t)(g8 * brightness),
          (uint8_t)(b8 * brightness)
        ));
      }
      M5Cardputer.Display.setCursor(x, y);
      M5Cardputer.Display.print(matrixChars[c][r]);
    }
  }
  M5Cardputer.Display.endWrite();
}

// ── Lissajous ─────────────────────────────────────────────────────────────────
#define LISS_TRAIL  180
#define LISS_MARGIN 10

struct LissPoint { int16_t x, y; };
LissPoint lissTrail[LISS_TRAIL];
int       lissHead  = 0;
float     lissPhase = 0.0f;
float     lissFreqX = 3.0f;
float     lissFreqY = 2.0f;
float     lissT     = 0.0f;

void initLissajous() {
  memset(lissTrail, 0, sizeof(lissTrail));
  lissHead  = 0;
  lissPhase = 0.0f;
  lissT     = 0.0f;
}

void updateLissajous(float vol) {
  float speed  = 0.04f + vol * 0.18f;
  float radius = (float)(min(SCREEN_W, SCREEN_H) / 2 - LISS_MARGIN)
                 * (0.7f + vol * 0.3f);
  lissFreqX = 3.0f + sin(millis() / 8000.0f) * 1.0f;
  lissFreqY = 2.0f + cos(millis() / 6000.0f) * 1.0f;
  lissPhase += 0.002f + vol * 0.008f;
  lissT     += speed;

  float rx = radius * sin(lissFreqX * lissT + lissPhase);
  float ry = radius * cos(lissFreqY * lissT);

  static float rotation = 0.0f;
  rotation += vol * 0.08f;
  float cosR = cos(rotation);
  float sinR = sin(rotation);

  int px = CX + (int)(rx * cosR - ry * sinR);
  int py = CY + (int)(rx * sinR + ry * cosR);
  px = max(0, min(SCREEN_W - 1, px));
  py = max(0, min(SCREEN_H - 1, py));

  lissTrail[lissHead] = { (int16_t)px, (int16_t)py };
  lissHead = (lissHead + 1) % LISS_TRAIL;
}

void drawLissajous() {
  M5Cardputer.Display.startWrite();
  M5Cardputer.Display.fillScreen(TFT_BLACK);
  for (int i = 0; i < LISS_TRAIL; i++) {
    int idx = (lissHead + i) % LISS_TRAIL;
    int px  = lissTrail[idx].x;
    int py  = lissTrail[idx].y;
    float fade = (float)i / LISS_TRAIL;
    float hue  = fmod((float)i / LISS_TRAIL * 360.0f + millis() / 15.0f, 360.0f);
    uint16_t col = hueToColor(hue);
    uint8_t r8 = ((col >> 11) & 0x1F) << 3;
    uint8_t g8 = ((col >> 5)  & 0x3F) << 2;
    uint8_t b8 = (col & 0x1F) << 3;
    r8 = (uint8_t)(r8 * fade);
    g8 = (uint8_t)(g8 * fade);
    b8 = (uint8_t)(b8 * fade);
    uint16_t dimCol = M5Cardputer.Display.color565(r8, g8, b8);
    if (i > LISS_TRAIL * 0.85f)
      M5Cardputer.Display.fillCircle(px, py, 2, dimCol);
    else
      M5Cardputer.Display.drawPixel(px, py, dimCol);
  }
  M5Cardputer.Display.endWrite();
}

// ── Fireworks ─────────────────────────────────────────────────────────────────
#define MAX_ROCKETS    20
#define MAX_PARTICLES  20

struct Particle {
  float x, y, vx, vy, life, decay;
  uint16_t color;
  bool streak;
};

struct Rocket {
  float x, y, vy, targetY;
  uint16_t color;
  bool active, exploded;
  Particle particles[MAX_PARTICLES];
};

Rocket rockets[MAX_ROCKETS];

void initFireworks() {
  for (int i = 0; i < MAX_ROCKETS; i++) rockets[i].active = false;
}

void explodeRocket(Rocket& r, float vol) {
  r.exploded = true;
  int numParticles = 8 + random(MAX_PARTICLES - 8);
  for (int p = 0; p < numParticles; p++) {
    float angle = (float)p / numParticles * 2.0f * M_PI;
    float speed = 0.5f + vol * 2.0f + (float)random(200) / 100.0f;
    r.particles[p] = {
      r.x, r.y,
      cos(angle) * speed, sin(angle) * speed,
      1.0f, 0.025f + (float)random(100) / 2000.0f,
      hueToColor(fmod(random(360), 360.0f)),
      (p % 3 == 0)
    };
  }
  for (int p = numParticles; p < MAX_PARTICLES; p++) r.particles[p].life = 0;
}

void launchRocket(float vol) {
  for (int i = 0; i < MAX_ROCKETS; i++) {
    if (!rockets[i].active) {
      Rocket& r    = rockets[i];
      r.x          = 10 + random(SCREEN_W - 20);
      r.y          = SCREEN_H - 1;
      r.vy         = -(6.0f + random(20) / 10.0f + vol * 8.0f);
      r.targetY    = 10 + random(SCREEN_H / 2);
      r.color      = hueToColor(random(360));
      r.active     = true;
      r.exploded   = false;
      for (int p = 0; p < MAX_PARTICLES; p++) r.particles[p].life = 0;
      break;
    }
  }
}

void updateFireworks(float vol) {
  if (vol > 0.01f) {
    float spawnChance = vol * 0.9f;
    if ((float)random(1000) / 1000.0f < spawnChance) launchRocket(vol);
  }
  for (int i = 0; i < MAX_ROCKETS; i++) {
    Rocket& r = rockets[i];
    if (!r.active) continue;
    if (!r.exploded) {
      r.y += r.vy;
      r.vy *= 0.98f;
      if (r.y <= r.targetY) explodeRocket(r, vol);
    } else {
      bool anyAlive = false;
      for (int p = 0; p < MAX_PARTICLES; p++) {
        Particle& pt = r.particles[p];
        if (pt.life <= 0) continue;
        anyAlive = true;
        pt.x += pt.vx; pt.y += pt.vy;
        pt.vy += 0.05f; pt.vx *= 0.97f;
        pt.life -= pt.decay;
      }
      if (!anyAlive) r.active = false;
    }
  }
}

void drawFireworks() {
  M5Cardputer.Display.startWrite();
  M5Cardputer.Display.fillScreen(TFT_BLACK);
  for (int i = 0; i < MAX_ROCKETS; i++) {
    Rocket& r = rockets[i];
    if (!r.active) continue;
    if (!r.exploded) {
      M5Cardputer.Display.drawPixel((int)r.x, (int)r.y, TFT_WHITE);
      M5Cardputer.Display.drawPixel((int)r.x, (int)r.y + 1, 0x4208);
      M5Cardputer.Display.drawPixel((int)r.x, (int)r.y + 2, 0x2104);
    } else {
      for (int p = 0; p < MAX_PARTICLES; p++) {
        Particle& pt = r.particles[p];
        if (pt.life <= 0) continue;
        int px = (int)pt.x, py = (int)pt.y;
        if (px < 0 || px >= SCREEN_W || py < 0 || py >= SCREEN_H) continue;
        uint16_t col = pt.color;
        uint8_t r8 = (uint8_t)((((col >> 11) & 0x1F) << 3) * pt.life);
        uint8_t g8 = (uint8_t)((((col >> 5)  & 0x3F) << 2) * pt.life);
        uint8_t b8 = (uint8_t)(((col & 0x1F) << 3) * pt.life);
        uint16_t dimCol = M5Cardputer.Display.color565(r8, g8, b8);
        if (pt.streak) {
          int px2 = max(0, min(SCREEN_W-1, px - (int)(pt.vx * 3)));
          int py2 = max(0, min(SCREEN_H-1, py - (int)(pt.vy * 3)));
          M5Cardputer.Display.drawLine(px, py, px2, py2, dimCol);
          M5Cardputer.Display.drawLine(px+1, py, px2+1, py2, dimCol);
        } else {
          M5Cardputer.Display.fillRect(px, py, 2, 2, dimCol);
        }
      }
    }
  }
  M5Cardputer.Display.endWrite();
}

// ── Mirror mode ───────────────────────────────────────────────────────────────
void drawMirror() {
  M5Cardputer.Display.startWrite();
  M5Cardputer.Display.fillScreen(TFT_BLACK);
  int halfBars = NUM_BARS / 2;
  int halfW    = SCREEN_W / 2;
  int halfH    = SCREEN_H / 2;
  int barW     = max(1, halfW / halfBars);
  for (int i = 0; i < halfBars; i++) {
    int   barIdx   = halfBars - 1 - i;
    float h        = barH[barIdx];
    float colorPos = (float)i / halfBars;
    float hue = fmod(240.0f - colorPos * 240.0f + millis() / 30.0f, 360.0f);
    uint16_t col = hueToColor(hue);
    int xLeft  = halfW - (i + 1) * barW;
    int xRight = halfW + i * barW;
    int barHpx = (int)(h * halfH);
    if (barHpx > 0) {
      M5Cardputer.Display.fillRect(xLeft,  0,                  barW - 1, barHpx, col);
      M5Cardputer.Display.fillRect(xRight, 0,                  barW - 1, barHpx, col);
      M5Cardputer.Display.fillRect(xLeft,  SCREEN_H - barHpx, barW - 1, barHpx, col);
      M5Cardputer.Display.fillRect(xRight, SCREEN_H - barHpx, barW - 1, barHpx, col);
    }
  }
  M5Cardputer.Display.drawFastVLine(halfW, 0, SCREEN_H, 0x2104);
  M5Cardputer.Display.drawFastHLine(0, halfH, SCREEN_W, 0x2104);
  M5Cardputer.Display.endWrite();
}

// ── Kaleidoscope ──────────────────────────────────────────────────────────────
#define KALEIDO_BLOBS  8
const float blobHues[KALEIDO_BLOBS] = {
  0.0f, 45.0f, 90.0f, 135.0f, 180.0f, 225.0f, 270.0f, 315.0f
};
const int blobBand[KALEIDO_BLOBS] = { 2, 6, 10, 16, 22, 30, 38, 44 };

void drawBlob(int cx, int cy, float radius, float hue, float phase, float freqLevel) {
  if (radius < 2) return;
  uint16_t col = hueToColor(hue);
  const int POINTS = 24;
  int prevX = 0, prevY = 0;
  for (int p = 0; p <= POINTS; p++) {
    float angle  = (float)p / POINTS * 2.0f * M_PI;
    float wobble = 1.0f
      + 0.3f * sin(3.0f * angle + phase)
      + 0.2f * sin(5.0f * angle - phase * 1.3f)
      + freqLevel * 0.5f * sin(2.0f * angle + phase * 0.7f);
    float r = radius * wobble;
    int   x = max(0, min(SCREEN_W - 1, cx + (int)(r * cos(angle))));
    int   y = max(0, min(SCREEN_H - 1, cy + (int)(r * sin(angle))));
    if (p > 0) {
      M5Cardputer.Display.drawLine(prevX, prevY, x, y, col);
      M5Cardputer.Display.drawLine(cx, cy, x, y, col);
    }
    prevX = x; prevY = y;
  }
}

void drawKaleidoscope(float vol) {
  M5Cardputer.Display.startWrite();
  M5Cardputer.Display.fillScreen(TFT_BLACK);
  float t        = millis() / 1000.0f;
  float scale    = 0.2f + vol * 0.8f;
  float maxRadius= min(SCREEN_W, SCREEN_H) * 0.22f * scale;
  for (int b = 0; b < KALEIDO_BLOBS; b++) {
    float angle    = (float)b / KALEIDO_BLOBS * 2.0f * M_PI + t * 0.2f;
    float dist     = vol * 35.0f;
    int   bx       = CX + (int)(dist * cos(angle));
    int   by       = CY + (int)(dist * sin(angle));
    float freqLevel= barH[blobBand[b]];
    float radius   = vol < 0.02f ? 0.0f : 8.0f + maxRadius * (0.3f + freqLevel * 0.7f);
    float phase    = t * (1.0f + b * 0.3f) + freqLevel * 2.0f;
    float hue      = fmod(blobHues[b] + vol * 30.0f + t * 10.0f, 360.0f);
    drawBlob(bx, by, radius, hue, phase, freqLevel);
    int mbx = CX - (bx - CX);
    int mby = CY - (by - CY);
    drawBlob(mbx, mby, radius, hue, phase + M_PI, freqLevel);
  }
  float centerR   = 3.0f + vol * 8.0f;
  float centerHue = fmod(t * 60.0f, 360.0f);
  M5Cardputer.Display.fillCircle(CX, CY, (int)centerR, hueToColor(centerHue));
  M5Cardputer.Display.endWrite();
}

// ── Polygons ──────────────────────────────────────────────────────────────────
#define MAX_POLYS   12

struct Poly {
  float x, y, vx, vy;
  float radius, baseRadius;
  int   sides;
  float rotation, rotSpeed;
  float hue, hueSpeed;
  int   bandIdx;
  bool  active;
};

Poly polys[MAX_POLYS];

void initPoly(int i) {
  Poly& p      = polys[i];
  p.x          = 20 + random(SCREEN_W - 40);
  p.y          = 20 + random(SCREEN_H - 40);
  p.vx         = (random(200) - 100) / 100.0f;
  p.vy         = (random(200) - 100) / 100.0f;
  p.baseRadius = 8 + random(16);
  p.radius     = p.baseRadius;
  p.sides      = 3 + random(4);
  p.rotation   = random(360) * M_PI / 180.0f;
  p.rotSpeed   = (random(100) - 50) / 500.0f;
  p.hue        = random(360);
  p.hueSpeed   = 0.5f + random(200) / 100.0f;
  p.bandIdx    = random(NUM_BARS);
  p.active     = true;
}

void initPolygons() {
  for (int i = 0; i < MAX_POLYS; i++) initPoly(i);
}

void drawPoly(float cx, float cy, float radius, int sides, float rotation, uint16_t col) {
  int prevX = 0, prevY = 0;
  for (int s = 0; s <= sides; s++) {
    float angle = rotation + (float)s / sides * 2.0f * M_PI;
    int x = max(0, min(SCREEN_W-1, (int)(cx + radius * cos(angle))));
    int y = max(0, min(SCREEN_H-1, (int)(cy + radius * sin(angle))));
    if (s > 0) M5Cardputer.Display.drawLine(prevX, prevY, x, y, col);
    prevX = x; prevY = y;
  }
}

bool polyCollide(Poly& a, Poly& b) {
  float dx = a.x - b.x, dy = a.y - b.y;
  return sqrt(dx*dx + dy*dy) < (a.radius + b.radius);
}

void updatePolygons(float vol) {
  float speedMult = 1.0f + vol * 3.0f;
  for (int i = 0; i < MAX_POLYS; i++) {
    Poly& p = polys[i];
    if (!p.active) continue;
    p.x += p.vx * speedMult;
    p.y += p.vy * speedMult;
    p.rotation += p.rotSpeed * speedMult;
    p.hue = fmod(p.hue + p.hueSpeed + vol * 2.0f, 360.0f);
    float freqLevel = barH[p.bandIdx];
    p.radius = p.baseRadius * (1.0f + freqLevel * 1.5f + vol * 0.5f);
    if (p.x - p.radius < 0)        { p.x = p.radius;            p.vx =  fabs(p.vx); }
    if (p.x + p.radius > SCREEN_W) { p.x = SCREEN_W - p.radius; p.vx = -fabs(p.vx); }
    if (p.y - p.radius < 0)        { p.y = p.radius;             p.vy =  fabs(p.vy); }
    if (p.y + p.radius > SCREEN_H) { p.y = SCREEN_H - p.radius;  p.vy = -fabs(p.vy); }
    for (int j = i + 1; j < MAX_POLYS; j++) {
      Poly& q = polys[j];
      if (!q.active || !polyCollide(p, q)) continue;
      float tvx = p.vx; float tvy = p.vy;
      p.vx = q.vx; p.vy = q.vy; q.vx = tvx; q.vy = tvy;
      float dx = p.x - q.x, dy = p.y - q.y;
      float dist = max(0.1f, sqrt(dx*dx + dy*dy));
      float overlap = (p.radius + q.radius - dist) / 2.0f;
      p.x += dx/dist*overlap; p.y += dy/dist*overlap;
      q.x -= dx/dist*overlap; q.y -= dy/dist*overlap;
      p.hue = fmod(p.hue + 180.0f, 360.0f);
      q.hue = fmod(q.hue + 180.0f, 360.0f);
    }
  }
}

void drawPolygons() {
  M5Cardputer.Display.startWrite();
  M5Cardputer.Display.fillScreen(TFT_BLACK);
  for (int i = 0; i < MAX_POLYS; i++) {
    Poly& p = polys[i];
    if (!p.active) continue;
    drawPoly(p.x, p.y, p.radius, p.sides, p.rotation, hueToColor(p.hue));
    M5Cardputer.Display.drawPixel((int)p.x, (int)p.y, TFT_WHITE);
  }
  M5Cardputer.Display.endWrite();
}

// ── Wave Grid ─────────────────────────────────────────────────────────────────
#define WAVE_LINES   20
#define WAVE_POINTS  80

float wavePhase = 0.0f;

void drawWaveGrid(float vol, float avgVol) {
  M5Cardputer.Display.startWrite();
  M5Cardputer.Display.fillScreen(TFT_BLACK);

  float baseHue  = 240.0f + avgVol * 120.0f;
  float amplitude= 0.5f + vol * (SCREEN_H / (WAVE_LINES * 1.2f));
  wavePhase     += 0.05f + vol * 0.25f;

  for (int line = 0; line < WAVE_LINES; line++) {
    float lineY     = (float)(line + 0.5f) / WAVE_LINES * SCREEN_H;
    float freq      = 2.0f + (float)line / WAVE_LINES * 2.0f;
    float linePhase = wavePhase + (float)line / WAVE_LINES * M_PI * 2.0f;
    float brightness= 0.6f + 0.4f * ((float)line / WAVE_LINES);
    float hue       = fmod(baseHue + (float)line / WAVE_LINES * 30.0f, 360.0f);

    uint16_t col = hueToColor(hue);
    uint8_t r8 = (uint8_t)((((col >> 11) & 0x1F) << 3) * brightness);
    uint8_t g8 = (uint8_t)((((col >> 5)  & 0x3F) << 2) * brightness);
    uint8_t b8 = (uint8_t)(((col & 0x1F) << 3) * brightness);
    uint16_t lineCol = M5Cardputer.Display.color565(r8, g8, b8);

    int prevX = -1, prevY = -1;
    for (int p = 0; p <= WAVE_POINTS; p++) {
      float t  = (float)p / WAVE_POINTS;
      int   x  = (int)(t * SCREEN_W);
      float y  = lineY + amplitude * sin(freq * t * 2.0f * M_PI + linePhase);
      int   yi = max(0, min(SCREEN_H - 1, (int)y));
      if (prevX >= 0) M5Cardputer.Display.drawLine(prevX, prevY, x, yi, lineCol);
      prevX = x; prevY = yi;
    }
  }
  M5Cardputer.Display.endWrite();
}

// ── Ripple ────────────────────────────────────────────────────────────────────
#define MAX_RIPPLES   16
#define MAX_RIPPLE_R  155.0f

struct Ripple {
  float radius, speed, hue, life;
  bool  active;
};

Ripple ripples[MAX_RIPPLES];
float  rippleHue  = 0.0f;
float  spawnAccum = 0.0f;

void initRipples() {
  for (int i = 0; i < MAX_RIPPLES; i++) ripples[i].active = false;
  rippleHue  = 0.0f;
  spawnAccum = 0.0f;
}

void spawnRipple(float vol) {
  for (int i = 0; i < MAX_RIPPLES; i++) {
    if (!ripples[i].active) {
      ripples[i] = { 2.0f, 1.5f + vol * 4.0f, rippleHue, 1.0f, true };
      rippleHue  = fmod(rippleHue + 25.0f, 360.0f);
      break;
    }
  }
}

void updateRipples(float vol) {
  if (vol > 0.02f) {
    spawnAccum += vol * 0.4f;
    while (spawnAccum >= 1.0f) {
      spawnRipple(vol);
      spawnAccum -= 1.0f;
    }
  }
  for (int i = 0; i < MAX_RIPPLES; i++) {
    if (!ripples[i].active) continue;
    ripples[i].radius += ripples[i].speed;
    ripples[i].life    = 1.0f - ripples[i].radius / MAX_RIPPLE_R;
    if (ripples[i].radius > MAX_RIPPLE_R) ripples[i].active = false;
  }
  rippleHue = fmod(rippleHue + 0.5f, 360.0f);
}

void drawRipples() {
  M5Cardputer.Display.startWrite();
  M5Cardputer.Display.fillScreen(TFT_BLACK);
  for (int i = 0; i < MAX_RIPPLES; i++) {
    if (!ripples[i].active) continue;
    float life = max(0.0f, ripples[i].life);
    float hue  = fmod(ripples[i].hue + millis() / 30.0f, 360.0f);
    uint16_t col = hueToColor(hue);
    uint8_t r8 = (uint8_t)((((col >> 11) & 0x1F) << 3) * life);
    uint8_t g8 = (uint8_t)((((col >> 5)  & 0x3F) << 2) * life);
    uint8_t b8 = (uint8_t)(((col & 0x1F) << 3) * life);
    uint16_t dimCol = M5Cardputer.Display.color565(r8, g8, b8);
    int r = (int)ripples[i].radius;
    M5Cardputer.Display.drawCircle(CX, CY, r, dimCol);
    if (life > 0.5f) M5Cardputer.Display.drawCircle(CX, CY, r + 1, dimCol);
  }
  M5Cardputer.Display.fillCircle(CX, CY, 2, hueToColor(rippleHue));
  M5Cardputer.Display.endWrite();
}

// ── State ─────────────────────────────────────────────────────────────────────
int16_t micBuf[SAMPLES];
double  vReal[SAMPLES];
double  vImag[SAMPLES];
ArduinoFFT<double> FFT(vReal, vImag, SAMPLES, SAMPLE_RATE);

float barH[NUM_BARS];
float peakH[NUM_BARS];
int   peakTimer[NUM_BARS];
float ledSmooth = 0.0f;
float avgVolume = 0.0f;

// ── Helpers ───────────────────────────────────────────────────────────────────
uint16_t hueToColor(float hue) {
  float h = hue / 60.0f;
  int   i = (int)h;
  float f = h - i;
  float q = 1.0f - f;
  float r, g, b;
  switch (i % 6) {
    case 0: r=1; g=f; b=0; break;
    case 1: r=q; g=1; b=0; break;
    case 2: r=0; g=1; b=f; break;
    case 3: r=0; g=q; b=1; break;
    case 4: r=f; g=0; b=1; break;
    default:r=1; g=0; b=q; break;
  }
  return M5Cardputer.Display.color565(
    (uint8_t)(r * 255),
    (uint8_t)(g * 255),
    (uint8_t)(b * 255)
  );
}

uint16_t rainbowColor(int bar) {
  float hue = fmod((float)bar / NUM_BARS * 360.0f + millis() / 20.0f, 360.0f);
  return hueToColor(hue);
}

uint16_t rainbowStatic(int bar) {
  float hue = fmod((float)bar / NUM_BARS * 360.0f + millis() / 20.0f, 360.0f);
  return hueToColor(hue);
}

int binForBar(int bar) {
  float t    = (float)bar / NUM_BARS;
  float freq = FREQ_MIN * pow(FREQ_MAX / FREQ_MIN, t);
  return max(1, (int)(freq * SAMPLES / SAMPLE_RATE));
}

// ── Jingle (disabled) ────────────────────────────────────────────────────────
void playJingle() {
  // Jingle disabled — keeping function for future use
}

// ── Splash screen ─────────────────────────────────────────────────────────────
void showSplash() {
  M5Cardputer.Display.fillScreen(TFT_BLACK);
  const char* title = "Rainbow Visualizer";
  int len    = strlen(title);
  int charW  = 12;
  int totalW = len * charW;
  int startX = (SCREEN_W - totalW) / 2;
  M5Cardputer.Display.setTextSize(2);
  for (int i = 0; i < len; i++) {
    float hue = fmod((float)i / len * 360.0f, 360.0f);
    M5Cardputer.Display.setTextColor(hueToColor(hue));
    M5Cardputer.Display.setCursor(startX + i * charW, 55);
    M5Cardputer.Display.print(title[i]);
  }
  playJingle();
  delay(1500);
}

// ── Read mic ──────────────────────────────────────────────────────────────────
bool readAudio() {
  if (!M5Cardputer.Mic.record(micBuf, SAMPLES, SAMPLE_RATE)) return false;
  double dcSum = 0;
  for (int i = 0; i < SAMPLES; i++) dcSum += micBuf[i];
  double dcBias = dcSum / SAMPLES;
  for (int i = 0; i < SAMPLES; i++) {
    vReal[i] = (double)micBuf[i] - dcBias;
    vImag[i] = 0.0;
  }
  return true;
}

// ── LED ───────────────────────────────────────────────────────────────────────
void updateLED(float volume) {
  ledSmooth += (volume - ledSmooth) * 0.15f;
  float v = min(ledSmooth, 1.0f);
  uint8_t r, g;
  if (v < 0.33f) {
    r = (uint8_t)(v / 0.33f * 255); g = 255;
  } else if (v < 0.66f) {
    r = 255; g = (uint8_t)((1.0f - (v - 0.33f) / 0.33f) * 255);
  } else {
    r = 255; g = 0;
  }
  uint8_t bright = (uint8_t)(v * 255);
  led.setPixelColor(0, led.Color(
    (uint16_t)(r * bright) >> 8,
    (uint16_t)(g * bright) >> 8,
    0
  ));
  led.show();
}

// ── Mode name overlay ─────────────────────────────────────────────────────────
void drawModeName() {
  unsigned long elapsed = millis() - modeNameTimer;
  if (elapsed < MODE_NAME_MS) {
    const char* name = modeNames[currentMode];
    int len   = strlen(name);
    int textW = len * 12;
    int boxW  = textW + 20;
    int boxX  = CX - boxW / 2;
    int boxY  = 50;
    int boxH  = 34;

    // Thick color-cycling border — 4 layered rects
    for (int t = 0; t < 4; t++) {
      float hue = fmod(millis() / 8.0f + t * 30.0f, 360.0f);
      M5Cardputer.Display.drawRoundRect(boxX - t, boxY - t, boxW + t*2, boxH + t*2, 8, hueToColor(hue));
    }

    // Black fill inside
    M5Cardputer.Display.fillRoundRect(boxX, boxY, boxW, boxH, 8, TFT_BLACK);

    // Rainbow text
    M5Cardputer.Display.setTextSize(2);
    int startX = CX - textW / 2;
    for (int i = 0; i < len; i++) {
      float hue = fmod((float)i / len * 360.0f + millis() / 20.0f, 360.0f);
      M5Cardputer.Display.setTextColor(hueToColor(hue));
      M5Cardputer.Display.setCursor(startX + i * 12, 58);
      M5Cardputer.Display.print(name[i]);
    }
  } else if (elapsed < MODE_NAME_MS + 100) {
    M5Cardputer.Display.fillScreen(TFT_BLACK);
  }
}

// ── MODE 0: Bars ──────────────────────────────────────────────────────────────
void drawBars() {
  M5Cardputer.Display.startWrite();
  for (int i = 0; i < NUM_BARS; i++) {
    int      x       = i * BAR_W;
    int      filledH = (int)(barH[i] * BAR_MAX_H);
    int      peakY   = SCREEN_H - 1 - (int)(peakH[i] * BAR_MAX_H);
    uint16_t col     = rainbowColor(i);
    M5Cardputer.Display.fillRect(x, 0, BAR_W - BAR_GAP, SCREEN_H, TFT_BLACK);
    if (filledH > 0) {
      M5Cardputer.Display.fillRect(
        x, SCREEN_H - filledH, BAR_W - BAR_GAP, filledH, col
      );
    }
    if (peakH[i] > 0.01f) {
      M5Cardputer.Display.drawFastHLine(x, peakY, BAR_W - BAR_GAP, TFT_WHITE);
    }
  }
  M5Cardputer.Display.endWrite();
}

// ── MODE 1: Waveform ──────────────────────────────────────────────────────────
void drawWaveform() {
  M5Cardputer.Display.startWrite();
  int step = SAMPLES / SCREEN_W / 4;
  if (step < 1) step = 1;
  for (int x = 0; x < SCREEN_W; x++) {
    M5Cardputer.Display.drawFastVLine(x, 0, SCREEN_H, TFT_BLACK);
    int   idx    = x * step;
    float sample = (float)micBuf[idx] / 32768.0f;
    int   y      = (int)(CY - sample * (SCREEN_H - 8) * 10.0f);
    y = max(0, min(SCREEN_H - 1, y));
    float hue = fmod((float)x / SCREEN_W * 360.0f + millis() / 20.0f, 360.0f);
    M5Cardputer.Display.drawFastVLine(x, y, 3, hueToColor(hue));
  }
  M5Cardputer.Display.drawFastHLine(0, CY, SCREEN_W, 0x2104);
  M5Cardputer.Display.endWrite();
}

// ── MODE 2: Radial ────────────────────────────────────────────────────────────
void drawRadial() {
  const int MIN_R = 20;
  const int MAX_R = 62;
  M5Cardputer.Display.startWrite();
  M5Cardputer.Display.fillScreen(TFT_BLACK);
  M5Cardputer.Display.drawCircle(CX, CY, MIN_R, 0x2104);
  for (int i = 0; i < NUM_BARS; i++) {
    float angle  = (float)i / NUM_BARS * 2.0f * M_PI - M_PI / 2.0f;
    float barLen = barH[i] * (MAX_R - MIN_R);
    float x0 = CX + MIN_R * cos(angle);
    float y0 = CY + MIN_R * sin(angle);
    float x1 = CX + (MIN_R + barLen) * cos(angle);
    float y1 = CY + (MIN_R + barLen) * sin(angle);
    M5Cardputer.Display.drawLine((int)x0, (int)y0, (int)x1, (int)y1, rainbowStatic(i));
    if (peakH[i] > 0.01f) {
      float peakLen = peakH[i] * (MAX_R - MIN_R);
      int px = CX + (int)((MIN_R + peakLen) * cos(angle));
      int py = CY + (int)((MIN_R + peakLen) * sin(angle));
      M5Cardputer.Display.drawPixel(px, py, TFT_WHITE);
    }
  }
  M5Cardputer.Display.endWrite();
}

// ── MODE 3: Dot Matrix ────────────────────────────────────────────────────────
#define DOT_COLS  48
#define DOT_ROWS  16
#define DOT_W     (SCREEN_W / DOT_COLS)
#define DOT_H     (SCREEN_H / DOT_ROWS)

void drawDotMatrix() {
  if (millis() - modeNameTimer < MODE_NAME_MS)
    M5Cardputer.Display.fillScreen(TFT_BLACK);
  M5Cardputer.Display.startWrite();
  for (int col = 0; col < DOT_COLS; col++) {
    int litRows = (int)(barH[col] * DOT_ROWS);
    uint16_t col565 = rainbowStatic(col);
    for (int row = 0; row < DOT_ROWS; row++) {
      int x = col * DOT_W;
      int y = SCREEN_H - (row + 1) * DOT_H;
      M5Cardputer.Display.fillRect(x + 1, y + 1, DOT_W - 2, DOT_H - 2,
        (row < litRows) ? col565 : (uint16_t)0x1082);
    }
    if (peakH[col] > 0.01f) {
      int peakRow = min((int)(peakH[col] * DOT_ROWS), DOT_ROWS - 1);
      int x = col * DOT_W;
      int y = SCREEN_H - (peakRow + 1) * DOT_H;
      M5Cardputer.Display.fillRect(x + 1, y + 1, DOT_W - 2, DOT_H - 2, TFT_WHITE);
    }
  }
  M5Cardputer.Display.endWrite();
}

// ── MODE 12: Inverse Bars ─────────────────────────────────────────────────────
void drawInverseBars() {
  M5Cardputer.Display.startWrite();
  for (int i = 0; i < NUM_BARS; i++) {
    int      x       = i * BAR_W;
    int      filledH = (int)(barH[i] * BAR_MAX_H);
    int      emptyH  = BAR_MAX_H - filledH;
    uint16_t col     = rainbowColor(i);

    // Rainbow background (empty space above bar)
    if (emptyH > 0) {
      M5Cardputer.Display.fillRect(x, 0, BAR_W - BAR_GAP, emptyH, col);
    }

    // Black bar
    if (filledH > 0) {
      M5Cardputer.Display.fillRect(x, emptyH, BAR_W - BAR_GAP, filledH, TFT_BLACK);
    }

    // White peak dot
    if (peakH[i] > 0.01f) {
      int peakY = SCREEN_H - 1 - (int)(peakH[i] * BAR_MAX_H);
      M5Cardputer.Display.drawFastHLine(x, peakY, BAR_W - BAR_GAP, TFT_WHITE);
    }
  }
  M5Cardputer.Display.endWrite();
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
  auto cfg = M5.config();
  cfg.internal_mic = true;
  cfg.internal_spk = true;
  M5Cardputer.begin(cfg);
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.fillScreen(TFT_BLACK);

  randomSeed(analogRead(0));

  led.begin();
  led.setBrightness(255);
  led.clear();
  led.show();

  M5Cardputer.Speaker.begin();
  M5Cardputer.Speaker.setVolume(200);

  auto mic_cfg = M5Cardputer.Mic.config();
  mic_cfg.sample_rate        = SAMPLE_RATE;
  mic_cfg.stereo             = false;
  mic_cfg.over_sampling      = 2;
  mic_cfg.noise_filter_level = 0;
  M5Cardputer.Mic.config(mic_cfg);
  M5Cardputer.Mic.begin();

  showSplash();

  memset(barH,      0, sizeof(barH));
  memset(peakH,     0, sizeof(peakH));
  memset(peakTimer, 0, sizeof(peakTimer));

  initMatrix();
  initLissajous();
  initFireworks();
  initPolygons();
  initRipples();
  wavePhase = 0.0f;

  M5Cardputer.Speaker.stop();
  M5Cardputer.Speaker.end();

  M5Cardputer.Display.fillScreen(TFT_BLACK);
}

// ── Main loop ─────────────────────────────────────────────────────────────────
void loop() {
  M5Cardputer.update();

  // ── Keyboard: arrow keys to switch mode ───────────────────────────────────
  if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
    auto keys = M5Cardputer.Keyboard.keysState();
    for (auto k : keys.word) {
      if (k == 0x2C) {
        currentMode = (currentMode - 1 + NUM_MODES) % NUM_MODES;
        modeNameTimer = millis();
        M5Cardputer.Display.fillScreen(TFT_BLACK);
        if (currentMode == 4)  initMatrix();
        if (currentMode == 5)  initLissajous();
        if (currentMode == 6)  initFireworks();
        if (currentMode == 9)  initPolygons();
        if (currentMode == 11) initRipples();
      }
      if (k == 0x2F) {
        currentMode = (currentMode + 1) % NUM_MODES;
        modeNameTimer = millis();
        M5Cardputer.Display.fillScreen(TFT_BLACK);
        if (currentMode == 4)  initMatrix();
        if (currentMode == 5)  initLissajous();
        if (currentMode == 6)  initFireworks();
        if (currentMode == 9)  initPolygons();
        if (currentMode == 11) initRipples();
      }
    }
  }

  // ── Audio ─────────────────────────────────────────────────────────────────
  if (!readAudio()) return;

  FFT.windowing(FFTWindow::Hann, FFTDirection::Forward);
  FFT.compute(FFTDirection::Forward);
  FFT.complexToMagnitude();

  float totalVolume = 0;

  for (int i = 0; i < NUM_BARS; i++) {
    int binStart = binForBar(i);
    int binEnd   = binForBar(i + 1);
    if (binEnd <= binStart) binEnd = binStart + 1;
    binEnd = min(binEnd, FFT_SIZE);

    double mag = 0;
    for (int b = binStart; b < binEnd; b++) mag += vReal[b];
    mag /= (binEnd - binStart);

    float level = max(0.0f, (float)mag - NOISE_FLOOR) / SCALE;
    level = min(level, 1.0f);
    totalVolume += level;

    if (level > barH[i])
      barH[i] += (level - barH[i]) * RISE;
    else
      barH[i] = max(0.0f, barH[i] - DECAY);

    if (level >= peakH[i]) {
      peakH[i]     = level;
      peakTimer[i] = PEAK_HLD;
    } else if (peakTimer[i] > 0) {
      peakTimer[i]--;
    } else {
      peakH[i] = max(0.0f, peakH[i] - PEAK_DEC);
    }
  }

  avgVolume = totalVolume / NUM_BARS;
  updateLED(min(avgVolume * 8.0f, 1.0f));

  // ── Draw ──────────────────────────────────────────────────────────────────
  float vol = min(avgVolume * 6.0f, 1.0f);
  switch (currentMode) {
    case 0:  drawBars();                              break;
    case 1:  drawWaveform();                          break;
    case 2:  drawRadial();                            break;
    case 3:  drawDotMatrix();                         break;
    case 4:  updateMatrix(vol); drawMatrix(vol);      break;
    case 5:  updateLissajous(avgVolume);
             drawLissajous();                         break;
    case 6:  updateFireworks(vol); drawFireworks();   break;
    case 7:  drawMirror();                            break;
    case 8:  drawKaleidoscope(vol);                   break;
    case 9:  updatePolygons(vol); drawPolygons();     break;
    case 10: drawWaveGrid(vol, avgVolume);            break;
    case 11: updateRipples(vol); drawRipples();       break;
    case 12: drawInverseBars();                       break;
  }

  drawModeName();
}
