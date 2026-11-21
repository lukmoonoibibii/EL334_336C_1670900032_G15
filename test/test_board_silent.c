/*
 * EL334 Mini-Project : Dino Jump
 * Step 0 - Board test program (FRDM-K64F + Multi-function Shield)
 *
 * Purpose: check every device on the shield before writing the game.
 *   Test 1 : LED D1 -> D4 light one by one
 *   Test 2 : 7-Segment - walk every segment (A,B,C,D,E,F,G,DP) on each digit,
 *            from the LEFT digit to the RIGHT digit
 *   Test 3 : (disabled) buzzer is forced OFF - silent version
 *   Test 4 : Loop - show "1234" on the display, S1/S2/S3 light D1/D2/D3
 *
 * Pin map (Arduino header -> K64F):
 *   7-Seg LATCH  D4  -> PTB23        Buzzer  D3  -> PTA1 (active LOW)
 *   7-Seg CLOCK  D7  -> PTC3         LED D1  D13 -> PTD1 (active LOW)
 *   7-Seg DATA   D8  -> PTC12        LED D2  D12 -> PTD3
 *   S1           A1  -> PTB3         LED D3  D11 -> PTD2
 *   S2           A2  -> PTB10        LED D4  D10 -> PTD0
 *   S3           A3  -> PTB11        (buttons are active LOW, pulled up on shield)
 */

#include "MK64F12.h"

#define MASK(x) (1UL << (x))

/* ---------- pin numbers ---------- */
#define LATCH_PIN   23  /* PTB23 */
#define CLK_PIN      3  /* PTC3  */
#define DATA_PIN    12  /* PTC12 */
#define BUZZER_PIN   1  /* PTA1  */
#define S1_PIN       3  /* PTB3  */
#define S2_PIN      10  /* PTB10 */
#define S3_PIN      11  /* PTB11 */

static const uint8_t LED_PIN[4] = { 1, 3, 2, 0 };       /* D1..D4 on Port D */

/* Active-low segment patterns for 0-9 (bit order DP G F E D C B A) */
static const uint8_t SEG_DIGIT[10] = {
    0xC0, 0xF9, 0xA4, 0xB0, 0x99, 0x92, 0x82, 0xF8, 0x80, 0x90
};
/* Digit enable, left -> right (same as the instructor's example) */
static const uint8_t DIGIT_SEL[4] = { 0xF1, 0xF2, 0xF4, 0xF8 };

/* ================= delay (SysTick, polling) ================= */
void delay_ms(uint32_t ms)
{
    SysTick->LOAD = (SystemCoreClock / 1000U) - 1U;   /* 1 ms */
    SysTick->VAL  = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;
    while (ms--) {
        while ((SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) == 0) { }
    }
    SysTick->CTRL = 0;
}

void delay_us(uint32_t us)
{
    SysTick->LOAD = (SystemCoreClock / 1000000U) - 1U; /* 1 us */
    SysTick->VAL  = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;
    while (us--) {
        while ((SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) == 0) { }
    }
    SysTick->CTRL = 0;
}

/* ================= GPIO init ================= */
void gpio_init(void)
{
    SIM->SCGC5 |= SIM_SCGC5_PORTA_MASK | SIM_SCGC5_PORTB_MASK |
                  SIM_SCGC5_PORTC_MASK | SIM_SCGC5_PORTD_MASK;

    /* 7-segment control pins -> GPIO output */
    PORTB->PCR[LATCH_PIN] = PORT_PCR_MUX(1);
    PORTC->PCR[CLK_PIN]   = PORT_PCR_MUX(1);
    PORTC->PCR[DATA_PIN]  = PORT_PCR_MUX(1);
    PTB->PDDR |= MASK(LATCH_PIN);
    PTC->PDDR |= MASK(CLK_PIN) | MASK(DATA_PIN);
    PTB->PCOR  = MASK(LATCH_PIN);
    PTC->PCOR  = MASK(CLK_PIN);

    /* Buzzer -> start as INPUT (Hi-Z) = OFF.
       The shield drives the buzzer with a PNP (3906) whose emitter is at 5 V.
       A 3.3 V HIGH cannot fully turn it off, so we release the pin instead. */
    PORTA->PCR[BUZZER_PIN] = PORT_PCR_MUX(1);
    PTA->PCOR  = MASK(BUZZER_PIN);      /* output latch = LOW (used when ON) */
    PTA->PDDR &= ~MASK(BUZZER_PIN);     /* input = buzzer OFF               */

    /* LEDs -> output, OFF (HIGH) */
    for (int i = 0; i < 4; i++) {
        PORTD->PCR[LED_PIN[i]] = PORT_PCR_MUX(1);
        PTD->PSOR  = MASK(LED_PIN[i]);
        PTD->PDDR |= MASK(LED_PIN[i]);
    }

    /* Switches -> input */
    PORTB->PCR[S1_PIN] = PORT_PCR_MUX(1);
    PORTB->PCR[S2_PIN] = PORT_PCR_MUX(1);
    PORTB->PCR[S3_PIN] = PORT_PCR_MUX(1);
    PTB->PDDR &= ~(MASK(S1_PIN) | MASK(S2_PIN) | MASK(S3_PIN));
}

/* ================= LED ================= */
void led_set(int idx, int on)            /* idx 0..3 = D1..D4 */
{
    if (on) PTD->PCOR = MASK(LED_PIN[idx]);   /* active LOW */
    else    PTD->PSOR = MASK(LED_PIN[idx]);
}

/* ================= Buzzer ================= */
void buzzer_set(int on)
{
    if (on) PTA->PDDR |=  MASK(BUZZER_PIN);   /* output LOW -> buzzer ON  */
    else    PTA->PDDR &= ~MASK(BUZZER_PIN);   /* input/Hi-Z -> buzzer OFF */
}

/* ================= Switch ================= */
int switch_pressed(int pin)                /* returns 1 when pressed */
{
    return (PTB->PDIR & MASK(pin)) == 0;
}

/* ================= 7-Segment (74HC595 x2) ================= */
static void shift_byte(uint8_t b)          /* MSB first */
{
    for (int i = 7; i >= 0; i--) {
        if (b & (1U << i)) PTC->PSOR = MASK(DATA_PIN);
        else               PTC->PCOR = MASK(DATA_PIN);
        PTC->PSOR = MASK(CLK_PIN);         /* rising edge shifts the bit in */
        PTC->PCOR = MASK(CLK_PIN);
    }
}

void seg_write(uint8_t segments, uint8_t digit_sel)
{
    PTB->PCOR = MASK(LATCH_PIN);
    shift_byte(segments);                  /* first byte -> segment register */
    shift_byte(digit_sel);                 /* second byte -> digit register  */
    PTB->PSOR = MASK(LATCH_PIN);           /* latch both outputs together    */
}

void seg_off(void) { seg_write(0xFF, 0xF0); }

/* ================= Tests ================= */
void test_leds(void)
{
    for (int i = 0; i < 4; i++) {
        led_set(i, 1);
        delay_ms(400);
        led_set(i, 0);
    }
}

void test_segments(void)
{
    /* bit 0..7 = A B C D E F G DP ; one segment lit at a time */
    for (int d = 0; d < 4; d++) {
        for (int bit = 0; bit < 8; bit++) {
            seg_write((uint8_t)~(1U << bit), DIGIT_SEL[d]);
            delay_ms(300);
        }
    }
    seg_off();
}

void test_buzzer(void)
{
    /* (a) steady ON 300 ms */
    buzzer_set(1);
    delay_ms(300);
    buzzer_set(0);
    delay_ms(500);

    /* (b) toggle at ~1 kHz for 300 ms (passive-buzzer test) */
    for (int i = 0; i < 300; i++) {
        buzzer_set(1); delay_us(500);
        buzzer_set(0); delay_us(500);
    }
}

void show_1234_once(void)
{
    static const uint8_t val[4] = { 1, 2, 3, 4 };
    for (int d = 0; d < 4; d++) {
        seg_write(SEG_DIGIT[val[d]], DIGIT_SEL[d]);
        delay_ms(2);
    }
}

void test_switch_loop(void)
{
    while (1) {
        show_1234_once();
        led_set(0, switch_pressed(S1_PIN));
        led_set(1, switch_pressed(S2_PIN));
        led_set(2, switch_pressed(S3_PIN));
    }
}

/* ================= main ================= */
int main(void)
{
    gpio_init();
    seg_off();

    test_leds();
    test_segments();
    /* test_buzzer();  -- disabled: silent version */
    test_switch_loop();   /* never returns */

    return 0;
}
