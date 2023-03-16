// This optional setting causes Encoder to use more optimized code,
// It must be defined before Encoder.h is included.
#define ENCODER_OPTIMIZE_INTERRUPTS
#include <Encoder.h>
#include <Bounce.h>
#include <ADC.h>
#include <ADC_util.h>

ADC *adc = new ADC();

//Teensy 3.6 - Mux Pins
#define MUX_0 30
#define MUX_1 31
#define MUX_2 32
#define MUX_3 29

#define MUX1_S A0
#define MUX2_S A1
#define MUX3_S A2

#define DEMUX_0 36
#define DEMUX_1 35
#define DEMUX_2 34
#define DEMUX_3 33

#define DEMUX_EN_1 39
#define DEMUX_EN_2 38


//Note DAC
#define DACMULT 3.30
#define DAC_CS1 10
#define DAC_CS2 37

//Mux 1 Connections
#define MUX1_spare0 0 			// 2V
#define MUX1_pwLFO 1   			// 5V
#define MUX1_osc2PW 2			// 12V
#define MUX1_osc1PW 3			// 12V
#define MUX1_spare4 4			// 2V
#define MUX1_fmDepth 5			// 2V
#define MUX1_osc2PWM 6			// 2V
#define MUX1_osc1PWM 7  		// 2V

#define MUX1_osc1Range 8  		// 3.3V switches
#define MUX1_noiseLevel 9 		// 2V
#define MUX1_osc1SawLevel 10		// 2V 
#define MUX1_osc2Detune 11		// 3.3V
#define MUX1_osc2Range 12  		// 3.3V switches
#define MUX1_stack 13			// 3.3V
#define MUX1_osc2SawLevel 14		// 2V
#define MUX1_glideTime 15		// MIDI CC

//Mux 2 Connections
#define MUX2_filterType 0		// 5V switches
#define MUX2_LFOWaveform 1		// 5V
#define MUX2_LFODelay 2			// ? software
#define MUX2_LFORate 3			// 5V
#define MUX2_osc2TriangleLevel 4	// 2V
#define MUX2_osc2PulseLevel 5 		// 2V
#define MUX2_osc1PulseLevel 6		// 2V
#define MUX2_osc1SubLevel 7	// 2V

#define MUX2_filterDecay 8		// 5V
#define MUX2_ampAttack 9		// 5V
#define MUX2_filterAttack 10		// 5V
#define MUX2_filterRes 11		// 2.5V
#define MUX2_filterCutoff 12 		// 5V
#define MUX2_filterEGlevel 13		// 5V
#define MUX2_keyTrack 14		// 2V
#define MUX2_filterLFO 15		// 5V

//Mux 3 Connections
#define MUX3_volumeControl 0		// 2V
#define MUX3_balance 1			// 2V
#define MUX3_ampSustain 2		// 5V
#define MUX3_ampRelease 3		// 5V
#define MUX3_filterRelease 4		// 5V
#define MUX3_filterSustain 5		// 5V
#define MUX3_ampDecay 6			// 5V


//DeMux 1 Connections
#define DEMUX1_noiseLevel 0                 // 0-2v
#define DEMUX1_osc2PulseLevel 1             // 0-2v
#define DEMUX1_osc2Level 2                  // 0-2v
#define DEMUX1_LfoDepth 3                   // 0-2v
#define DEMUX1_osc1PWM 4                    // 0-2v
#define DEMUX1_PitchBend 5                  // 0-2v
#define DEMUX1_osc1PulseLevel 6             // 0-2v
#define DEMUX1_osc1Level 7                  // 0-2v
#define DEMUX1_modwheel 8                   // 0-2v
#define DEMUX1_volumeControl 9              // 0-2v
#define DEMUX1_osc2PWM 10                   // 0-2v
#define DEMUX1_spare 11                     // spare VCA 0-2v
#define DEMUX1_AmpEGLevel 12                // Amplevel 0-5v
#define DEMUX1_MasterTune 13                // -15v to +15v  control 12   +/-13v
#define DEMUX1_osc2Detune 14                // -15v to +15v control 17 +/-13v
#define DEMUX1_pitchEGLevel 15              // PitchEGlevel 0-5v

//DeMux 2 Connections
#define DEMUX2_Amprelease 0                 // 0-5v
#define DEMUX2_Ampsustain 1                 // 0-5v
#define DEMUX2_Ampdecay 2                   // 0-5v
#define DEMUX2_Ampattack 3                  // 0-5v
#define DEMUX2_LfoRate 4                    // 0-5v
#define DEMUX2_LFOMULTI 5                   // 0-5v
#define DEMUX2_LFOWaveform 6                // 0-5v
#define DEMUX2_LfoDelay 7                   // 0-5v
#define DEMUX2_osc2PW 8                     // 0-5v
#define DEMUX2_osc1PW 9                     // 0-5v
#define DEMUX2_filterEGlevel 10             // FilterEGlevel 0-5v  
#define DEMUX2_ADSRMode 11                  // 0-5v
#define DEMUX2_pwLFO 12                     // 0-5v 
#define DEMUX2_LfoSlope 13                  // 0-5v 
#define DEMUX2_filterCutoff 14              // 0-10v
#define DEMUX2_filterRes15                  // 0-10v

//DeMux 3 Connections
#define DEMUX3_filterA 0                    // filterA 0-5v switched
#define DEMUX3_filterB 1                    // filterB 0-5v switched
#define DEMUX3_filterC 2                    // filterC 0-5v switched
#define DEMUX3_filterLoopA0 3               // syncA0 0-5v switched
#define DEMUX3_filterLoopA1 4               // syncA1 0-5v switched    
#define DEMUX3_LFOalt 5                     // lFOalt 0-5v switched
#define DEMUX3_spare 6                      // spare 0-5v switched
#define DEMUX3_spare1 7                     // spare 0-5v switched
#define DEMUX3_pitchAttack 8                // Pitchattack 0-5v
#define DEMUX3_pitchDecay 9                 // Pitchdecay 0-5v
#define DEMUX3_pitchSustain 10              // Pitchsustain  0-5v
#define DEMUX3_PitchRelease 11              // Pitchrelease 0-5v
#define DEMUX3_filterAttack 12              // FilterAattack 0-5v
#define DEMUX3_filterDecay 13               // Filterdecay 0-5v
#define DEMUX3_filterSustain 14             // Filtersustain 0-5v
#define DEMUX3_filterEelease 14             // Filterrelease 0-5v

// 595 outputs

#define FILTERA_UPPER 0
#define FILTERB_UPPER 1
#define FILTERC_UPPER 2
#define FILTER_POLE_UPPER 3
#define FILTER_EG_INV_UPPER 4
#define FILTER_VELOCITY_UPPER 5
#define AMP_VELOCITY_UPPER 6
#define LFO_ALT_UPPER 7

#define CHORUS1_OUT_UPPER 8 
#define CHORUS2_OUT_UPPER 9
#define FILTER_MODE_BIT0_UPPER 10
#define FILTER_MODE_BIT1_UPPER 11
#define AMP_MODE_BIT0_UPPER 12
#define AMP_MODE_BIT1_UPPER 13
#define FILTER_LIN_LOG_UPPER 14
#define AMP_LIN_LOG_UPPER 15

#define OCT1A_UPPER 16
#define OCT1B_UPPER 17
#define OCT2A_UPPER 18
#define OCT2B_UPPER 19
#define FILTER_KEYTRACK_UPPER 20
#define UPPER1 21

#define FILTERA_LOWER 24
#define FILTERB_LOWER 25
#define FILTERC_LOWER 26
#define FILTER_POLE_LOWER 27
#define FILTER_EG_INV_LOWER 28
#define FILTER_VELOCITY_LOWER 29
#define AMP_VELOCITY_LOWER 30
#define LFO_ALT_LOWER 31

#define CHORUS1_OUT_LOWER 32 
#define CHORUS2_OUT_LOWER 33
#define FILTER_MODE_BIT0_LOWER 34
#define FILTER_MODE_BIT1_LOWER 35
#define AMP_MODE_BIT0_LOWER 36
#define AMP_MODE_BIT1_LOWER 37
#define FILTER_LIN_LOG_LOWER 38
#define AMP_LIN_LOG_LOWER 39

#define OCT1A_LOWER 40
#define OCT1B_LOWER 41
#define OCT2A_LOWER 42
#define OCT2B_LOWER 43
#define FILTER_KEYTRACK_LOWER 44
#define UPPER2 45

// 595 LEDs

#define CHORUS1_LED 0 
#define CHORUS2_LED 1
#define GLIDE_LED 2
#define VCAGATE_LED 3
#define VCALOOP_LED 4
#define VCAVEL_LED 5
#define LFO_ALT_LED 6 
#define FILTERPOLE_LED 7

#define FILTERLOOP_LED 8
#define FILTERVEL_LED 9
#define FILTERINV_LED 10
#define UPPER_LED 11
#define SAVE_LED 12
#define WHOLE_LED 13
#define DUAL_LED 14
#define SPLIT_LED 15

//Switches
// roxmux 74HC165

#define CHORUS1_SW 0
#define CHORUS2_SW 1
#define GLIDE_SW 2
#define VCAGATE_SW 3
#define VCALOOP_SW 4
#define VCAVEL_SW 5
#define LFO_ALT_SW 6
#define FILTERPOLE_SW 7

#define FILTERLOOP_SW 8
#define FILTERVEL_SW 9
#define FILTERINV_SW 10
#define UPPER_SW 11

#define WHOLE_SW 14
#define DUAL_SW 15
#define SPLIT_SW 13

// System Switches etc

#define RECALL_SW 6
#define SAVE_SW 24
#define SETTINGS_SW 25
#define BACK_SW 28

#define ENCODER_PINA 4
#define ENCODER_PINB 5

#define MUXCHANNELS 16
#define DEMUXCHANNELS 16
#define QUANTISE_FACTOR 10

#define DEBOUNCE 30

static byte muxInput = 0;
static byte muxOutput = 0;

static int mux1ValuesPrev[MUXCHANNELS] = {};
static int mux2ValuesPrev[MUXCHANNELS] = {};
static int mux3ValuesPrev[MUXCHANNELS] = {};

static int mux1Read = 0;
static int mux2Read = 0;
static int mux3Read = 0;


static long encPrevious = 0;

Bounce recallButton = Bounce(RECALL_SW, DEBOUNCE); //On encoder
boolean recall = true; //Hack for recall button
Bounce saveButton = Bounce(SAVE_SW, DEBOUNCE);
boolean del = true; //Hack for save button
Bounce settingsButton = Bounce(SETTINGS_SW, DEBOUNCE);
boolean reini = true; //Hack for settings button
Bounce backButton = Bounce(BACK_SW, DEBOUNCE);
boolean panic = true; //Hack for back button
Encoder encoder(ENCODER_PINB, ENCODER_PINA);//This often needs the pins swapping depending on the encoder

void setupHardware()
{
     //Volume Pot is on ADC0
  //Volume Pot is on ADC0
  adc->adc0->setAveraging(16); // set number of averages 0, 4, 8, 16 or 32.
  adc->adc0->setResolution(10); // set bits of resolution  8, 10, 12 or 16 bits.
  adc->adc0->setConversionSpeed(ADC_CONVERSION_SPEED::VERY_LOW_SPEED); // change the conversion speed
  adc->adc0->setSamplingSpeed(ADC_SAMPLING_SPEED::MED_SPEED); // change the sampling speed

  //MUXs on ADC1
  adc->adc1->setAveraging(16); // set number of averages 0, 4, 8, 16 or 32.
  adc->adc1->setResolution(10); // set bits of resolution  8, 10, 12 or 16 bits.
  adc->adc1->setConversionSpeed(ADC_CONVERSION_SPEED::VERY_LOW_SPEED); // change the conversion speed
  adc->adc1->setSamplingSpeed(ADC_SAMPLING_SPEED::MED_SPEED); // change the sampling speed

  analogReadResolution(10);


  //Mux address pins

  pinMode(DAC_CS1, OUTPUT);
  digitalWrite(DAC_CS1, HIGH);
  pinMode(DAC_CS2, OUTPUT);
  digitalWrite(DAC_CS2, HIGH);

  pinMode(MUX_0, OUTPUT);
  pinMode(MUX_1, OUTPUT);
  pinMode(MUX_2, OUTPUT);
  pinMode(MUX_3, OUTPUT);

  pinMode(DEMUX_0, OUTPUT);
  pinMode(DEMUX_1, OUTPUT);
  pinMode(DEMUX_2, OUTPUT);
  pinMode(DEMUX_3, OUTPUT);

  digitalWrite(MUX_0, LOW);
  digitalWrite(MUX_1, LOW);
  digitalWrite(MUX_2, LOW);
  digitalWrite(MUX_3, LOW);

  digitalWrite(DEMUX_0, LOW);
  digitalWrite(DEMUX_1, LOW);
  digitalWrite(DEMUX_2, LOW);
  digitalWrite(DEMUX_3, LOW);

  pinMode(DEMUX_EN_1, OUTPUT);
  pinMode(DEMUX_EN_2, OUTPUT);

  digitalWrite(DEMUX_EN_1, HIGH);
  digitalWrite(DEMUX_EN_2, HIGH);


  //Switches

  pinMode(RECALL_SW, INPUT_PULLUP); //On encoder
  pinMode(SAVE_SW, INPUT_PULLUP);
  pinMode(SETTINGS_SW, INPUT_PULLUP);
  pinMode(BACK_SW, INPUT_PULLUP);
  
}
