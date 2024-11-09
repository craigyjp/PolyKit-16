#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "pico-dco-dac.pio.h"
#include "hardware/pwm.h"
#include "bsp/board.h"
#include "tusb.h"
#include "hardware/uart.h"
#include "hardware/spi.h"

#define NUM_VOICES 8
#define MIDI_CHANNEL 1
#define USE_ADC_STACK_VOICES // gpio 28 (adc 2)
#define USE_ADC_DETUNE       // gpio 27 (adc 1)
#define USE_ADC_FM           // gpio 26 (adc 0)
#define USE_OCTAVE_SWITCH

#define PIN_MISO 4
#define PIN_MOSI 3
#define PIN_CS   5
#define PIN_SCK  2

#define VEL_DAC_SELECT 7

#define PIN_OCT1  20
#define PIN_OCT2  21

#define SPI_PORT spi0

uint32_t int_ref_on_flexible_mode = 0b00001001000010100000000000000000;

uint32_t DACS[8] = {0b00000000000000000000000000000000,
                    0b00000000000100000000000000000000, 
                    0b00000000001000000000000000000000, 
                    0b00000000001100000000000000000000, 
                    0b00000000010000000000000000000000, 
                    0b00000000010100000000000000000000, 
                    0b00000000011000000000000000000000,
                    0b00000000011100000000000000000000};

uint32_t sample_data = 0b00000000000000000000000000000000;

uint8_t rs[4];
uint8_t cv[4];
uint8_t vel[4];
uint8_t OCT1 = 1;
uint8_t OCT2 = 1;
float OCT = 0;

uint STACK_VOICES = 1;
float DETUNE = 0.0f, LAST_DETUNE = 0.0f;
float FM_VALUE = 0.0f;  
float LAST_FM = 0.0f;
float LAST_OCT = 1.0f;
float sfAdj[8] = {1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f};
#define NOTE_SF 276.60f
#define VEL_SF 256

// Scale factor for FM. Controls how intense the effect is at maximum input voltage.
// Units: Hertz.
float FM_INTENSITY = 15.0f;          

const float BASE_NOTE = 440.0f;
const uint8_t RESET_PINS[NUM_VOICES] = {13, 13, 13, 13, 13, 13, 13, 13};
const uint8_t RANGE_PINS[NUM_VOICES] = {16, 16, 16, 16, 16, 16, 16, 16};
const uint8_t GATE_PINS[NUM_VOICES] = {8, 9, 10, 11, 12, 15, 18, 14};
const uint8_t VOICE_TO_PIO[NUM_VOICES] = {0, 0, 0, 0, 1, 1, 1, 1};
const uint8_t VOICE_TO_SM[NUM_VOICES] = {0, 1, 2, 3, 0, 1, 2, 3};
const uint16_t DIV_COUNTER = 1250;
uint8_t RANGE_PWM_SLICES[NUM_VOICES];

uint32_t VOICES[NUM_VOICES];
uint8_t VOICE_NOTES[NUM_VOICES];
uint8_t VOICE_GATE[NUM_VOICES];

uint8_t NEXT_VOICE = 0;
uint32_t LED_BLINK_START = 0;
PIO pio[2] = {pio0, pio1};
uint8_t midi_serial_status = 0;
uint16_t midi_pitch_bend = 0x2000, last_midi_pitch_bend = 0x2000;
bool portamento = false;
uint8_t portamento_time = 0, portamento_start = 0, portamento_stop = 0;
float portamento_cur_freq = 0.0f;

void output_DAC(uint32_t sample_data);
void DAC_reset();
void cs_select();
void vel_select();
void cs_deselect();
void vel_deselect();
void zero_DACs();
void output_VELOCITY();
void init_sm(PIO pio, uint sm, uint offset, uint pin);
void set_frequency(PIO pio, uint sm, float freq);
float get_freq_from_midi_note(uint8_t note);
void led_blinking_task();
uint8_t get_free_voice();
void usb_midi_task();
void serial_midi_task();
void note_on(uint8_t note, uint8_t velocity);
void note_off(uint8_t note);
void voice_task();
void adc_task();
void octave_task();
long map(long x, long in_min, long in_max, long out_min, long out_max);

int main() {
    board_init();
    tusb_init();

    // use more accurate PWM mode for buck-boost converter
    gpio_init(23);
    gpio_set_dir(23, GPIO_OUT);
    gpio_put(23, 1);

    // init serial midi
    uart_init(uart0, 31250);
    uart_set_fifo_enabled(uart0, true);
    gpio_set_function(0, GPIO_FUNC_UART);
    gpio_set_function(1, GPIO_FUNC_UART);

    // pwm init
    for (int i=0; i<NUM_VOICES; i++) {
        gpio_set_function(RANGE_PINS[i], GPIO_FUNC_PWM);
        RANGE_PWM_SLICES[i] = pwm_gpio_to_slice_num(RANGE_PINS[i]);
        pwm_set_wrap(RANGE_PWM_SLICES[i], DIV_COUNTER);
        pwm_set_enabled(RANGE_PWM_SLICES[i], true);
    }

    // pio init
    uint offset[2];
    offset[0] = pio_add_program(pio[0], &frequency_program);
    offset[1] = pio_add_program(pio[1], &frequency_program);
    for (int i=0; i<NUM_VOICES; i++) {
        init_sm(pio[VOICE_TO_PIO[i]], VOICE_TO_SM[i], offset[VOICE_TO_PIO[i]], RESET_PINS[i]);
    }

    // gate gpio init
    for (int i=0; i<NUM_VOICES; i++) {
        gpio_init(GATE_PINS[i]);
        gpio_set_dir(GATE_PINS[i], GPIO_OUT);
    }

    // octave gpio init
    gpio_init(PIN_OCT1);
    gpio_init(PIN_OCT2);
    gpio_set_dir(PIN_OCT1, GPIO_IN);
    gpio_set_dir(PIN_OCT2, GPIO_IN);
    gpio_pull_up(PIN_OCT1);
    gpio_pull_up(PIN_OCT2);

    // use more accurate PWM mode for buck-boost converter
    gpio_init(VEL_DAC_SELECT);
    gpio_set_dir(VEL_DAC_SELECT, GPIO_OUT);
    gpio_put(VEL_DAC_SELECT, 1);

    // setup the SPI for the DAC
    spi_init(SPI_PORT, 1000 * 1000);
    spi_set_format(SPI_PORT, 8, 1, 0, SPI_MSB_FIRST);

    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    // Chip select is active-low, so we'll initialise it to a driven-high state
    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);

    // adc init
    #if defined(USE_ADC_STACK_VOICES) || defined(USE_ADC_DETUNE) || defined(USE_ADC_FM) 
    adc_init();
    #ifdef USE_ADC_DETUNE
    adc_gpio_init(27);
    #endif
    #ifdef USE_ADC_STACK_VOICES
    adc_gpio_init(28);
    #endif
    #ifdef USE_ADC_FM
    adc_gpio_init(26);
    #endif
    #endif


    // init voices
    for (int i=0; i<NUM_VOICES; i++) {
        VOICES[i] = 0;
        VOICE_GATE[i] = 0;
    }

    DAC_reset();
    zero_DACs();


    while (1) {
        tud_task();
        usb_midi_task();
        serial_midi_task();
        voice_task();
        #if defined(USE_ADC_STACK_VOICES) || defined(USE_ADC_DETUNE) || defined(USE_ADC_FM) || defined(USE_OCTAVE_SWITCH)
        adc_task();
        octave_task();
        #endif
        led_blinking_task();
    }
}

void cs_select() {
    asm volatile("nop \n nop \n nop");
    gpio_put(PIN_CS, 0);  // Active low
    asm volatile("nop \n nop \n nop");
}

void vel_select() {
    asm volatile("nop \n nop \n nop");
    gpio_put(VEL_DAC_SELECT, 0);  // Active low
    asm volatile("nop \n nop \n nop");
}

void cs_deselect() {
    asm volatile("nop \n nop \n nop");
    gpio_put(PIN_CS, 1);
    asm volatile("nop \n nop \n nop");
}

void vel_deselect() {
    asm volatile("nop \n nop \n nop");
    gpio_put(VEL_DAC_SELECT, 1);
    asm volatile("nop \n nop \n nop");
}

void DAC_reset() {
    cs_select();
    rs[0] = (int_ref_on_flexible_mode >> 24);
    rs[1] = (int_ref_on_flexible_mode >> 16);
    rs[2] = (int_ref_on_flexible_mode >> 8);
    rs[3] = (int_ref_on_flexible_mode);
    spi_write_blocking(SPI_PORT, rs, 4);
  cs_deselect();
  vel_select();
    rs[0] = (int_ref_on_flexible_mode >> 24);
    rs[1] = (int_ref_on_flexible_mode >> 16);
    rs[2] = (int_ref_on_flexible_mode >> 8);
    rs[3] = (int_ref_on_flexible_mode);
    spi_write_blocking(SPI_PORT, rs, 4);
  vel_deselect();
}

void zero_DACs(){
    output_DAC(0b00000000000000000000000000000000);
    output_DAC(0b00000000000100000000000000000000);
    output_DAC(0b00000000001000000000000000000000);
    output_DAC(0b00000000001100000000000000000000);
    output_DAC(0b00000000010000000000000000000000);
    output_DAC(0b00000000010100000000000000000000);
    output_DAC(0b00000000011000000000000000000000);
    output_DAC(0b00000000011100000000000000000000);
}

void output_DAC(uint32_t sample_data){
  cs_select();
    cv[0] = (sample_data >> 24);
    cv[1] = (sample_data >> 16);
    cv[2] = (sample_data >> 8);
    cv[3] = (sample_data);
  spi_write_blocking(SPI_PORT, cv, 4);
  cs_deselect();
}

void output_VELOCITY(uint32_t sample_data){
  vel_select();
    vel[0] = (sample_data >> 24);
    vel[1] = (sample_data >> 16);
    vel[2] = (sample_data >> 8);
    vel[3] = (sample_data);
  spi_write_blocking(SPI_PORT, vel, 4);
  vel_deselect();
}

void init_sm(PIO pio, uint sm, uint offset, uint pin) {
    init_sm_pin(pio, sm, offset, pin);
    pio_sm_set_enabled(pio, sm, true);
}

void set_frequency(PIO pio, uint sm, float freq) {
    uint32_t clk_div = clock_get_hz(clk_sys) / 2 / freq;
    if (freq == 0) clk_div = 0;
    pio_sm_put(pio, sm, clk_div);
    pio_sm_exec(pio, sm, pio_encode_pull(false, false));
    pio_sm_exec(pio, sm, pio_encode_out(pio_y, 32));
}

float get_freq_from_midi_note(uint8_t note) {
    if (portamento && portamento_start != 0 && portamento_stop != 0) {
        float freq1 = pow(2, (portamento_start-69)/12.0f) * BASE_NOTE;
        float freq2 = pow(2, (portamento_stop-69)/12.0f) * BASE_NOTE;
        if (portamento_cur_freq == 0) {
            portamento_cur_freq = freq1;
        } else {
            if (freq1 < freq2) {
                portamento_cur_freq += 1.0f/(portamento_time+1);
                if (portamento_cur_freq > freq2) portamento_cur_freq = freq2;
            } else {
                portamento_cur_freq -= 1.0f/(portamento_time+1);
                if (portamento_cur_freq < freq2) portamento_cur_freq = freq2;
            }
        }
        return portamento_cur_freq;
    }

    return pow(2, (note-69)/12.0f) * BASE_NOTE;
}

void usb_midi_task() {
    if (tud_midi_available() < 4) return;

    uint8_t buff[4];

    LED_BLINK_START = board_millis();
    board_led_write(true);

    if (tud_midi_packet_read(buff)) {
        if (buff[1] == (0x90 | (MIDI_CHANNEL-1))) {
            if (buff[3] > 0) {
                note_on(buff[2], buff[3]);
            } else {
                note_off(buff[2]);
            }
        }

        if (buff[1] == (0x80 | (MIDI_CHANNEL-1))) {
            note_off(buff[2]);
        }

        if (buff[1] == (0xE0 | (MIDI_CHANNEL-1))) {
            midi_pitch_bend = buff[2] | (buff[3]<<7);
        }

        if (midi_serial_status == (0xB0 | (MIDI_CHANNEL-1))) {
            if (buff[2] == 5) { // portamento time
                portamento_time = buff[3];
            }
            if (buff[2] == 65) { // portamento on/off
                portamento = buff[3] > 63;
            }
        }
    }
}

void serial_midi_task() {
    if (!uart_is_readable(uart0)) return;

    uint8_t lsb = 0, msb = 0;

    uint8_t data = uart_getc(uart0);
    
    LED_BLINK_START = board_millis();
    board_led_write(true);

    // status
    if (data >= 0xF0 && data <= 0xF7) {
        midi_serial_status = 0;
        return;
    }

    // realtime message
    if (data >= 0xF8 && data <= 0xFF) {
        return;
    }

    if (data >= 0x80 && data <= 0xEF) {
        midi_serial_status = data;
    }

    if (midi_serial_status >= 0x80 && midi_serial_status <= 0x9F ||
        midi_serial_status >= 0xB0 && midi_serial_status <= 0xBF || // cc messages
        midi_serial_status >= 0xE0 && midi_serial_status <= 0xEF) {

        lsb = uart_getc(uart0);
        msb = uart_getc(uart0);
    }

    if (midi_serial_status == (0x90 | (MIDI_CHANNEL-1))) {
        if (msb > 0) {
            note_on(lsb, msb);
        } else {
            note_off(lsb);
        }
    }

    if (midi_serial_status == (0x80 | (MIDI_CHANNEL-1))) {
        note_off(lsb);
    }

    if (midi_serial_status == (0xE0 | (MIDI_CHANNEL-1))) {
        midi_pitch_bend = lsb | (msb<<7);
    }

    if (midi_serial_status == (0xB0 | (MIDI_CHANNEL-1))) {
        if (lsb == 5) { // portamento time
            portamento_time = msb;
        }
        if (lsb == 65) { // portamento on/off
             portamento = msb > 63;
        }
    }
}

void note_on(uint8_t note, uint8_t velocity) {
    if (STACK_VOICES < 2) {
        for (int i=0; i<NUM_VOICES; i++) {
            if (VOICE_NOTES[i] == (note) && VOICE_GATE[i]) return; // note already playing
        }
    }
    for (int i=0; i<STACK_VOICES; i++) {
        uint8_t voice_num = get_free_voice();

        VOICES[voice_num] = board_millis();
        VOICE_NOTES[voice_num] = (note);
        VOICE_GATE[voice_num] = 1;

        float freq = get_freq_from_midi_note(note) * (1 + (pow(-1, i) * DETUNE));
        //set_frequency(pio[VOICE_TO_PIO[voice_num]], VOICE_TO_SM[voice_num], freq);
        // amplitude adjustment
        //pwm_set_chan_level(RANGE_PWM_SLICES[voice_num], pwm_gpio_to_channel(RANGE_PINS[voice_num]), (int)(DIV_COUNTER*(freq*0.00025f-1/(100*freq))));
        gpio_put(GATE_PINS[voice_num], 1);
        unsigned int mV = ((note) * NOTE_SF * sfAdj[voice_num] + 0.5);
        sample_data = ((DACS[voice_num] & 0xFFF0000F) | ((mV & 0xFFFF) << 4));
        output_DAC(sample_data);
        unsigned int velmV = ((velocity) * VEL_SF);
        sample_data = ((DACS[voice_num] & 0xFFF0000F) | ((velmV & 0xFFFF) << 4));
        output_VELOCITY(sample_data);
    }
    if (portamento) {
        if (portamento_start == 0) {
            portamento_start = (note);
            portamento_cur_freq = 0.0f;
        } else {
            portamento_stop = (note);
        }
    }
    last_midi_pitch_bend = 0;
}

void note_off(uint8_t note) {
    // gate off
    for (int i=0; i<NUM_VOICES; i++) {
        if (VOICE_NOTES[i] == (note)) {
            gpio_put(GATE_PINS[i], 0);

            //VOICE_NOTES[i] = 0;
            VOICES[i] = 0;
            VOICE_GATE[i] = 0;
        }
    }
    if (portamento_stop == (note)) {
        portamento_start = portamento_stop;
        portamento_stop = 0;
        portamento_cur_freq = 0.0f;
    }
    /*
    if (portamento_start == note) {
        portamento_stop = 0;
        portamento_cur_freq = 0.0f;
    }
    */
}

uint8_t get_free_voice() {
    uint32_t oldest_time = board_millis();
    uint8_t oldest_voice = 0;

    for (int i=0; i<NUM_VOICES; i++) {
        uint8_t n = (NEXT_VOICE+i)%NUM_VOICES;

        if (VOICE_GATE[n] == 0) {
            NEXT_VOICE = (n+1)%NUM_VOICES;
            return n;
        }

        if (VOICES[i]<oldest_time) {
            oldest_time = VOICES[i];
            oldest_voice = i;
        }
    }

    NEXT_VOICE = (oldest_voice+1)%NUM_VOICES;
    return oldest_voice;
}

void voice_task() {
    if (midi_pitch_bend != last_midi_pitch_bend || DETUNE != LAST_DETUNE || portamento || FM_VALUE != LAST_FM || OCT != LAST_OCT) {

        last_midi_pitch_bend = midi_pitch_bend;
        LAST_DETUNE = DETUNE;
        LAST_FM = FM_VALUE;
        LAST_OCT = OCT;

        for (int i=0; i<NUM_VOICES; i++) {

            float freq = get_freq_from_midi_note(VOICE_NOTES[i]) * (1 - (pow(1, i) * DETUNE));

            freq += FM_VALUE * FM_INTENSITY; // Add linear frequency modulation
            freq = (freq * OCT); // octave switch
            freq = freq-(freq*((0x2000-midi_pitch_bend)/67000.0f));
            //set_frequency(pio[VOICE_TO_PIO[i]], VOICE_TO_SM[i], freq);
            //pwm_set_chan_level(RANGE_PWM_SLICES[i], pwm_gpio_to_channel(RANGE_PINS[i]), (int)(DIV_COUNTER*(freq*0.00025f-1/(100*freq))));
        }
    }
}

void adc_task() {
    uint16_t raw;

    #ifdef USE_ADC_DETUNE
    adc_select_input(1);
    raw = adc_read();
    DETUNE = map(raw, 0, 4095, 0, 50)/1000.0f;
    #endif

    #ifdef USE_ADC_STACK_VOICES
    adc_select_input(2);
    raw = adc_read();
    STACK_VOICES = map(raw, 0, 4000, 1, NUM_VOICES);
    #endif

    #ifdef USE_ADC_FM

    adc_select_input(0);
    raw = adc_read();

    // This input assumes a centre FM value of 2^11 = 2048, as the 
    // ADCs are unsigned. This can be overcome in hardware with a fixed 
    // voltage offset. The range of the Pico's ADCs is 3.3V, so a fixed 
    // offset of 1.65V is needed.
    int signed_raw = raw - (1 << 11);
    FM_VALUE = (float) signed_raw / (float) (1 << 11);
    #endif

}

void octave_task(){
    #ifdef USE_OCTAVE_SWITCH
    OCT1 = gpio_get(PIN_OCT1);
    OCT2 = gpio_get(PIN_OCT2);

    if ((OCT1 >> 0) & (OCT2 >> 0)){
        OCT = 1;
    }
    if ((OCT1 == 0) & (OCT2 >> 0)){
        OCT = 0.5;
    }
    if ((OCT2 == 0) & (OCT1 >> 0)){
        OCT = 2;
    }
    #endif
}

long map(long x, long in_min, long in_max, long out_min, long out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void led_blinking_task() {
    if (board_millis() - LED_BLINK_START < 50) return;
    board_led_write(false);
}
