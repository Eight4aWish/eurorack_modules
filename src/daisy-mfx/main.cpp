/*  DaisyMFX — Simplified (2 banks: Reverb x4, Delay x4)
    Kept: CV tap, wet fade on patch change, shimmer warm-up, OLED sleep, CV takeover.
    Removed: Banks C/D (Mods/Utils) and all related DSP/state/UI.
*/

#include <Arduino.h>
#include <DaisyDuino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "pins.h"
#include <math.h>
#include <cstring>

using namespace daisysp;
using namespace daisy;

// ---------------- UI / timing ----------------
#define UI_FRAME_MIN_MS_ACTIVE  33
#define UI_FRAME_MIN_MS_IDLE    150
#define UI_ACTIVE_BOOST_MS      1500
#define UI_IDLE_SLEEP_MS        15000
#define UI_CHANGE_EPS           0.005f
#define UI_LOW_CONTRAST         0x10
#define I2C_CLOCK_HZ            100000
#define BTN_DEBOUNCE_MS         25
#define BTN_LONG_MS             800
const float OUT_LPF_HZ = 14500.f;

// ================== Hardware ==================
#define OLED_W 128
#define OLED_H 64
#define OLED_ADDR 0x3C
Adafruit_SSD1306 oled(OLED_W, OLED_H, &Wire);
// Pins defined in include/daisy-mfx/pins.h

// ================== DSP / utils ==================
DaisyHardware hw;
float samplerate = 48000.f;
inline float clampf(float x, float a, float b){ return x < a ? a : (x > b ? b : x); }
inline float sin01(float ph){ return sinf(2.f * 3.14159265f * ph); }
inline float map_exp01(float x01, float minv, float maxv){
  x01 = clampf(x01, 0.f, 1.f); float lnmin = logf(minv), lnrange = logf(maxv) - lnmin;
  return expf(lnmin + x01 * lnrange);
}
inline float map_lin01(float x01, float minv, float maxv){
  x01 = clampf(x01, 0.f, 1.f); return minv + x01*(maxv - minv);
}
inline void onepole_lp(float xL, float xR, float a, float &yL, float &yR){ yL += a*(xL-yL); yR += a*(xR-yR); }

// ---- Reverb core (Bank A) ----
DSY_SDRAM_BSS static ReverbSc verb;
DSY_SDRAM_BSS static DelayLine<float, 12000> preL_A2, preR_A2;   // Plate predelay
DSY_SDRAM_BSS static DelayLine<float, 16000> preL_A3, preR_A3;   // Tank predelay
DSY_SDRAM_BSS static DelayLine<float, 1200>  a3mL, a3mR;         // Tank light modulation
float a3_phL = 0.f, a3_phR = 0.5f;
DSY_SDRAM_BSS static PitchShifter shifter;                       // Shimmer

// ---- Delays (Bank B) ----
DSY_SDRAM_BSS static DelayLine<float, 96000> dlyL, dlyR;         // up to ~2 s @ 48k safely
float tape_ph=0.f;
float fb_lpL=0.f, fb_lpR=0.f;

// ================== Output post-filters ==================
struct DcBlock { float R, x1, y1; inline float Process(float x){ float y=x-x1+R*y1; x1=x; y1=y; return y; } } dcL, dcR;
struct OnePoleLP { float a=0.f,y=0.f; inline void SetCutoff(float fc,float fs){ float alpha=1.f-expf(-2.f*3.14159265f*fc/fs); a=clampf(alpha,0.f,1.f);} inline float Process(float x){ y+=a*(x-y); return y; } } oplpL, oplpR;

// ================== State / UI ==================
enum Bank   { BANK_A, BANK_B };
enum PatchA { A1_CLASSIC, A2_PLATE, A3_TANK, A4_SHIMMER };
enum PatchB { B1_PING, B2_TAPE, B3_MULTITAP, B4_ECHOVERB };
enum UiLevel { LEVEL_BANK, LEVEL_PATCH };

UiLevel level; Bank bankSel; int patchIdx; Bank previewBank;
inline float readPotInv01(int pin){ return 1.f - (analogRead(pin)/65535.f); }
float P1=0, P2=0, P3=0;
inline float Adc01ToVin(float a01){ return (3.3f*a01 - 1.68f)/-0.33f; }   // your existing mapping
inline float cv_uni01(float v){ return clampf((v+5.f)*0.1f, 0.f, 1.f); }
float CV1_volts=0.f, CV2_volts=0.f, CV2_volts_raw=0.f;

struct CvTakeover { float eps_on, eps_off; bool cv_mode; bool update(float pot01){ if(!cv_mode && pot01<=eps_on) cv_mode=true; else if(cv_mode && pot01>=eps_off) cv_mode=false; return cv_mode; } } toP2, toP3;

uint32_t last_tap_ms = 0; float tap_delay_samps = 24000.f; bool tap_gate=false;
static bool g_have_tap = false; // explicit tap-tempo armed
const float TAP_HIGH=1.5f, TAP_LOW=1.0f;

bool g_oled_awake = true; uint32_t g_last_user_ms = 0;
inline void OledSleep(){ Wire.setClock(100000); if(!g_oled_awake) return; oled.ssd1306_command(SSD1306_DISPLAYOFF); g_oled_awake=false; }
inline void OledWake(){ Wire.setClock(400000); if(g_oled_awake) return; oled.ssd1306_command(SSD1306_DISPLAYON); g_oled_awake=true; }

// --- Patch/blast protection ---
static int g_patch_fade_samps = 0;
static int g_shimmer_warm_samps = 0;
static const int PATCH_FADE_SAMPS   = 2048;  // ~43 ms @ 48k
static const int SHIMMER_WARM_SAMPS = 8192;  // ~170 ms @ 48k

static void ResetFxForBankPatch(){
  verb.Init(samplerate);
  dlyL.Reset(); dlyR.Reset();
  fb_lpL = fb_lpR = 0.f;
  a3_phL = 0.f; a3_phR = 0.5f; tape_ph = 0.f;

  // Clear short mod/predelay lines
  for(int k=0;k<16000;++k){ if(k<12000){ preL_A2.Write(0.f); preR_A2.Write(0.f); } if(k<16000){ preL_A3.Write(0.f); preR_A3.Write(0.f); } if(k<1200){ a3mL.Write(0.f); a3mR.Write(0.f); } }

  // Arm fades
  g_patch_fade_samps = PATCH_FADE_SAMPS;
  if(bankSel == BANK_A && patchIdx == A4_SHIMMER){
    g_shimmer_warm_samps = SHIMMER_WARM_SAMPS;
    shifter.Init(samplerate);
    shifter.SetTransposition(12.f);
  } else {
    g_shimmer_warm_samps = 0;
  }
}

// ================== AUDIO CALLBACK ==================
void AudioCallback(float **in, float **out, size_t size)
{
  for(size_t i=0;i<size;i++)
  {
    float dryL=in[0][i], dryR=in[1][i];

    // Per-sample patch fade
    float patch_fade = 1.f;
    if(g_patch_fade_samps > 0){
      patch_fade = 1.f - (g_patch_fade_samps / (float)PATCH_FADE_SAMPS);
      g_patch_fade_samps--;
    }

    float wetL=dryL, wetR=dryR;
    float P2ctrl = toP2.update(P2) ? cv_uni01(CV1_volts) : P2;
    float P3ctrl = toP3.update(P3) ? cv_uni01(CV2_volts) : P3;

    if(bankSel==BANK_A){
      switch(static_cast<PatchA>(patchIdx)){
        case A1_CLASSIC:{
          float decay=map_lin01(P2ctrl,0.70f,0.98f);
          float tone =map_lin01(P3ctrl,1000.f,18000.f);
          verb.SetFeedback(decay); verb.SetLpFreq(tone);
          verb.Process(dryL,dryR,&wetL,&wetR);
        }break;

        case A2_PLATE:{
          static float pre=0.f;
          float pre_ms=map_exp01(P2ctrl,10.f,80.f);
          float target=clampf(pre_ms*0.001f*samplerate,1.f,11999.f);
          fonepole(pre,target,0.0015f);
          preL_A2.SetDelay(pre); preR_A2.SetDelay(pre);
          float inL=preL_A2.Read(), inR=preR_A2.Read();
          preL_A2.Write(dryL); preR_A2.Write(dryR);
          float tone=map_lin01(P3ctrl,12000.f,18000.f);
          float decay=map_lin01(0.6f+0.4f*P2ctrl,0.75f,0.97f);
          verb.SetFeedback(decay); verb.SetLpFreq(tone);
          verb.Process(inL,inR,&wetL,&wetR);
        }break;

        case A3_TANK:{
          static float pre=0.f;
          float pre_ms=map_exp01(P2ctrl,30.f,200.f);
          float target=clampf(pre_ms*0.001f*samplerate,1.f,15999.f);
          fonepole(pre,target,0.0015f);
          preL_A3.SetDelay(pre); preR_A3.SetDelay(pre);
          float inL=preL_A3.Read(), inR=preR_A3.Read();
          preL_A3.Write(dryL); preR_A3.Write(dryR);
          float rate=0.15f/samplerate; a3_phL+=rate; if(a3_phL>=1.f) a3_phL-=1.f;
          a3_phR+=rate; if(a3_phR>=1.f) a3_phR-=1.f;
          a3mL.SetDelay(clampf(samplerate*(0.006f+0.002f*sin01(a3_phL)),4.f,1190.f));
          a3mR.SetDelay(clampf(samplerate*(0.006f+0.002f*sin01(a3_phR+0.3f)),4.f,1190.f));
          float mmL=a3mL.Read(); a3mL.Write(inL); inL=mmL;
          float mmR=a3mR.Read(); a3mR.Write(inR); inR=mmR;
          float decay=map_lin01(0.5f+0.5f*P2ctrl,0.85f,0.985f);
          float tone =map_lin01(1.f-P3ctrl,3000.f,12000.f);
          verb.SetFeedback(decay); verb.SetLpFreq(tone);
          verb.Process(inL,inR,&wetL,&wetR);
        }break;

        case A4_SHIMMER:{
          float decay=map_lin01(P2ctrl,0.75f,0.98f);
          float tone =map_lin01(P3ctrl,1500.f,16000.f);
          verb.SetFeedback(decay); verb.SetLpFreq(tone);
          float vL,vR; verb.Process(dryL,dryR,&vL,&vR);
          float shimmer_warm = 1.f;
          if(g_shimmer_warm_samps > 0){
            shimmer_warm = 1.f - (g_shimmer_warm_samps / (float)SHIMMER_WARM_SAMPS);
            g_shimmer_warm_samps--;
          }
          float wetMono=0.5f*(vL+vR);
          float shim = shifter.Process(wetMono) * clampf(P3ctrl,0.f,1.f);
          shim *= shimmer_warm;
          wetL = vL + shim * 0.7f;
          wetR = vR + shim * 0.7f;
        }break;
      }
    }
    else // BANK_B
    {
      switch(static_cast<PatchB>(patchIdx)){
        case B1_PING:{
          static float tS=24000.f; static bool init=false;
          float targ = g_have_tap ? clampf(tap_delay_samps,10.f,95990.f)
                                  : clampf(map_exp01(P2ctrl,10.f,800.f)*0.001f*samplerate,10.f,95990.f);
          if(!init){ tS=targ; init=true; }
          fonepole(tS,targ,0.0015f);
          dlyL.SetDelay(tS); dlyR.SetDelay(tS);
          float dl=dlyL.Read(), dr=dlyR.Read();
          float fb=clampf(P3ctrl,0.f,0.90f);
          dlyL.Write(dryL + dr*fb);
          dlyR.Write(dryR + dl*fb);
          wetL=dl; wetR=dr;
        }break;

        case B2_TAPE:{
          static float tS=24000.f; static bool init=false;
          float base_ms=map_exp01(P2ctrl,20.f,800.f);
          tape_ph+=0.6f/samplerate; if(tape_ph>=1.f)tape_ph-=1.f;
          float mod=1.f+0.0025f*sin01(tape_ph);
          float targ=clampf(base_ms*mod*0.001f*samplerate,10.f,95990.f);
          if(!init){ tS=targ; init=true; }
          fonepole(tS,targ,0.0015f);
          dlyL.SetDelay(tS); dlyR.SetDelay(tS);
          float dl=dlyL.Read(), dr=dlyR.Read();
          float fbAmt=clampf(P3ctrl,0.f,0.90f);
          float tone_a=map_lin01(fbAmt,0.10f,0.35f);
          float fbL=dl, fbR=dr; onepole_lp(fbL,fbR,tone_a,fb_lpL,fb_lpR);
          dlyL.Write(dryL + fb_lpL*fbAmt);
          dlyR.Write(dryR + fb_lpR*fbAmt);
          wetL=dl; wetR=dr;
        }break;

        case B3_MULTITAP:{
          static float baseS=24000.f; static bool init=false;
          float base_ms=map_exp01(P2ctrl,60.f,900.f);
          float targ=clampf(base_ms*0.001f*samplerate,10.f,63990.f);
          if(!init){ baseS=targ; init=true; }
          fonepole(baseS,targ,0.0015f);

          // write once, read multiple taps
          dlyL.SetDelay(10.f); dlyR.SetDelay(10.f);
          dlyL.Write(dryL); dlyR.Write(dryR);

          float taps[3]={0.5f*baseS,1.0f*baseS,1.5f*baseS};
          float sumL=0.f,sumR=0.f;
          for(int t=0;t<3;t++){
            float d=clampf(taps[t],10.f,95990.f);
            dlyL.SetDelay(d); dlyR.SetDelay(d);
            float xL=dlyL.Read(), xR=dlyR.Read();
            float pan=(t-1)*clampf(P3ctrl,0.f,1.f); // width
            float g=clampf(1.f-0.2f*t,0.5f,1.f);
            float l=(pan<=0.f)?1.f:(1.f-pan);
            float r=(pan>=0.f)?1.f:(1.f+pan);
            sumL+=xL*g*l; sumR+=xR*g*r;
          }
          wetL=sumL; wetR=sumR;
        }break;

        case B4_ECHOVERB:{
          static float tS=24000.f; static bool init=false;
          float targ = g_have_tap ? clampf(tap_delay_samps,10.f,95990.f)
                                  : clampf(map_exp01(P2ctrl,30.f,900.f)*0.001f*samplerate,10.f,95990.f);
          if(!init){ tS=targ; init=true; }
          fonepole(tS,targ,0.0015f);
          dlyL.SetDelay(tS); dlyR.SetDelay(tS);
          float dl=dlyL.Read(), dr=dlyR.Read();
          float fb01=clampf(P3ctrl,0.f,1.f);
          float fb=clampf(fb01,0.f,0.90f);
          float tone_a=map_lin01(fb01,0.10f,0.35f);
          float fbL=dl, fbR=dr; onepole_lp(fbL,fbR,tone_a,fb_lpL,fb_lpR);
          dlyL.Write(dryL+fb_lpL*fb); dlyR.Write(dryR+fb_lpR*fb);

          float vL,vR;
          verb.SetFeedback(0.88f);
          verb.SetLpFreq(map_lin01(1.f-fb01,5000.f,14000.f));
          verb.Process(dl*map_lin01(fb01,0.20f,0.60f),
                       dr*map_lin01(fb01,0.20f,0.60f), &vL,&vR);
          wetL=dl+vL; wetR=dr+vR;
        }break;
      }
    }

    // Apply universal patch fade
    wetL *= patch_fade;
    wetR *= patch_fade;

    // Final mix + output filtering
    float outL=(1.f-P1)*dryL + P1*wetL;
    float outR=(1.f-P1)*dryR + P1*wetR;
    outL = dcL.Process(outL); outR = dcR.Process(outR);
    outL = oplpL.Process(outL); outR = oplpR.Process(outR);
    outL = clampf(outL,-1.2f,1.2f); outR = clampf(outR,-1.2f,1.2f);
    out[0][i]=outL; out[1][i]=outR;
  }
}

// ================== OLED (helpers) ==================
#include "eurorack_ui/OledHelpers.hpp"
namespace ui {
static void printClipped(int x,int y,int w,const char* s){ eurorack_ui::printClipped(oled, x, y, w, s); }
static void printClippedBold(int x,int y,int w,const char* s,bool bold){ eurorack_ui::printClippedBold(oled, x, y, w, s, bold); }
static void drawBar(int x,int y,int w,int h,float amt,bool invert=false){ eurorack_ui::drawBar(oled, x, y, w, h, amt, invert); }

// Label-only printer (no numerals)
static void printLabelOnly(int x,int y,int w,const char* label){ eurorack_ui::printLabelOnly(oled, x, y, w, label); }

static const char* GetBankTitle(Bank b){
  switch(b){
    case BANK_A: return "A: Revb";
    case BANK_B: return "B: Dely";
  }
  return "(?)";
}

static const char* patchTitleShort(){
  if(bankSel==BANK_A){ switch(static_cast<PatchA>(patchIdx)){
    case A1_CLASSIC:   return "A1 Classic";
    case A2_PLATE:     return "A2 Plate";
    case A3_TANK:      return "A3 Tank";
    case A4_SHIMMER:   return "A4 Shimmer"; } }
  else { switch(static_cast<PatchB>(patchIdx)){
    case B1_PING:      return "B1 Ping";
    case B2_TAPE:      return "B2 Tape";
    case B3_MULTITAP:  return "B3 MultiTap";
    case B4_ECHOVERB:  return "B4 EchoVerb"; } }
  return "";
}

// ---- 3–4 char parameter labels ----
static const char* p1Label() { return "Mix"; }
static const char* p2Label() {
  if(bankSel==BANK_A){
    switch(static_cast<PatchA>(patchIdx)){ case A2_PLATE: case A3_TANK: return "PreD"; default: return "Decy"; }
  } else {
    return "Time";
  }
}
static const char* p3Label() {
  if(bankSel==BANK_A){
    return (patchIdx==A4_SHIMMER) ? "Shim" : "Tone";
  } else {
    return (patchIdx==B4_ECHOVERB) ? "Macr" : "Fdbk";
  }
}

static void drawBankMenu(Bank highlight){
  oled.clearDisplay(); oled.setTextSize(1); oled.setTextColor(SSD1306_WHITE);
  oled.fillRect(0,0,OLED_W,12,SSD1306_WHITE); oled.setTextColor(SSD1306_BLACK);
  ui::printClipped(2,2,OLED_W-4,"Bank Sel");
  oled.setTextColor(SSD1306_WHITE);
  int cellW=OLED_W/2, cellH=(OLED_H-12)/2; int xL=0,xR=cellW; int y1=12,y2=12+cellH;
  auto cell=[&](int x,int y,const char* label,bool hilite){ oled.drawRect(x,y,cellW,cellH,SSD1306_WHITE); int tx=x+6, ty=y+cellH/2-4; ui::printClippedBold(tx,ty,cellW-12,label,hilite); };
  cell(xL,y1,GetBankTitle(BANK_A),highlight==BANK_A);
  cell(xR,y1,GetBankTitle(BANK_B),highlight==BANK_B);
  // bottom row now intentionally empty
  oled.display();
}

static void drawPatchUi(bool btn_pressed,bool /*showTapFlag*/){
  oled.clearDisplay(); oled.setTextSize(1);
  oled.fillRect(0,0,OLED_W,12,SSD1306_WHITE);
  oled.setTextColor(SSD1306_BLACK);
  ui::printClipped(2,2,96,patchTitleShort());
  oled.setTextColor(SSD1306_WHITE);

  const int yRow1=14,yRow2=38; const int cellW_L=60,cellW_R=68,cellH=22;

  // Button cell
  oled.drawRect(0,yRow1,cellW_L,cellH,SSD1306_WHITE);
  ui::printClipped(4,yRow1+2,cellW_L-8,"Btn");
  ui::drawBar(4,yRow1+cellH-9,cellW_L-8,7,btn_pressed?1.f:0.f);

  // P1
  oled.drawRect(cellW_L,yRow1,cellW_R,cellH,SSD1306_WHITE);
  ui::printLabelOnly(cellW_L+4,yRow1+2,cellW_R-8,ui::p1Label());
  ui::drawBar(cellW_L+4,yRow1+cellH-9,cellW_R-8,7,P1);

  // P2
  oled.drawRect(0,yRow2,cellW_L,cellH,SSD1306_WHITE);
  ui::printLabelOnly(4,yRow2+2,cellW_L-8,ui::p2Label());
  ui::drawBar(4,yRow2+cellH-9,cellW_L-8,7,(toP2.cv_mode?cv_uni01(CV1_volts):P2),toP2.cv_mode);

  // P3
  oled.drawRect(cellW_L,yRow2,cellW_R,cellH,SSD1306_WHITE);
  ui::printLabelOnly(cellW_L+4,yRow2+2,cellW_R-8,ui::p3Label());
  ui::drawBar(cellW_L+4,yRow2+cellH-9,cellW_R-8,7,(toP3.cv_mode?cv_uni01(CV2_volts):P3),toP3.cv_mode);

  oled.display();
}
} // namespace ui

// ================== SETUP / LOOP ==================
void setup(){
  Serial.begin(115200); uint32_t t0=millis(); while(!Serial && (millis()-t0)<1500) {}
  hw = DAISY.init(DAISY_PATCH, AUDIO_SR_48K); samplerate = DAISY.get_samplerate(); analogReadResolution(16);
  Wire.setSCL(PIN_SCL); Wire.setSDA(PIN_SDA); Wire.begin(); Wire.setClock(I2C_CLOCK_HZ);
  pinMode(PIN_BTN,INPUT_PULLUP); pinMode(PIN_LED,OUTPUT);
  if(!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)){ for(;;){ digitalWrite(PIN_LED,!digitalRead(PIN_LED)); delay(150);} }
  oled.dim(true); oled.ssd1306_command(SSD1306_SETCONTRAST); oled.ssd1306_command(UI_LOW_CONTRAST);

  verb.Init(samplerate);
  preL_A2.Init(); preR_A2.Init();
  preL_A3.Init(); preR_A3.Init();
  a3mL.Init(); a3mR.Init();
  shifter.Init(samplerate); shifter.SetTransposition(12.f);

  dlyL.Init(); dlyR.Init();

  oplpL.SetCutoff(OUT_LPF_HZ,samplerate); oplpR.SetCutoff(OUT_LPF_HZ,samplerate);

  // runtime inits
  a3_phR=0.5f;
  toP2.eps_on=0.015f; toP2.eps_off=0.030f; toP2.cv_mode=false;
  toP3.eps_on=0.015f; toP3.eps_off=0.030f; toP3.cv_mode=false;
  dcL.R=dcR.R=0.995f; dcL.x1=dcL.y1=dcR.x1=dcR.y1=0.f;
  level=LEVEL_PATCH; bankSel=BANK_A; patchIdx=0; previewBank=BANK_A;

  ui::drawBankMenu(previewBank); g_last_user_ms = millis();
  DAISY.begin(AudioCallback);
}

void loop(){
  digitalWrite(PIN_LED,(millis()/500)%2);

  // Pots (smoothed)
  P1=0.98f*P1+0.02f*readPotInv01(PIN_POT1);
  P2=0.98f*P2+0.02f*readPotInv01(PIN_POT2);
  P3=0.98f*P3+0.02f*readPotInv01(PIN_POT3);

  // CV (smoothed) + raw
  float cv1_a01=analogRead(PIN_CV1)/65535.f, cv2_a01=analogRead(PIN_CV2)/65535.f;
  float cv1_v=Adc01ToVin(cv1_a01), cv2_v=Adc01ToVin(cv2_a01);
  CV1_volts=0.95f*CV1_volts+0.05f*cv1_v; CV2_volts=0.90f*CV2_volts+0.10f*cv2_v; CV2_volts_raw=cv2_v;

  uint32_t nowTicks=daisy::System::GetNow(); uint32_t ms=millis();

  // Tap-tempo on CV2 (Delays only) — explicit arm + auto hand-back
  if(bankSel==BANK_B){
    if(!tap_gate && CV2_volts_raw>=TAP_HIGH){
      tap_gate=true;
      uint32_t dt=nowTicks-last_tap_ms;
      if(dt>50 && dt<2000){
        tap_delay_samps=(dt/1000.f)*samplerate;
        g_have_tap = true;
      }
      last_tap_ms=nowTicks;
    } else if(tap_gate && CV2_volts_raw<=TAP_LOW){
      tap_gate=false;
    }
    if(g_have_tap && (nowTicks - last_tap_ms) > 1800){
      g_have_tap = false;
    }
  } else {
    g_have_tap = false; // leaving delay bank cancels tap
  }

  // Button (debounced, short/long)
  static bool btn_state=false, btn_last=false, btn_long_fired=false;
  static uint32_t btn_last_change_ms=0, btn_press_start_ms=0;
  bool raw_pressed=(digitalRead(PIN_BTN)==LOW);
  if(raw_pressed!=btn_last){ btn_last=raw_pressed; btn_last_change_ms=ms; }
  if(ms-btn_last_change_ms>=BTN_DEBOUNCE_MS && btn_state!=raw_pressed){
    btn_state=raw_pressed;
    if(btn_state){ btn_press_start_ms=ms; btn_long_fired=false; g_last_user_ms=ms; OledWake(); }
    else if(!btn_long_fired){
      if(level==LEVEL_BANK){ previewBank = (previewBank==BANK_A) ? BANK_B : BANK_A; }
      else { patchIdx=(patchIdx+1)%4; ResetFxForBankPatch(); }
      g_last_user_ms=ms; OledWake();
    }
  }
  if(btn_state && !btn_long_fired && (ms-btn_press_start_ms>=BTN_LONG_MS)){
    btn_long_fired=true;
    if(level==LEVEL_BANK){ bankSel=previewBank; patchIdx=0; ResetFxForBankPatch(); level=LEVEL_PATCH; }
    else { previewBank=bankSel; level=LEVEL_BANK; }
    g_last_user_ms=ms; OledWake();
  }

  // Event-driven UI
  static uint32_t last_draw=0;
  static float p1_last=-1.f,p2_last=-1.f,p3_last=-1.f;
  static int patch_last=-1; static Bank bank_last=(Bank)255, preview_last=(Bank)255;
  static UiLevel level_last=(UiLevel)255;
  bool showTap=(bankSel==BANK_B)&&((nowTicks-last_tap_ms)<200);

  bool user_interaction = btn_state
    || fabsf(P1-p1_last)>UI_CHANGE_EPS || fabsf(P2-p2_last)>UI_CHANGE_EPS || fabsf(P3-p3_last)>UI_CHANGE_EPS
    || (patchIdx!=patch_last) || (bankSel!=bank_last) || (previewBank!=preview_last) || (level!=level_last) || showTap;

  if(user_interaction){ g_last_user_ms=ms; OledWake(); }
  if(ms-g_last_user_ms>UI_IDLE_SLEEP_MS){ OledSleep(); }

  uint32_t min_frame = (ms-g_last_user_ms)<UI_ACTIVE_BOOST_MS ? UI_FRAME_MIN_MS_ACTIVE : UI_FRAME_MIN_MS_IDLE;
  if(g_oled_awake && (ms-last_draw)>=min_frame && user_interaction){
    last_draw=ms; p1_last=P1; p2_last=P2; p3_last=P3; patch_last=patchIdx; bank_last=bankSel; preview_last=previewBank; level_last=level;
    if(level==LEVEL_BANK) ui::drawBankMenu(previewBank); else ui::drawPatchUi(btn_state,showTap);
  }
}