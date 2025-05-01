#include "rm67162.h"
#include <TFT_eSPI.h>    
#include "true_color.h"
#include <AccelStepper.h>
#include <math.h>

// ——— Display size ———
#define WIDTH   536
#define HEIGHT  240

// ——— Stepper pins & motion ———
#define STEP_PIN        1
#define DIR_PIN         2
#define ENABLE_PIN      3
#define STEPS_PER_REV   6400           // 200 full × 32 µ-steps
#define TWO_REVS        (2 * STEPS_PER_REV)
#define SCREEN_INTERVAL 50             // ms between updates

// ——— Graphics objects ———
// FIXED: declare tft as an object, not a function
TFT_eSPI    tft;  
TFT_eSprite spr(&tft);
AccelStepper stepper(AccelStepper::DRIVER, STEP_PIN, DIR_PIN);

// ——— Gauge geometry ———
int   gaugeX, gaugeY, gaugeR, gaugeREff;
const int   DOT_R     = 5;           
const float DEG2RAD   = PI / 180.0f; 

// ——— State machine ———
enum MoveState { FORWARD, PAUSE1, BACKWARD, PAUSE2 };
MoveState      moveState    = FORWARD;
unsigned long  pauseStart   = 0;
unsigned long  lastScreenMs = 0;
long           lastPosShown = LONG_MIN;
long           segmentStart = 0;
int            lastAngle    = -1;

void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);

  // initialize the AMOLED via rm67162 driver
  rm67162_init();
  lcd_setRotation(1);

  // prepare full-screen sprite
  spr.createSprite(WIDTH, HEIGHT);
  spr.setSwapBytes(1);
  spr.fillSprite(TFT_BLACK);

  // static UI text
  spr.setTextSize(1);
  spr.setTextFont(4);
  spr.setTextColor(TFT_CYAN);
  spr.setCursor(12, 10);
  spr.print("Stepper Demo");

  spr.setTextColor(TFT_WHITE);
  spr.setCursor(12, 35);
  spr.print("Position:");
  spr.setCursor(12, 55);
  spr.setTextFont(4);
  spr.setTextSize(2);
  spr.print("0.00 rev");

  // reposition & resize gauge:
  const int topMargin   = 60;
  const int rightFactor = 8;        // out of 10 → 80% to the right
  gaugeX = WIDTH * rightFactor / 10;
  gaugeY = (HEIGHT + topMargin) / 2;
  gaugeR = min(gaugeX, gaugeY - topMargin) - 10;
  if (gaugeR < 20) gaugeR = 20;
  gaugeREff = gaugeR - DOT_R;       // keep the dot inside the border

  // draw gauge background & border once
  spr.fillCircle(gaugeX, gaugeY, gaugeR, TFT_DARKGREY);
  spr.drawCircle(gaugeX, gaugeY, gaugeR, TFT_WHITE);

  // push initial frame
  lcd_PushColors(0, 0, WIDTH, HEIGHT, (uint16_t*)spr.getPointer());

  // stepper enable & tuning
  pinMode(ENABLE_PIN, OUTPUT);
  digitalWrite(ENABLE_PIN, LOW);
  stepper.setMaxSpeed(20000);
  stepper.setAcceleration(20000);

  // start first forward move
  segmentStart = stepper.currentPosition();
  stepper.moveTo(TWO_REVS);
}

void loop() {
  unsigned long now = millis();

  // state machine
  switch (moveState) {
    case FORWARD:
      if (stepper.distanceToGo()) stepper.run();
      else { pauseStart = now; moveState = PAUSE1; }
      break;
    case PAUSE1:
      if (now - pauseStart >= 1000) {
        segmentStart = stepper.currentPosition();
        stepper.moveTo(-TWO_REVS);
        moveState = BACKWARD;
      }
      break;
    case BACKWARD:
      if (stepper.distanceToGo()) stepper.run();
      else { pauseStart = now; moveState = PAUSE2; }
      break;
    case PAUSE2:
      if (now - pauseStart >= 1000) {
        segmentStart = stepper.currentPosition();
        stepper.moveTo(TWO_REVS);
        moveState = FORWARD;
      }
      break;
  }

  // update display & gauge when needed
  long pos = stepper.currentPosition();
  if (pos != lastPosShown && now - lastScreenMs >= SCREEN_INTERVAL) {
    lastScreenMs  = now;
    lastPosShown  = pos;

    // erase the old dot inside the gauge only
    if (lastAngle >= 0) {
      float oldRad = (lastAngle - 90) * DEG2RAD;
      int   ox     = gaugeX + cos(oldRad) * gaugeREff;
      int   oy     = gaugeY + sin(oldRad) * gaugeREff;
      spr.fillCircle(ox, oy, DOT_R, TFT_DARKGREY);
    }

    // redraw numeric readout
    spr.fillRect(12, 55, 200, 40, TFT_BLACK);
    spr.setTextFont(4);
    spr.setTextSize(2);
    spr.setTextColor(TFT_WHITE);
    spr.setCursor(12, 55);
    float revs = float(pos) / STEPS_PER_REV;
    spr.print(revs, 2);
    spr.print(" rev");

    // compute & draw the new dot inside the gauge border
    long rel = (pos - segmentStart) % TWO_REVS;
    if (rel < 0) rel += TWO_REVS;
    float frac = float(rel) / TWO_REVS;
    int   angleDeg = int(frac * 360.0f + 0.5f);
    float rad     = (angleDeg - 90) * DEG2RAD;
    int   nx      = gaugeX + cos(rad) * gaugeREff;
    int   ny      = gaugeY + sin(rad) * gaugeREff;
    uint16_t col  = (moveState == FORWARD ? TFT_GREEN : TFT_RED);
    spr.fillCircle(nx, ny, DOT_R, col);
    lastAngle = angleDeg;

    // push updated frame
    lcd_PushColors(0, 0, WIDTH, HEIGHT, (uint16_t*)spr.getPointer());
  }
}
