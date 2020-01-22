#include "Wire.h"
#include "OPL2.h"
#include "instruments.h"
#include "Adafruit_MCP23017.h"
#include <Arduino.h>
#include <EEPROM.h>

#define DEBUG 1
#define CHIP1ADDR 0x21
#define CHIP2ADDR 0x20
// Rows are connected to ground
#define LED_ROWS 2
// Cols are connected to 5V
#define LED_COLS 3

#define ENCODER_TEMPO 0
#define ENCODER_PITCH 1
#define ENCODER_VOLUME 2

Adafruit_MCP23017 mcp;
Adafruit_MCP23017 mcp2;
Adafruit_MCP23017 mcp3;

OPL2 opl2;

typedef struct  {
  int value;
  int max_value;
  int min_value;
  int step;
} Encoder;




Encoder encoders[] = {
  // Tempo
  {100, 600, 20, 10},
  // Number of steps
  {1, 32, 0, 1},
  // Pitch
  {0, 1023, 0, 1},
  // Voice select
  {0, 10, 0, 1},
  // Decay
  {0, 15, 0, 1},
  // Sustain
  {0, 15, 0, 1},
  // Release
  {0, 15, 0, 1},
  
  // Volume
  {0, 15, 0, 1},
  // Waveform
  {0, 2, 0, 1},
  // Feedback
  {0, 2, 0, 1},
  // Voice select
  {0, 11, 0, 1},
  // Step select
  {0, 31, 0, 1},
  // Amplitude Modulation
  {0, 2, 0, 1},
  // Vibrato
  {0, 2, 0, 1},
};

int i = 0;

int note = 1;
int octave = 1;
int tempo = 100;
int nSteps = 32;
bool tempoEncoder = true;

int selectedChannel = 0;

// Which note is selected.
int selectedStep = 0;


volatile boolean awakenByInterrupt = false;
volatile boolean encoderInterrupt = false;
volatile boolean switchInterrupt = false;

int leds[LED_ROWS * LED_COLS] = {false};

typedef struct  {
  uint8_t attack;
  uint8_t decay;
  uint8_t sustain;
  uint8_t release;
  uint8_t waveform;
  uint8_t volume;
  uint8_t feedback;
  uint8_t vibrato;
  uint8_t tremolo;
  uint8_t m_attack;
  uint8_t m_decay;
  uint8_t m_sustain;
  uint8_t m_release;
  uint8_t m_volume;
  uint8_t m_waveform;
  uint8_t m_mult;
} Channel;


Channel channels[6] = {
    {14, 6, 6, 9, 0, 15, 1, 1, 1, 10, 2, 6, 9, 15, 3, 1},
    {14, 6, 6, 9, 0, 15, 1, 1, 1, 10, 2, 6, 9, 15, 3, 1},
    {14, 6, 6, 9, 0, 15, 1, 1, 1, 10, 2, 6, 9, 15, 3, 1},
    {14, 6, 6, 9, 0, 15, 1, 1, 1, 10, 2, 6, 9, 15, 3, 1},
    {14, 6, 6, 9, 0, 15, 1, 1, 1, 10, 2, 6, 9, 15, 3, 1},
    {14, 6, 6, 9, 0, 15, 1, 1, 1, 10, 2, 6, 9, 15, 3, 1},
};

// Array of ints.
// 32 for each voice.
// First 6 are melodic voices, last 5 are percussion voices
// For melodic voices, the value is the pitch of that note.
// For percussion voices a value other than 0 means play.
int playNotes[11][32];

int pinInt;
int switchInt;

void pinChanged() {
  encoderInterrupt = true;
}

void switchChanged() {
  switchInterrupt = true;
}

ISR(TIMER1_COMPA_vect){//timer1 interrupt plays music
  awakenByInterrupt = true;
}

void setup() {
  Serial.begin(9600);

  mcp2.begin(1);
  mcp2.pinMode(8, OUTPUT);

  mcp3.begin(2);
  mcp3.pinMode(0, OUTPUT);
  mcp3.pinMode(1, OUTPUT);
  mcp3.pinMode(2, OUTPUT);
  mcp3.pinMode(3, OUTPUT);
  mcp3.pinMode(4, OUTPUT);
    
  opl2.init();

  // Set percussion mode and load instruments.
  opl2.setPercussion(true);
  opl2.setInstrument(0, INSTRUMENT_BDRUM1);
  opl2.setInstrument(0, INSTRUMENT_RKSNARE1);
  opl2.setInstrument(0, INSTRUMENT_TOM2);
  opl2.setInstrument(0, INSTRUMENT_CYMBAL1);
  opl2.setInstrument(0, INSTRUMENT_HIHAT2);

  opl2.setWaveFormSelect(true);

  //opl2.setDeepTremolo(true);
  // Set octave and frequency for bass drum.
  opl2.setBlock(6, 4);
  opl2.setFNumber(6, opl2.getNoteFNumber(NOTE_C));

  // Set octave and frequency for snare drum and hi-hat.
  opl2.setBlock(7, 3);
  opl2.setFNumber(7, opl2.getNoteFNumber(NOTE_C));
  // Set low volume on hi-hat
  opl2.setVolume(7, OPERATOR1, 16);

  // Set octave and frequency for tom tom and cymbal.
  opl2.setBlock(8, 3);
  opl2.setFNumber(8, opl2.getNoteFNumber(NOTE_A));
  Serial.begin(9600);

  //set timer1 interrupt at 4Hz
  TCCR1A = 0;// set entire TCCR1A register to 0
  TCCR1B = 0;// same for TCCR1B
  TCNT1  = 0;//initialize counter value to 0
  // set compare match register for 16hz increments
  OCR1A = (int) (16000000 / (2 * 1024)) - 1; // (must be <65536)
  // turn on CTC mode
  TCCR1B |= (1 << WGM12);
  // Set CS12 and CS10 bits for 1024 prescaler
  TCCR1B |= (1 << CS12) | (1 << CS10);  
  // enable timer compare interrupt
  TIMSK1 |= (1 << OCIE1A);

  for (int m = 0; m < 32; m++) {
    playNotes[selectedChannel][m] = m % 2 == 0 ? 32 : 0;
    playNotes[1][m] = 40;
    playNotes[7][m] = m % 4 == 0 ? 1 : 0;
    playNotes[8][m] = (m + 1) % 4 == 0 ? 1 : 0;
  }

  pinMode(3, INPUT);
  pinMode(3, INPUT_PULLUP);
  pinMode(2, INPUT);
  pinMode(2, INPUT_PULLUP);

  mcp.begin();

  mcp.setupInterrupts(false, false, LOW);
  
  for(int i = 0; i <= 15;i++) {
    mcp.pinMode(i, INPUT);
    mcp.pullUp(i, HIGH);
  }

  for(int i = 0; i <= 7;i++) {
    mcp.setupInterruptPin(i, CHANGE); 
  }

  mcp2.setupInterrupts(false, false, LOW);
  mcp2.pinMode(0, INPUT);
  mcp2.pullUp(0, HIGH);
  mcp2.setupInterruptPin(0, CHANGE); 

  pinInt = digitalPinToInterrupt(3);
  switchInt = digitalPinToInterrupt(2);
  attachInterrupt(pinInt, pinChanged, FALLING);
  attachInterrupt(switchInt, switchChanged, FALLING);
  pinMode(3, INPUT);
  pinMode(3, INPUT_PULLUP);
  pinMode(2, INPUT);
  pinMode(2, INPUT_PULLUP);
}


void play();

void play() {
  if (i % 4 == 0) {
    mcp2.digitalWrite(8, HIGH);
  }

  // play melodic instruments if required
  for(int j = 0; j<= 6; j++) {
    if (playNotes[j][i]) {
      octave = playNotes[j][i] >> 4;
      note = (int)((float)(playNotes[j][i] & 0xf) / 16.0 * 12.0);
      opl2.playNote(j, octave, note);
    }
  }

  // play drums instruments if required
  opl2.setDrums(playNotes[6][i], playNotes[7][i], playNotes[8][i], playNotes[9][i], playNotes[10][i]);

  // Flash tempo led.
  // Not sure if that will be necessary now.
  if ((i + 3) % 4 == 0) {
    mcp2.digitalWrite(8, LOW);
  }
  i++;
  if (i >= nSteps) {
    i = 0;
  }
  awakenByInterrupt = false;
}


int currentRow = 0;

int changeLedRow();
int changeLedRow() {
  // Switch rows

  for (int k = 0; k < LED_ROWS;k++) {
    if (k != currentRow) {
      mcp3.digitalWrite(k, HIGH);
    }
  }
  
  for (int j = 0; j < LED_COLS;j++) {
    mcp3.digitalWrite(LED_ROWS + j, leds[currentRow * LED_COLS + j]);
  }
  mcp3.digitalWrite(currentRow, LOW);
    
  currentRow++;
  if (currentRow >= LED_ROWS) {
    currentRow = 0;
  }
}

int ledOn(int ref, int level);
int ledOn (int ref, int level) {
  leds[ref] = true;
}
int ledOff (int ref);
int ledOff (int ref) {
  leds[ref] = false;
}

void saveChannel(int j);
void saveChannel(int j) {
  opl2.setFeedback(j, channels[j].feedback);

  //Can't remember what this is for.
  opl2.setSynthMode(j, false);

  //Add button to set Tremolo/vibrato
  opl2.setTremolo   (j, CARRIER, channels[j].tremolo > 0);
  opl2.setVibrato   (j, CARRIER, channels[j].vibrato > 0);

  //Set carrier multiplier to one because otherwise low notes are impossible
  opl2.setMultiplier(j, CARRIER, 0x01);
  
  // This is the way to adjust the modulator frequency. There is a button for that.
  opl2.setMultiplier(j, MODULATOR, channels[j].m_mult);
  // Add dial to set waveform
  opl2.setWaveForm(j, MODULATOR, channels[j].m_waveform);

  //Add button to set volume per channel
  opl2.setVolume(j, MODULATOR, 15 - channels[j].m_volume);
  opl2.setAttack    (j, MODULATOR, channels[j].m_attack);
  opl2.setDecay     (j, MODULATOR, channels[j].m_decay);
  opl2.setSustain   (j, MODULATOR, channels[j].m_sustain);
  opl2.setRelease   (j, MODULATOR, channels[j].m_release);
  
  //Add button to set volume per channel
  opl2.setVolume(j, CARRIER, 15 - channels[j].volume);
  opl2.setWaveForm(j, CARRIER, channels[j].waveform);
  opl2.setAttack    (j, CARRIER, channels[j].attack);
  opl2.setDecay     (j, CARRIER, channels[j].decay);
  opl2.setSustain   (j, CARRIER, channels[j].sustain);
  opl2.setRelease   (j, CARRIER, channels[j].release);
}

void loop() {
  if (awakenByInterrupt) {
    saveChannel(selectedChannel);
    saveChannel(1);
    play();
  }

  if (encoderInterrupt) {
    //An encoder was turned
    int pin = mcp.getLastInterruptPin();
    int val = mcp.getLastInterruptPinValue();
    mcp.readGPIO(0);
    if (MCP23017_INT_ERR == val || MCP23017_INT_ERR == pin || val == 1) {
      encoderInterrupt = false;
      
      return ;
    }
    
    int other = mcp.digitalRead(pin+8);

    Encoder encoder = encoders[0];
    if (pin == ENCODER_PITCH) {
      encoder = encoders[2];
      encoder.value = playNotes[selectedChannel][selectedStep];
    } else if (pin == ENCODER_VOLUME) {
      encoder = encoders[7];
      encoder.value = channels[selectedChannel].volume;
    } else if (pin == ENCODER_TEMPO) {
      if (tempoEncoder == true) {
        encoder = encoders[0];
        encoder.value = tempo;
      } else {
        encoder = encoders[1];
        encoder.value = nSteps;
      }
    }

    if (other == 0) {
      encoder.value -= encoder.step;
      if (encoder.value < encoder.min_value){
        encoder.value = encoder.min_value;
      }
    } else {
      encoder.value += encoder.step;
      if (encoder.value > encoder.max_value){
        encoder.value = encoder.max_value;
      }
    }

    if (pin == ENCODER_PITCH) {
      playNotes[selectedChannel][selectedStep] = encoder.value;
      Serial.print("pitch for  step ");
      Serial.print(selectedStep);
      Serial.print(" of channel ");
      Serial.print(selectedChannel);
      Serial.print(": ");
      Serial.println(playNotes[selectedChannel][selectedStep]);
    } else if (pin == ENCODER_VOLUME) {
      channels[selectedChannel].volume = encoder.value;
      Serial.print("Volume: ");
      Serial.println(channels[selectedChannel].volume);
    } else if (pin == ENCODER_TEMPO) {
      if (tempoEncoder == true) {
        tempo = encoder.value;
        OCR1A = (int) (16000000 / (tempo * 8 * 4)) - 1;
        Serial.println(tempo);
      } else {
        nSteps = encoder.value;
        // Serial.print("num steps ");
        // Serial.println(nSteps);
      }
      //Serial.println(tempo);
    }
    mcp.readGPIO(0);
    //while( ! (mcp.readGPIO(0) ));

    encoderInterrupt = false;
  }

  if (switchInterrupt) {
    // a switch was pressed
    int pin = mcp2.getLastInterruptPin();
    int val = mcp2.getLastInterruptPinValue();
    mcp2.readGPIO(0);
    if (MCP23017_INT_ERR == val || MCP23017_INT_ERR == pin || val == 1) {
      switchInterrupt = false;
      return ;
    }

    if (pin == 0) {
      tempoEncoder = !tempoEncoder;
      if (tempoEncoder) {
        ledOn(1,1);
      } else {
        ledOff(1);
      }
    }
    
    Serial.print("switch interrupt ");
    Serial.println(pin);

    switchInterrupt = false;
  }

  changeLedRow();
}


