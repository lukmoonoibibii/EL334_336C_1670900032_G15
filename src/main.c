/*
 * ============================================================================
 *  EL334 Mini-Project : DINO JUMP  (FRDM-K64F + Arduino Multi-function Shield)
 * ============================================================================
 *
 *  Game
 *    - The dino lives on the LEFT digit of the 7-segment display.
 *        standing  = tall shape on the left       (segments E F G)
 *        jumping   = small box in the UPPER half  (segments A B F G)
 *        ducking   = small arch in the LOWER half (segments C E G)
 *    - Obstacles move from the RIGHT digit to the left:
 *        cactus "_" (bottom segment D) -> press S1 to JUMP over it
 *        bird   "-" (top segment A)    -> press S2 to DUCK under it
 *      Each obstacle passed = 1 point. Birds appear from 5 points on.
 *    - 3 lives (LED D1-D3). The game gets faster every 8 points.
 *
 *  Controls
 *    Menu  : turn the potentiometer to pick level 1-3 ("LEV1".."LEV3"),
 *            S2 = background music ON/OFF (LED D4 on = music ON),
 *            press S1 to start
 *    Play  : S1 = jump, S2 = duck,
 *            S3 once = pause / resume, S3 twice quickly = back to menu
 *    Over  : score is shown (all LEDs blink on a new high score),
 *            S1 = play again, S3 = back to the menu
 *
 *  Peripherals used (each has its own functions below)
 *    GPIO       : LEDs D1-D4, 74HC595 control lines, buzzer
 *    PORTB IRQ  : S1/S2/S3 falling-edge interrupts           (Interrupt)
 *    PIT ch0    : 1 ms system tick -> display scan, time base,
 *                 melody sequencing                           (Timer + Interrupt)
 *    PIT ch1    : buzzer tone generator (square wave)         (Timer + Interrupt)
 *    ADC0       : potentiometer -> difficulty level
 *
 *  Pin map (Arduino header -> K64F)
 *    7-Seg LATCH D4 -> PTB23   CLOCK D7 -> PTC3   DATA D8 -> PTC12
 *    Buzzer      D3 -> PTA1    (PNP driver, LOW = ON, released/Hi-Z = OFF)
 *    LED D1..D4  D13,D12,D11,D10 -> PTD1, PTD3, PTD2, PTD0 (LOW = ON)
 *    S1, S2, S3  A1, A2, A3      -> PTB3, PTB10, PTB11     (pressed = LOW)
 *    Pot         A0              -> PTB2 = ADC0_SE12
 * ============================================================================
 */

#include "MK64F12.h"

#define MASK(x) (1UL << (x))

/* ------------------------------------------------------------------ pins -- */
#define LATCH_PIN   23      /* PTB23 */
#define CLK_PIN      3      /* PTC3  */
#define DATA_PIN    12      /* PTC12 */
#define BUZZER_PIN   1      /* PTA1  */
#define POT_PIN      2      /* PTB2  */
#define POT_ADC_CH  12      /* ADC0_SE12 */
#define S1_PIN       3      /* PTB3  */
#define S2_PIN      10      /* PTB10 */
#define S3_PIN      11      /* PTB11 */

static const uint8_t LED_PIN[4] = { 1, 3, 2, 0 };     /* D1..D4 on Port D */

/* ------------------------------------------------------ 7-segment glyphs -- */
/* active LOW, bit order: DP G F E D C B A  (0 = segment ON) */
static const uint8_t SEG_DIGIT[10] = {
    0xC0, 0xF9, 0xA4, 0xB0, 0x99, 0x92, 0x82, 0xF8, 0x80, 0x90
};
#define SEG_BLANK   0xFF
#define SEG_DASH    0xBF    /* G           */
#define SEG_L       0xC7    /* D E F       */
#define SEG_E       0x86    /* A D E F G   */
#define SEG_U       0xC1    /* B C D E F   */
#define SEG_P       0x8C    /* A B E F G   */
#define SEG_A       0x88    /* A B C E F G */
#define SEG_S       0x92    /* same as 5   */
#define SEG_ALL     0x00    /* every segment */
#define SEG_G       0xC2    /* A C D E F   */
#define SEG_M       0xC8    /* A B C E F  (looks like an upside-down U) */
#define SEG_O       0xC0    /* same as 0   */
#define SEG_R       0xAF    /* E G  (small r) */

/* "GAME OVEr" scrolling across the 4 digits (blanks let it enter/leave) */
static const uint8_t TEXT_GAME_OVER[] = {
    SEG_BLANK, SEG_BLANK, SEG_BLANK, SEG_BLANK,
    SEG_G, SEG_A, SEG_M, SEG_E, SEG_BLANK, SEG_O, SEG_U, SEG_E, SEG_R,
    SEG_BLANK, SEG_BLANK, SEG_BLANK, SEG_BLANK
};
#define SCROLL_STEP_MS  280

#define DINO_STAND  0x8F    /* E F G   : tall, standing   */
#define DINO_AIR    0x9C    /* A B F G : upper box, jump  */
#define DINO_DUCK   0xAB    /* C E G   : low arch, duck   */
#define CACTUS      0xF7    /* D       : bottom bar       */
#define BIRD        0xFE    /* A       : top bar          */

/* obstacle[] values */
#define OBS_NONE    0
#define OBS_CACTUS  1
#define OBS_BIRD    2

static const uint8_t DIGIT_SEL[4] = { 0xF1, 0xF2, 0xF4, 0xF8 };  /* left->right */

/* ---------------------------------------------------------------- notes -- */
#define REST   0
#define G3   196
#define B3   247
#define C4   262
#define D4   294
#define E4   330
#define F4   349
#define GS4  415
#define A4   440
#define B4   494
#define G4   392
#define C5   523
#define D5   587
#define DS5  622
#define E5   659
#define F5   698
#define G5   784
#define FS5  740
#define GS5  831
#define A5   880
#define B5   988
#define C6  1047
#define D6  1175
#define DS6 1245
#define E6  1319

typedef struct { uint16_t freq; uint16_t ms; } Note;

static const Note MEL_START[]  = { {C5,80},{E5,80},{G5,80},{C6,180} };
static const Note MEL_JUMP[]   = { {E6,35} };
static const Note MEL_DUCK[]   = { {C5,35} };
static const Note MEL_POINT[]  = { {C6,15} };
static const Note MEL_HIT[]    = { {G4,120},{REST,40},{G4,250} };
static const Note MEL_OVER[]   = {   /* original 8-bit style "game over" jingle
                                        (one octave lower = softer on the ear) */
    {C5,100},{G4,100},{E4,100},{C4,300},{REST,120},     /* quick fall down     */
    {D4,120},{F4,120},{A4,240},{GS4,320},{REST,100},    /* climb... sour note  */
    {G4,120},{E4,120},{D4,120},{B3,300},{REST,80},      /* stumble down        */
    {C4,150},{G3,150},{C4,600},                         /* "dun - dun - duuun" */
};
static const Note MEL_RECORD[] = { {C5,100},{E5,100},{G5,100},{C6,200},
                                   {REST,60},{G5,100},{C6,450} };
#define LEN(a) (sizeof(a) / sizeof((a)[0]))

/* --- background music (original tunes, loop forever) --- */
/* menu: "The Entertainer" A-theme - Scott Joplin (1902, public domain)
 *       1 sixteenth = 130 ms                                              */
#define T16 130
static const Note BGM_MENU[] = {
    {D5,T16},{DS5,T16},{E5,T16},{C6,2*T16},{E5,T16},{C6,2*T16},{E5,T16},{C6,6*T16},
    {C6,T16},{D6,T16},{DS6,T16},{E6,T16},{C6,T16},{D6,T16},{E6,2*T16},{B5,T16},
    {D6,2*T16},{C6,6*T16},{D5,T16},{DS5,T16},{E5,T16},{C6,2*T16},{E5,T16},{C6,2*T16},
    {E5,T16},{C6,6*T16},{A5,T16},{G5,T16},{FS5,T16},{A5,T16},{C6,T16},{E6,2*T16},
    {D6,T16},{C6,T16},{A5,T16},{D6,6*T16},{D5,T16},{DS5,T16},{E5,T16},{C6,2*T16},
    {E5,T16},{C6,2*T16},{E5,T16},{C6,6*T16},{C6,T16},{D6,T16},{DS6,T16},{E6,T16},
    {C6,T16},{D6,T16},{E6,2*T16},{B5,T16},{D6,2*T16},{C6,6*T16},{REST,8*T16},
};
#define MUSIC_DUTY   12   /* % ON time of the square wave for music  (quiet) */
#define SFX_DUTY     50   /* % ON time for sound effects             (loud)  */
#define SOFT_DUTY    15   /* % ON time for long jingles (game over / record) */
#define BGM_GAP_MS   30   /* short silence at the end of each note so
                             repeated notes are heard separately          */

/* ---------------------------------------------------------- game tuning -- */
#define JUMP_TICKS        3     /* game ticks the dino stays in the air      */
#define DUCK_TICKS        3     /* game ticks the dino stays ducked          */
#define BIRD_FROM_SCORE   5     /* birds start appearing at this score       */
#define BIRD_CHANCE      25     /* % of new obstacles that are birds         */
#define MIN_GAP           4     /* min ticks between two obstacles           */
#define DOUBLE_PRESS_MS 400     /* S3 pressed twice within this = menu       */
#define LIVES_START       3
#define HIT_PAUSE_MS    900
static const uint16_t LEVEL_TICK_MS[3] = { 550, 430, 330 };
#define SPEEDUP_EVERY     8     /* points                                    */
#define SPEEDUP_MS       15
#define MIN_TICK_MS     200

typedef enum { ST_MENU, ST_PLAY, ST_PAUSE, ST_HIT, ST_OVER } GameState;

/* ================================================================ shared == */
static volatile uint32_t ms_ticks = 0;          /* incremented by PIT0 (1 ms) */
static volatile uint8_t  disp[4]  = { SEG_BLANK, SEG_BLANK, SEG_BLANK, SEG_BLANK };

static volatile uint8_t  btn_event[3] = { 0, 0, 0 };   /* set by PORTB ISR   */
static uint32_t          btn_last_ms[3] = { 0, 0, 0 }; /* for debouncing     */
#define DEBOUNCE_MS     150

/* sound effect player (priority over music, driven from PIT0 ISR) */
static const Note *volatile mel_ptr = 0;
static volatile uint8_t  mel_len = 0, mel_idx = 0;
static volatile uint16_t mel_left_ms = 0;
static volatile uint16_t sfx_freq = 0;
static volatile uint8_t  sfx_duty = SFX_DUTY;

/* background music player (loops, driven from PIT0 ISR) */
static const Note *volatile bgm_ptr = 0;
static volatile uint8_t  bgm_len = 0, bgm_idx = 0;
static volatile uint16_t bgm_left_ms = 0;
static volatile uint16_t bgm_freq = 0;
static volatile uint8_t  bgm_paused = 0;
static uint8_t           music_on = 1;         /* toggled with S2 in menu */

static volatile uint16_t tone_now = 0;         /* frequency playing now   */
static volatile uint8_t  duty_now = 0;         /* duty cycle playing now  */

/* tone generator state (PIT1) */
static volatile uint32_t tone_len_on = 0, tone_len_off = 0;
static volatile uint8_t  tone_is_on = 0;

/* game variables */
static GameState state = ST_MENU;
static uint8_t   level = 1;
static uint8_t   obstacle[4];          /* OBS_NONE / OBS_CACTUS / OBS_BIRD  */
static uint8_t   air  = 0;             /* ticks left in the air, 0 = ground */
static uint8_t   duck = 0;             /* ticks left ducking                */
static uint32_t  s3_last_ms = 0;       /* for S3 double-press detection     */
static uint8_t   lives = LIVES_START;
static uint16_t  score = 0, high_score = 0;
static uint16_t  tick_ms = 420;
static uint8_t   since_spawn = 0;
static uint32_t  state_ms = 0;         /* time stamp of last state change   */
static uint32_t  rng = 12345;

/* =========================================================== utilities == */
static uint32_t millis(void) { return ms_ticks; }

static uint32_t random_next(void)      /* linear congruential generator */
{
    rng = rng * 1103515245UL + 12345UL;
    return (rng >> 16) & 0x7FFF;
}

static uint32_t bus_clock_hz(void)
{
    uint32_t outdiv1 = ((SIM->CLKDIV1 & SIM_CLKDIV1_OUTDIV1_MASK) >> SIM_CLKDIV1_OUTDIV1_SHIFT) + 1;
    uint32_t outdiv2 = ((SIM->CLKDIV1 & SIM_CLKDIV1_OUTDIV2_MASK) >> SIM_CLKDIV1_OUTDIV2_SHIFT) + 1;
    return (SystemCoreClock * outdiv1) / outdiv2;   /* MCGOUT / OUTDIV2 */
}

/* ================================================================= LED == */
void led_init(void)
{
    SIM->SCGC5 |= SIM_SCGC5_PORTD_MASK;
    for (int i = 0; i < 4; i++) {
        PORTD->PCR[LED_PIN[i]] = PORT_PCR_MUX(1);
        PTD->PSOR  = MASK(LED_PIN[i]);           /* OFF (active LOW) */
        PTD->PDDR |= MASK(LED_PIN[i]);
    }
}

void led_set(int idx, int on)                    /* idx 0..3 = D1..D4 */
{
    if (on) PTD->PCOR = MASK(LED_PIN[idx]);
    else    PTD->PSOR = MASK(LED_PIN[idx]);
}

void led_show_lives(uint8_t n)                   /* D1..D3 = lives */
{
    for (int i = 0; i < 3; i++) led_set(i, i < n);
}

void led_all(int on)
{
    for (int i = 0; i < 4; i++) led_set(i, on);
}

/* ========================================================== 7-segment == */
void display_init(void)
{
    SIM->SCGC5 |= SIM_SCGC5_PORTB_MASK | SIM_SCGC5_PORTC_MASK;
    PORTB->PCR[LATCH_PIN] = PORT_PCR_MUX(1);
    PORTC->PCR[CLK_PIN]   = PORT_PCR_MUX(1);
    PORTC->PCR[DATA_PIN]  = PORT_PCR_MUX(1);
    PTB->PDDR |= MASK(LATCH_PIN);
    PTC->PDDR |= MASK(CLK_PIN) | MASK(DATA_PIN);
    PTB->PCOR  = MASK(LATCH_PIN);
    PTC->PCOR  = MASK(CLK_PIN);
}

static void shift_byte(uint8_t b)                /* MSB first into 74HC595 */
{
    for (int i = 7; i >= 0; i--) {
        if (b & (1U << i)) PTC->PSOR = MASK(DATA_PIN);
        else               PTC->PCOR = MASK(DATA_PIN);
        PTC->PSOR = MASK(CLK_PIN);
        PTC->PCOR = MASK(CLK_PIN);
    }
}

static void display_write(uint8_t segments, uint8_t digit_sel)
{
    PTB->PCOR = MASK(LATCH_PIN);
    shift_byte(segments);
    shift_byte(digit_sel);
    PTB->PSOR = MASK(LATCH_PIN);
}

/* called every 1 ms from PIT0 ISR: lights ONE digit per call (multiplexing) */
static void display_scan(void)
{
    static uint8_t pos = 0;
    display_write(disp[pos], DIGIT_SEL[pos]);
    pos = (pos + 1) & 3;
}

void display_set(uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3)
{
    disp[0] = d0; disp[1] = d1; disp[2] = d2; disp[3] = d3;
}

void display_number(uint16_t n)                  /* right aligned, no leading 0 */
{
    uint8_t buf[4];
    for (int i = 3; i >= 0; i--) {
        buf[i] = (n > 0 || i == 3) ? SEG_DIGIT[n % 10] : SEG_BLANK;
        n /= 10;
    }
    display_set(buf[0], buf[1], buf[2], buf[3]);
}

/* ============================================================= buzzer == */
/*  Q1 on the shield is a PNP with its emitter at 5 V. A 3.3 V HIGH cannot
 *  switch it fully off (it hums), so OFF = pin released as input (Hi-Z),
 *  ON = pin driven LOW.                                                     */
void buzzer_init(void)
{
    SIM->SCGC5 |= SIM_SCGC5_PORTA_MASK;
    PORTA->PCR[BUZZER_PIN] = PORT_PCR_MUX(1);
    PTA->PCOR  = MASK(BUZZER_PIN);               /* output latch = LOW */
    PTA->PDDR &= ~MASK(BUZZER_PIN);              /* input = OFF        */
}

static void buzzer_off(void)    { PTA->PDDR &= ~MASK(BUZZER_PIN); }

/* ============================================================== timers == */
/*  PIT channel 0 : 1 ms tick.   PIT channel 1 : tone generator.           */
void timer_init(void)
{
    SIM->SCGC6 |= SIM_SCGC6_PIT_MASK;
    PIT->MCR = 0;                                /* enable PIT module */

    /* channel 0 : 1 kHz */
    PIT->CHANNEL[0].LDVAL = bus_clock_hz() / 1000U - 1U;
    PIT->CHANNEL[0].TFLG  = PIT_TFLG_TIF_MASK;
    PIT->CHANNEL[0].TCTRL = PIT_TCTRL_TIE_MASK | PIT_TCTRL_TEN_MASK;

    /* channel 1 : stopped until a note is played */
    PIT->CHANNEL[1].TCTRL = 0;
    PIT->CHANNEL[1].TFLG  = PIT_TFLG_TIF_MASK;

    NVIC_SetPriority(PIT1_IRQn, 1);              /* tone: highest of ours */
    NVIC_SetPriority(PIT0_IRQn, 2);
    NVIC_EnableIRQ(PIT0_IRQn);
    NVIC_EnableIRQ(PIT1_IRQn);
}

/* Square wave on the buzzer with an adjustable duty cycle.
 * Shorter ON time -> less energy into the buzzer -> quieter sound.
 * PIT1 alternates between an ON period and an OFF period.
 * (A new LDVAL is used after the current period ends, so the ISR always
 *  writes the length of the period AFTER the one that has just started.) */
static void tone_start(uint16_t freq, uint8_t duty)
{
    PIT->CHANNEL[1].TCTRL = 0;
    buzzer_off();
    tone_is_on = 0;
    if (freq == 0) return;                       /* REST */

    uint32_t period = bus_clock_hz() / freq;
    uint32_t on     = period * duty / 100U;
    if (on < 2) on = 2;
    tone_len_on  = on - 1U;
    tone_len_off = (period - on) - 1U;

    PIT->CHANNEL[1].LDVAL = tone_len_off;        /* first period: OFF */
    PIT->CHANNEL[1].TFLG  = PIT_TFLG_TIF_MASK;
    PIT->CHANNEL[1].TCTRL = PIT_TCTRL_TIE_MASK | PIT_TCTRL_TEN_MASK;
    PIT->CHANNEL[1].LDVAL = tone_len_on;         /* next period: ON   */
}

/* ============================================================== sound == */
void sound_play_duty(const Note *melody, uint8_t len, uint8_t duty)
{
    __disable_irq();
    sfx_duty    = duty;
    mel_ptr     = melody;
    mel_len     = len;
    mel_idx     = 0;
    mel_left_ms = 0;                             /* ISR loads first note */
    __enable_irq();
}

void sound_play(const Note *melody, uint8_t len)    /* normal effect */
{
    sound_play_duty(melody, len, SFX_DUTY);
}

void music_start(const Note *song, uint8_t len)    /* loop from the start */
{
    __disable_irq();
    if (music_on) {
        bgm_ptr = song;
        bgm_len = len;
    } else {
        bgm_ptr = 0;
    }
    bgm_idx     = 0;
    bgm_left_ms = 0;
    bgm_paused  = 0;
    __enable_irq();
}

void music_stop(void)          { bgm_ptr = 0; }
void music_pause(int paused)   { bgm_paused = (uint8_t)paused; }

void sound_stop(void)                            /* stop everything */
{
    __disable_irq();
    mel_ptr = 0;
    bgm_ptr = 0;
    tone_now = 0;
    tone_start(0, 0);
    __enable_irq();
}

/* called every 1 ms from PIT0 ISR
 * - advances the sound effect and the background music
 * - a sound effect (if any) is heard, otherwise the music              */
static void sound_update(void)
{
    uint16_t want = 0;
    uint8_t  duty = SFX_DUTY;

    /* sound effect */
    if (mel_ptr != 0) {
        if (mel_left_ms == 0) {
            if (mel_idx >= mel_len) {
                mel_ptr = 0;                     /* effect finished */
            } else {
                sfx_freq    = mel_ptr[mel_idx].freq;
                mel_left_ms = mel_ptr[mel_idx].ms;
                mel_idx++;
            }
        }
        if (mel_ptr != 0) mel_left_ms--;
    }

    /* background music */
    if (bgm_ptr != 0 && !bgm_paused) {
        if (bgm_left_ms == 0) {
            bgm_freq    = bgm_ptr[bgm_idx].freq;
            bgm_left_ms = bgm_ptr[bgm_idx].ms;
            bgm_idx     = (uint8_t)((bgm_idx + 1) % bgm_len);   /* loop */
        }
        bgm_left_ms--;
    }

    /* choose what to hear */
    if (mel_ptr != 0) {
        want = sfx_freq;
        duty = sfx_duty;
    }
    else if (bgm_ptr != 0 && !bgm_paused && bgm_left_ms > BGM_GAP_MS) {
        want = bgm_freq;
        duty = MUSIC_DUTY;
    }

    if (want != tone_now || duty != duty_now) {
        tone_now = want;
        duty_now = duty;
        tone_start(want, duty);
    }
}

/* ============================================================ switches == */
/*  S1/S2/S3 generate a PORTB interrupt on the falling edge (press).       */
void switch_init(void)
{
    SIM->SCGC5 |= SIM_SCGC5_PORTB_MASK;
    PORTB->PCR[S1_PIN] = PORT_PCR_MUX(1) | PORT_PCR_IRQC(0xA) | PORT_PCR_ISF_MASK;
    PORTB->PCR[S2_PIN] = PORT_PCR_MUX(1) | PORT_PCR_IRQC(0xA) | PORT_PCR_ISF_MASK;
    PORTB->PCR[S3_PIN] = PORT_PCR_MUX(1) | PORT_PCR_IRQC(0xA) | PORT_PCR_ISF_MASK;
    PTB->PDDR &= ~(MASK(S1_PIN) | MASK(S2_PIN) | MASK(S3_PIN));

    NVIC_SetPriority(PORTB_IRQn, 3);
    NVIC_EnableIRQ(PORTB_IRQn);
}

/* returns 1 once per press (event consumed) */
int switch_event(int idx)
{
    if (btn_event[idx]) { btn_event[idx] = 0; return 1; }
    return 0;
}

static void switch_clear_events(void)
{
    btn_event[0] = btn_event[1] = btn_event[2] = 0;
}

/* ================================================================= ADC == */
void adc_init(void)
{
    SIM->SCGC6 |= SIM_SCGC6_ADC0_MASK;
    SIM->SCGC5 |= SIM_SCGC5_PORTB_MASK;
    PORTB->PCR[POT_PIN] = PORT_PCR_MUX(0);       /* analog function */
    ADC0->CFG1 = ADC_CFG1_ADIV(3) | ADC_CFG1_MODE(1) | ADC_CFG1_ADICLK(0); /* 12-bit */
    ADC0->SC2  = 0;                              /* software trigger */
}

uint16_t adc_read_pot(void)                      /* 0 .. 4095 */
{
    ADC0->SC1[0] = ADC_SC1_ADCH(POT_ADC_CH);
    while ((ADC0->SC1[0] & ADC_SC1_COCO_MASK) == 0) { }
    return (uint16_t)ADC0->R[0];
}

/* =================================================== interrupt handlers == */
void PIT0_IRQHandler(void)                       /* every 1 ms */
{
    PIT->CHANNEL[0].TFLG = PIT_TFLG_TIF_MASK;
    ms_ticks++;
    display_scan();
    sound_update();
}

void PIT1_IRQHandler(void)                       /* end of an ON/OFF period */
{
    PIT->CHANNEL[1].TFLG = PIT_TFLG_TIF_MASK;
    tone_is_on = !tone_is_on;
    if (tone_is_on) {
        PTA->PDDR |= MASK(BUZZER_PIN);           /* buzzer ON (drive LOW) */
        PIT->CHANNEL[1].LDVAL = tone_len_off;    /* period after this one */
    } else {
        PTA->PDDR &= ~MASK(BUZZER_PIN);          /* buzzer OFF (Hi-Z)     */
        PIT->CHANNEL[1].LDVAL = tone_len_on;
    }
}

void PORTB_IRQHandler(void)
{
    uint32_t flags = PORTB->ISFR;
    PORTB->ISFR = flags;                         /* clear (write 1) */

    static const uint8_t pins[3] = { S1_PIN, S2_PIN, S3_PIN };
    uint32_t now = ms_ticks;
    for (int i = 0; i < 3; i++) {
        if ((flags & MASK(pins[i])) && (now - btn_last_ms[i] > DEBOUNCE_MS)) {
            btn_last_ms[i] = now;
            btn_event[i]   = 1;
        }
    }
}

/* ================================================================ game == */
static void game_change_state(GameState s)
{
    state    = s;
    state_ms = millis();
}

static uint8_t obstacle_glyph(uint8_t o)
{
    if (o == OBS_CACTUS) return CACTUS;
    if (o == OBS_BIRD)   return BIRD;
    return SEG_BLANK;
}

static void game_render(void)
{
    uint8_t d0 = (air > 0) ? DINO_AIR : (duck > 0) ? DINO_DUCK : DINO_STAND;
    d0 &= obstacle_glyph(obstacle[0]);           /* AND = draw both (active LOW) */
    display_set(d0,
                obstacle_glyph(obstacle[1]),
                obstacle_glyph(obstacle[2]),
                obstacle_glyph(obstacle[3]));
}

static void game_clear_obstacles(void)
{
    for (int i = 0; i < 4; i++) obstacle[i] = 0;
    since_spawn = 0;
    air  = 0;
    duck = 0;
}

static void game_update_speed(void);

static void game_start(void)
{
    rng = millis() | 1U;                         /* seed from human timing */
    score = 0;
    lives = LIVES_START;
    game_clear_obstacles();
    game_update_speed();
    led_all(0);
    led_show_lives(lives);
    sound_play(MEL_START, LEN(MEL_START));
    music_stop();                                /* no music while playing */
    switch_clear_events();
    game_change_state(ST_PLAY);
}

static void game_to_menu(void)
{
    sound_stop();
    switch_clear_events();
    music_start(BGM_MENU, LEN(BGM_MENU));
    game_change_state(ST_MENU);
}

/* S3: returns 1 if this press is the 2nd press of a double press */
static int s3_is_double(void)
{
    uint32_t now = millis();
    int dbl = (now - s3_last_ms) < DOUBLE_PRESS_MS;
    s3_last_ms = dbl ? 0 : now;                  /* reset after a double */
    return dbl;
}

static void game_update_speed(void)
{
    int t = LEVEL_TICK_MS[level - 1] - (score / SPEEDUP_EVERY) * SPEEDUP_MS;
    tick_ms = (t < MIN_TICK_MS) ? MIN_TICK_MS : (uint16_t)t;
}

void game_menu(void)
{
    level = (uint8_t)(adc_read_pot() * 3U / 4096U) + 1U;   /* 1..3 */
    display_set(SEG_L, SEG_E, SEG_U, SEG_DIGIT[level]);
    led_all(0);
    led_set(3, music_on);                        /* D4 = music ON indicator */

    if (switch_event(1)) {                       /* S2 = music ON / OFF */
        music_on = !music_on;
        if (music_on) music_start(BGM_MENU, LEN(BGM_MENU));
        else          sound_stop();
    }
    switch_event(2);                             /* S3 does nothing here */
    if (switch_event(0)) game_start();           /* S1 = start */
}

/* one step of the world: cacti move left, collision, landing */
static void game_tick(void)
{
    int passed = obstacle[0];                    /* cactus under the dino now */

    /* 1) move cacti one digit to the left */
    obstacle[0] = obstacle[1];
    obstacle[1] = obstacle[2];
    obstacle[2] = obstacle[3];
    obstacle[3] = 0;

    /* 2) spawn a new obstacle on the right (keep a gap the dino can clear) */
    since_spawn++;
    if (obstacle[2] == 0 && obstacle[1] == 0 && since_spawn >= MIN_GAP) {
        if ((random_next() % 100) < 50 || since_spawn >= MIN_GAP + 3) {
            int bird = (score >= BIRD_FROM_SCORE) &&
                       ((random_next() % 100) < BIRD_CHANCE);
            obstacle[3] = bird ? OBS_BIRD : OBS_CACTUS;
            since_spawn = 0;
        }
    }

    /* 3) score: the cactus that was under the dino has gone */
    if (passed) {
        score++;
        game_update_speed();
        sound_play(MEL_POINT, LEN(MEL_POINT));
        led_set(3, 1);                           /* D4 flashes on a point */
    } else {
        led_set(3, 0);
    }

    /* 4) collision
     *    cactus hits the dino unless it is jumping
     *    bird   hits the dino unless it is ducking                          */
    int hit = (obstacle[0] == OBS_CACTUS && air  == 0) ||
              (obstacle[0] == OBS_BIRD   && duck == 0);
    if (hit) {
        lives--;
        led_show_lives(lives);
        led_set(3, 0);
        display_set(SEG_ALL, SEG_DASH, SEG_DASH, SEG_DASH);
        if (lives == 0) {
            music_stop();
            sound_play_duty(MEL_OVER, LEN(MEL_OVER), SOFT_DUTY);
            if (score > high_score) high_score = score;
            game_change_state(ST_OVER);
        } else {
            sound_play(MEL_HIT, LEN(MEL_HIT));
            game_change_state(ST_HIT);
        }
        return;
    }

    /* 5) gravity / stand up again */
    if (air  > 0) air--;
    if (duck > 0) duck--;

    game_render();
}

void game_play(void)
{
    static uint32_t last_tick = 0;

    if (switch_event(2)) {                       /* S3 */
        if (s3_is_double()) { game_to_menu(); return; }   /* twice = menu  */
        display_set(SEG_P, SEG_A, SEG_U, SEG_S);          /* once  = pause */
        mel_ptr = 0;                                      /* cut effect    */
        music_pause(1);
        game_change_state(ST_PAUSE);
        return;
    }
    if (switch_event(0) && air == 0 && duck == 0) {   /* S1 = jump */
        air = JUMP_TICKS;
        sound_play(MEL_JUMP, LEN(MEL_JUMP));
        game_render();                           /* react immediately */
    }
    if (switch_event(1) && air == 0 && duck == 0) {   /* S2 = duck */
        duck = DUCK_TICKS;
        sound_play(MEL_DUCK, LEN(MEL_DUCK));
        game_render();
    }

    if (millis() - last_tick >= tick_ms) {
        last_tick = millis();
        game_tick();
    }
}

void game_pause(void)
{
    switch_event(0);                             /* S1, S2 ignored */
    switch_event(1);
    if (switch_event(2)) {                       /* S3 */
        if (s3_is_double()) { game_to_menu(); return; }   /* twice = menu   */
        switch_clear_events();                            /* once  = resume */
        music_pause(0);
        game_render();
        game_change_state(ST_PLAY);
    }
}

void game_hit(void)
{
    uint32_t t = millis() - state_ms;
    if (t < 300) return;                         /* crash picture */
    display_number(score);                       /* then show the score */
    if (t >= HIT_PAUSE_MS) {
        game_clear_obstacles();
        switch_clear_events();
        game_render();
        game_change_state(ST_PLAY);
    }
}

void game_over(void)
{
    uint32_t t = millis() - state_ms;
    int new_record = (score == high_score && score > 0);

    if (t < 600) return;                         /* crash picture */

    /* phase 1: scroll "GAME OVEr" from right to left */
    uint32_t frames = sizeof(TEXT_GAME_OVER) - 3;          /* window positions */
    uint32_t scroll_end = 600 + frames * SCROLL_STEP_MS;
    if (t < scroll_end) {
        uint32_t f = (t - 600) / SCROLL_STEP_MS;
        display_set(TEXT_GAME_OVER[f],     TEXT_GAME_OVER[f + 1],
                    TEXT_GAME_OVER[f + 2], TEXT_GAME_OVER[f + 3]);
        if (switch_event(2)) { game_to_menu(); return; }   /* skip allowed */
        if (switch_event(0)) { sound_stop(); game_start(); return; }
        switch_event(1);
        return;
    }

    /* phase 2: blink the score; new record -> LEDs blink + fanfare */
    int on = (((t - scroll_end) / 400) % 2) == 0;
    if (on) display_number(score);
    else    display_set(SEG_BLANK, SEG_BLANK, SEG_BLANK, SEG_BLANK);

    if (new_record) {
        led_all(on);
        static uint32_t fanfare_for = 0xFFFFFFFF;
        if (fanfare_for != state_ms) {
            fanfare_for = state_ms;
            sound_play_duty(MEL_RECORD, LEN(MEL_RECORD), SOFT_DUTY);
        }
    }

    if (switch_event(2)) { game_to_menu(); return; }   /* S3 = menu      */
    if (switch_event(0)) { sound_stop(); game_start(); return; } /* S1 = again */
    switch_event(1);
}

/* ================================================================ main == */
int main(void)
{
    SystemCoreClockUpdate();

    led_init();
    display_init();
    buzzer_init();
    switch_init();
    adc_init();
    timer_init();                                /* starts the 1 ms tick */

    game_to_menu();                              /* menu + menu music */

    while (1) {
        switch (state) {
            case ST_MENU:  game_menu();  break;
            case ST_PLAY:  game_play();  break;
            case ST_PAUSE: game_pause(); break;
            case ST_HIT:   game_hit();   break;
            case ST_OVER:  game_over();  break;
        }
    }
}
