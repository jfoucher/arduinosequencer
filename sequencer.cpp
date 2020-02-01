#include "Wire.h"
#include "OPL2.h"
#include "instruments.h"
#include "Adafruit_MCP23017.h"
#include <Arduino.h>
#include <EEPROM.h>
#include "SPI.h"

#define DEBUG 1
#define CHIP1ADDR 0x21
#define CHIP2ADDR 0x20
// Rows are connected to ground
#define LED_ROWS 6
// Cols are connected to 5V
#define LED_COLS 8

#define ENCODER_TEMPO 0
#define ENCODER_VOICE 1
#define ENCODER_STEP 2
#define ENCODER_PITCH 3

#define ENCODER_ATTACK 4
#define ENCODER_DECAY 5
#define ENCODER_SUSTAIN 6
#define ENCODER_RELEASE 7

//

// MCP2
#define ENCODER_WF 4
#define ENCODER_VOLUME 3

#define SWITCH_TEMPO 0
#define SWITCH_KON 1
#define SWITCH_MOD 2
#define SWITCH_WF 5

#define CHANNEL_DATA_EEPROM_OFFSET 0
#define NOTE_DATA_EEPROM_OFFSET 128

Adafruit_MCP23017 mcp;
Adafruit_MCP23017 mcp2;

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
  // Step select
  {0, 32, -1, 1},
  // Attack
  {0, 15, 0, 1},
  // Decay
  {0, 15, 0, 1},
  // Sustain
  {0, 15, 0, 1},
  // Release
  {0, 15, 0, 1},
  // Volume
  {0, 63, 0, 1},
  // Waveform
  {0, 2, 0, 1},
  // Feedback
  {0, 2, 0, 1},
};

int i = 0;

int note = 1;
int octave = 1;
int tempo = 100;
int nSteps = 32;
bool tempoEncoder = true;
bool WFEncoder = true;

int selectedChannel = 0;

int selectedOperator = CARRIER;

// Which note is selected.
int selectedStep = 0;


volatile boolean playInterrupt = false;
volatile boolean encoderInterrupt = false;
volatile boolean switchInterrupt = false;

uint8_t leds[LED_ROWS * LED_COLS] = {0};

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
    {14, 6, 6, 9, 0, 35, 1, 1, 1, 10, 2, 6, 9, 35, 3, 1},
    {14, 6, 6, 9, 0, 35, 1, 1, 1, 10, 2, 6, 9, 35, 3, 1},
    {14, 6, 6, 9, 0, 35, 1, 1, 1, 10, 2, 6, 9, 35, 3, 1},
    {14, 6, 6, 9, 0, 35, 1, 1, 1, 10, 2, 6, 9, 35, 3, 1},
    {14, 6, 6, 9, 0, 35, 1, 1, 1, 10, 2, 6, 9, 35, 3, 1},
    {14, 6, 6, 9, 0, 35, 1, 1, 1, 10, 2, 6, 9, 35, 3, 1}
};

//Percussion channels volume
uint8_t percussions[6] = {35};

// Array of ints.
// 32 for each voice.
// First 6 are melodic voices, last 5 are percussion voices
// For melodic voices, the value is the pitch of that note.
// For percussion voices a value other than 0 means play.
int playNotes[11][32];

int pinInt;
int switchInt;

uint8_t currentRow = 0;

void play();


void pinChanged() {
  encoderInterrupt = true;
}

void switchChanged() {
  switchInterrupt = true;
}

ISR(TIMER1_COMPA_vect){//timer1 interrupt plays music
  playInterrupt = true;
}


void saveChannel(int j);
void saveChannel(int j) {
  if (j >= 6) {
    if (j == 6) {
      opl2.setVolume(j, OPERATOR1, 63 - percussions[j - 6]);
      opl2.setVolume(j, OPERATOR2, 63 - percussions[j - 6]);
    } else if (j > 8) {
      opl2.setVolume(j - 1, OPERATOR2, 63 - percussions[j - 6]);
    } else {
      opl2.setVolume(j, OPERATOR1, 63 - percussions[j - 6]);
    }
    
    for (uint8_t a=0; a < 32; a++) {
      EEPROM.put((int)(j * 32 + (a * sizeof(int)) + CHANNEL_DATA_EEPROM_OFFSET + NOTE_DATA_EEPROM_OFFSET), (int)playNotes[j][a]);
    }
    return;
  }

  for (uint8_t a=0; a < 32; a++) {
    EEPROM.put((int)(j * 32 + (a * sizeof(int)) + CHANNEL_DATA_EEPROM_OFFSET + NOTE_DATA_EEPROM_OFFSET), (int)playNotes[j][a]);
  }

  int chSize = sizeof(Channel);
  for (uint8_t a=0; a < chSize; a++) {
    uint8_t *pPtr = (uint8_t *)&channels[j];
    uint8_t val = *(pPtr+a);
    EEPROM.update(j * chSize + a + CHANNEL_DATA_EEPROM_OFFSET, val);
  }

  

  opl2.setVolume(j, CARRIER, 63 - channels[j].volume);
  // Only set volume for percussion channels

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
  opl2.setVolume(j, MODULATOR, 63 - channels[j].m_volume);
  opl2.setAttack    (j, MODULATOR, channels[j].m_attack);
  opl2.setDecay     (j, MODULATOR, channels[j].m_decay);
  opl2.setSustain   (j, MODULATOR, channels[j].m_sustain);
  opl2.setRelease   (j, MODULATOR, channels[j].m_release);
  
  //Add button to set volume per channel
  
  opl2.setWaveForm(j, CARRIER, channels[j].waveform);
  opl2.setAttack    (j, CARRIER, channels[j].attack);
  opl2.setDecay     (j, CARRIER, channels[j].decay);
  opl2.setSustain   (j, CARRIER, channels[j].sustain);
  opl2.setRelease   (j, CARRIER, channels[j].release);

  //TODO save to Eeprom
  //Also save "playNotes"
  //Start address for this channel
  
}

void setup() {
  //Serial.begin(9600);
    //Reset mcp
  pinMode(12, OUTPUT);
  // digitalWrite(12, LOW);
  // digitalWrite(12, HIGH);
  // digitalWrite(12, LOW);
  digitalWrite(12, HIGH);

  //I2C_ClearBus();


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
  opl2.setBlock(6, 3);
  opl2.setFNumber(6, opl2.getNoteFNumber(NOTE_C));

  // Set octave and frequency for snare drum and hi-hat.
  opl2.setBlock(7, 3);
  opl2.setFNumber(7, opl2.getNoteFNumber(NOTE_C));
  // Set low volume on hi-hat
  opl2.setVolume(7, OPERATOR1, 0x2f);
  opl2.setVolume(7, OPERATOR2, 0x2F);

  // Set octave and frequency for tom tom and cymbal.
  opl2.setBlock(8, 3);
  opl2.setFNumber(8, opl2.getNoteFNumber(NOTE_A));
  opl2.setVolume(8, OPERATOR1, 0x1F);
  opl2.setVolume(8, OPERATOR2, 0x1F);

  // Serial.begin(9600);


  // TCCR0A = 0;// set entire TCCR0A register to 0
  // TCCR0B = 0;// same for TCCR0B
  // TCNT0  = 0;//initialize counter value to 0
  // // set compare match register for 2khz increments
  // OCR0A = 0xB0;// = (16*10^6) / (2000*64) - 1 (must be <256)
  // // turn on CTC mode
  // TCCR0A |= (1 << WGM01);
  // // Set CS02 and CS00 bits for 1024 prescaler
  // TCCR0B |= (1 << CS02) | (1 << CS00);   
  // // enable timer compare interrupt
  // TIMSK0 |= (1 << OCIE0A);

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

  //sei();

  pinMode(3, INPUT);
  pinMode(3, INPUT_PULLUP);
  pinMode(2, INPUT);
  pinMode(2, INPUT_PULLUP);



  delay(500);
  mcp.begin(1);

  mcp.setupInterrupts(false, false, LOW);
  
  for(int i = 0; i <= 15;i++) {
    mcp.pinMode(i, INPUT);
    mcp.pullUp(i, HIGH);
  }

  for(int i = 0; i <= 7;i++) {
    mcp.setupInterruptPin(i, CHANGE); 
  }

  delay(100);
  mcp2.begin(0);
  mcp2.setupInterrupts(false, false, LOW);

  for(int i = 0; i <= 15;i++) {
    mcp2.pinMode(i, INPUT);
    mcp2.pullUp(i, HIGH);
  }

  for(int i = 0; i <= 7;i++) {
    mcp2.setupInterruptPin(i, CHANGE); 
  }

  pinInt = digitalPinToInterrupt(3);
  switchInt = digitalPinToInterrupt(2);
  attachInterrupt(pinInt, pinChanged, FALLING);
  attachInterrupt(switchInt, switchChanged, FALLING);
  pinMode(3, INPUT);
  pinMode(3, INPUT_PULLUP);
  pinMode(2, INPUT);
  pinMode(2, INPUT_PULLUP);

  pinMode(9, OUTPUT);
  digitalWrite(9, LOW);



  //Turn off all leds
  SPI.transfer(0);
  SPI.transfer(0);
  digitalWrite(9, HIGH);
  digitalWrite(9, LOW);

  //Restore channel data from EEPROM
  int chSize = sizeof(Channel);
  for (int j = 0; j < 11; j++) {
    uint8_t *pPtr = (uint8_t *)&channels[j];
    for (uint8_t a=0; a < chSize; a++) {
      *(pPtr+a) = EEPROM.read(j * chSize + a + CHANNEL_DATA_EEPROM_OFFSET);
    }
  }
  //Serial.begin(115200);
  // // Restore playnotes from EEPROM
  for (int j = 0; j < 11; j++) {
    for (uint8_t a=0; a < 32; a++) {
      int val = 0;
      int addr = (int)(j * 32 + (a * sizeof(int)) + CHANNEL_DATA_EEPROM_OFFSET + NOTE_DATA_EEPROM_OFFSET);
      EEPROM.get(addr, val);
      // Serial.print("got from EEprom ");
      // Serial.print(val);
      // Serial.print(" from address ");
      // Serial.println(addr);
      playNotes[j][a] = val == -1 ? 0 : val;
    }
  }

  // for (uint8_t a=0; a < 32; a++) {
  //   EEPROM.put((int)(j * 32 + (a * sizeof(int)) + CHANNEL_DATA_EEPROM_OFFSET + NOTE_DATA_EEPROM_OFFSET), playNotes[j][a]);
  // }
  
  EEPROM.get(512, tempo);

  OCR1A = (int) (16000000 / (tempo * 8 * 4)) - 1;

  EEPROM.get(516, nSteps);

  for (int m = 0; m < 32; m++) {
    //playNotes[0][m] = m % 4 == 0 ? 32 : 0;
    // playNotes[1][m] = 40;
    //playNotes[6][m] = (m+2) % 8 == 0 ? 1 : 0;
    //playNotes[7][m] = (m+2) % 4;
    // playNotes[8][m] = (m + 2) % 8 == 0 ? 1 : 0;
    // playNotes[9][m] = (m + 3) % 8 == 0 ? 1 : 0;
    // playNotes[10][m] = (m + 4) % 8 == 0 ? 1 : 0;
  }
}


// ISR(TIMER0_COMPA_vect){  //change the 0 to 1 for timer1 and 2 for timer2

  
// }

void ledOn(int ref, int level);
void ledOn (int ref, int level) {
  leds[ref] = (256 - (level & 0xff)) | 1;
}
void ledOff (int ref);
void ledOff (int ref) {
  leds[ref] = 0;
}



void play() {
  OCR1A = (int) (16000000 / (tempo * 8 * 4)) - 1;
  // play melodic instruments if required
  for(int j = 0; j < 6; j++) {
    if (playNotes[j][i]) {
      octave = playNotes[j][i] >> 4;
      note = (int)((float)(playNotes[j][i] & 0xf) / 16.0 * 12.0);
      opl2.playNote(j, octave, note);
    }
  }

  //TODO lower level of selected step led if in the same sector as the current step
  
  if (i % 32 == 0) {
    ledOff(nSteps + 15);
  } else {
    ledOff((i % nSteps) + 15);
  }

  ledOn(selectedStep + 16, 34);
  int ledNum = (i % nSteps) + 16;
  ledOn(ledNum, playNotes[selectedChannel][i] ? 255 : 15);
  // Serial.print("play led: ");
  // Serial.println(ledNum);
  
  // play drums instruments if required
  opl2.setDrums(playNotes[6][i] ? 1 : 0, playNotes[7][i] ?  1 : 0, playNotes[8][i] ? 1 : 0, playNotes[9][i] ? 1 : 0, playNotes[10][i] ? 1 : 0);
  
  i++;
  
  if (i >= nSteps) {
    i = 0;
  }
}

int l = 0;
unsigned int pmils;
uint8_t lc = 0;

// 0  1  2  3  4  5  6  7
// 8  9  10 11 12 13 14 15
// 16 17 18 19 20 21 22 23
// 24 25 26 27 28 29 30 31
// 32 33 34 35 36 37 38 39
// 40 41 42 43 44 45 46 47

void loop() {
  ledOn(selectedChannel, 255);
  
  //Tranfer two bytes, in the first one put which rows are on
  //And in the second one which column(s) is on
  uint8_t b1 = (1 << currentRow); // HIGH means this row will be on
  uint8_t b2 = 0;
  for (int j = 0; j < LED_COLS;j++) {
    uint8_t l = leds[currentRow * LED_COLS + j];
    if (l > 0 && lc-l >= 0) {
      b2 |= 1 << j;
    } else {
      b2 &= ~(1 << j);
    }
  }
  SPI.transfer(b1);
  SPI.transfer(~b2);
  // SPI.transfer(0x1);
  // SPI.transfer(~0b01111100);

  digitalWrite(9, HIGH);
  digitalWrite(9, LOW);

  lc++;

  currentRow++;
  if (currentRow >= LED_ROWS) {
    currentRow = 0;
  }

  if (playInterrupt) {
    saveChannel(selectedChannel);

    play();
    playInterrupt = false;
  }

  if (encoderInterrupt) {
    //An encoder was turned
    encoderInterrupt = false;
    int pin = mcp.getLastInterruptPin();
    int val = mcp.getLastInterruptPinValue();
    mcp.readGPIO(0);



    // Serial.print("Pin interrupt ");
    // Serial.println(pin);
    
    if (MCP23017_INT_ERR == val || MCP23017_INT_ERR == pin || val == 1) {
      //digitalWrite(12, LOW);
      //digitalWrite(12, HIGH);
      return ;
    }
    
    int other = mcp.digitalRead(pin+8);

    Encoder encoder = encoders[0];
    if (pin == ENCODER_PITCH) {
      if (selectedChannel >= 6) {
        return;
      }
      encoder = encoders[2];
      if (selectedOperator == CARRIER) {
        encoder.value = playNotes[selectedChannel][selectedStep];
      } else {
        encoder.value = channels[selectedChannel].m_mult;
      }
    } else if (pin == ENCODER_ATTACK) {
      if (selectedChannel >= 6) {
        return;
      }
      encoder = encoders[5];
      if (selectedOperator == CARRIER) {
        encoder.value = channels[selectedChannel].attack;
      } else {
        encoder.value = channels[selectedChannel].m_attack;
      }
    } else if (pin == ENCODER_DECAY) {
      if (selectedChannel >= 6) {
        return;
      }
      encoder = encoders[5];
      if (selectedOperator == CARRIER) {
        encoder.value = channels[selectedChannel].decay;
      } else {
        encoder.value = channels[selectedChannel].m_decay;
      }
      
    } else if (pin == ENCODER_SUSTAIN) {
      if (selectedChannel >= 6) {
        return;
      }
      encoder = encoders[5];
      if (selectedOperator == CARRIER) {
        encoder.value = channels[selectedChannel].sustain;
      } else {
        encoder.value = channels[selectedChannel].m_sustain;
      }
    } else if (pin == ENCODER_RELEASE) {
      if (selectedChannel >= 6) {
        return;
      }
      encoder = encoders[8];
      if (selectedOperator == CARRIER) {
        encoder.value = channels[selectedChannel].release;
      } else {
        encoder.value = channels[selectedChannel].m_release;
      }
    }
    //  else if (pin == ENCODER_VOLUME) {
    //   encoder = encoders[9];
    //   if (selectedOperator == CARRIER) {
    //     encoder.value = channels[selectedChannel].volume;
    //   } else {
    //     encoder.value = channels[selectedChannel].m_volume;
    //   }
    // } 
    else if (pin == ENCODER_TEMPO) {
      if (tempoEncoder == true) {
        encoder = encoders[0];
        encoder.value = tempo;
      } else {
        encoder = encoders[1];
        encoder.value = nSteps;
      }
    } else if (pin == ENCODER_VOICE) {
        encoder = encoders[3];
        encoder.value = selectedChannel;
    } else if (pin == ENCODER_STEP) {
        encoder = encoders[4];
        encoder.value = selectedStep;
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
      if (selectedOperator == CARRIER) {
        playNotes[selectedChannel][selectedStep] = encoder.value;
      } else {
        if (encoder.value >= 15) {
          channels[selectedChannel].m_mult = 15;
        } else {
          channels[selectedChannel].m_mult = encoder.value;
        }
      }
      
      // Serial.print("pitch for  step ");
      // Serial.print(selectedStep);
      // Serial.print(" of channel ");
      // Serial.print(selectedChannel);
      // Serial.print(": ");
      // Serial.println(encoder.value);

    } else if (pin == ENCODER_ATTACK) {
      if (selectedOperator == CARRIER) {
        channels[selectedChannel].attack = encoder.value;
      } else {
        channels[selectedChannel].m_attack = encoder.value;
      }
      // Serial.print("attack ");
      // Serial.println(encoder.value);
    } else if (pin == ENCODER_DECAY) {
      if (selectedOperator == CARRIER) {
        channels[selectedChannel].decay = encoder.value;
      } else {
        channels[selectedChannel].m_decay = encoder.value;
      }
      // Serial.print("decay ");
      // Serial.println(encoder.value);
    } else if (pin == ENCODER_SUSTAIN) {
      if (selectedOperator == CARRIER) {
        channels[selectedChannel].sustain = encoder.value;
      } else {
        channels[selectedChannel].m_sustain = encoder.value;
      }
      // Serial.print("sustain ");
      // Serial.println(encoder.value);
    }  else if (pin == ENCODER_RELEASE) {
      if (selectedOperator == CARRIER) {
        channels[selectedChannel].release = encoder.value;
      } else {
        channels[selectedChannel].m_release = encoder.value;
      }
      // Serial.print("release ");
      // Serial.println(encoder.value);
    }else if (pin == ENCODER_TEMPO) {
      if (tempoEncoder == true) {
        tempo = encoder.value;
        
        EEPROM.put(512, tempo);
        // Serial.println(tempo);
      } else {
        nSteps = encoder.value;
        EEPROM.put(516, nSteps);
        // Serial.print("num steps ");
        // Serial.println(nSteps);
      }
      //Serial.println(tempo);
    } else if (pin == ENCODER_VOICE) {
      ledOff(selectedChannel);
      selectedChannel = encoder.value;
      ledOn(selectedChannel, 255);
      // Serial.print("new channel ");
      // Serial.println(selectedChannel);
      //Serial.println(tempo);
    } else if (pin == ENCODER_STEP) {
        ledOff(selectedStep + 16);
        if (encoder.value >= nSteps) {
          encoder.value = 0;
        }
        if (encoder.value < 0) {
          encoder.value = nSteps - 1;
        }
        selectedStep = encoder.value;
        ledOn(selectedStep + 16, 128);
      //Serial.println(tempo);
    }
    mcp.readGPIO(0);
    //while( ! (mcp.readGPIO(0) ));

    encoderInterrupt = false;
  }

  if (switchInterrupt) {
    // a switch was pressed
    
    switchInterrupt = false;
    int pin = mcp2.getLastInterruptPin();

    int val = mcp2.getLastInterruptPinValue();
    mcp2.readGPIO(0);
    
    if (MCP23017_INT_ERR == val || MCP23017_INT_ERR == pin || val == 1) {
      //digitalWrite(12, LOW);
      //digitalWrite(12, HIGH);
      return ;
    }
    // Serial.print("switch interrupt ");
    // Serial.println(pin);
    if (pin == SWITCH_TEMPO) {
      tempoEncoder = !tempoEncoder;
      if (tempoEncoder) {
        ledOff(11);
      } else {
        ledOn(11, 255);
      }
      // Serial.print("Tempo encoder : ");
      // Serial.println(tempoEncoder ? "tempo" : "steps");
    } else if (pin == SWITCH_KON) {
      if (playNotes[selectedChannel][selectedStep]) {
        playNotes[selectedChannel][selectedStep] = 0;
      } else {
        playNotes[selectedChannel][selectedStep] = 30;
      }
    } else if (pin == SWITCH_MOD) {
      if (selectedOperator == MODULATOR) {
        selectedOperator = CARRIER;
        ledOff(13);
      } else {
        selectedOperator = MODULATOR;
        ledOn(13, 255);
      }
      if (tempoEncoder) {
        ledOff(11);
      } else {
        ledOn(11, 255);
      }
      // Serial.print("operator ");
      // Serial.println(selectedOperator == MODULATOR ? "MODULATOR" : "CARRIER");
    } else if (pin == ENCODER_WF) {
      if (selectedChannel >= 6) {
        return;
      }
      int other = mcp2.digitalRead(9);
      if (other == 1) {
        if (selectedOperator == CARRIER) {
          channels[selectedChannel].waveform += 1;
          if (channels[selectedChannel].waveform >= 3) {
            channels[selectedChannel].waveform = 3;
          }
        } else {
            channels[selectedChannel].m_waveform += 1;
            if (channels[selectedChannel].m_waveform >= 3) {
              channels[selectedChannel].m_waveform = 3;
            }
        }
      } else {
        if (selectedOperator == CARRIER) {
          if (channels[selectedChannel].waveform <= 1) {
            channels[selectedChannel].waveform = 0;
            return;
          }
          channels[selectedChannel].waveform -= 1;
        } else {
          if (channels[selectedChannel].m_waveform <= 1) {
            channels[selectedChannel].m_waveform = 0;
            return;
          }
          channels[selectedChannel].m_waveform -= 1;
        }
      }
      // Serial.print("waveform carrier ");
      // Serial.print(channels[selectedChannel].waveform);
      // Serial.print(" waveform modulator ");
      // Serial.println(channels[selectedChannel].m_waveform);
    } else if (pin == ENCODER_VOLUME) {
      int other = mcp2.digitalRead(8);
      if (other == 1) {
        if (selectedOperator == CARRIER) {
          if (selectedChannel >= 6) {
            percussions[selectedChannel - 6] += 1;
            if (percussions[selectedChannel - 6] >= 63) {
              percussions[selectedChannel - 6] = 63;
            }
          } else {
            channels[selectedChannel].volume += 1;
            if (channels[selectedChannel].volume >= 63) {
              channels[selectedChannel].volume = 63;
            }
          }
          
        } else {
          if (selectedChannel <= 6) {
            channels[selectedChannel].m_volume += 1;
            if (channels[selectedChannel].m_volume >= 63) {
              channels[selectedChannel].m_volume = 63;
            }
          }
        }
      } else {
        if (selectedOperator == CARRIER) {
          if (selectedChannel >= 6) {
            if (percussions[selectedChannel - 6] <= 1) {
              percussions[selectedChannel - 6] = 0;
              return;
            }
            percussions[selectedChannel - 6] -= 1;
          }else {
            if (channels[selectedChannel].volume <= 1) {
              channels[selectedChannel].volume = 0;
              return;
            }
            channels[selectedChannel].volume -= 1;
          }
        } else {
          if (selectedChannel >= 6) {
            return;
          }
          if (channels[selectedChannel].m_volume <= 1) {
            channels[selectedChannel].m_volume = 0;
            return;
          }
          channels[selectedChannel].m_volume -= 1;
        }
      }
    }
  }
}


