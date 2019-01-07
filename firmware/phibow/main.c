#include <stdlib.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/wdt.h>
#include <util/delay.h>

#include "usbdrv.h"
#include "description.h"
#include "touch.h"
#include "atmega-adc.h"

#define F_CPU 16000000UL
#define COUNTER_BITS 11
#define ASIZE (1<<COUNTER_BITS)
#define MASK (ASIZE-1)

static uchar sendEmptyFrame;
volatile uchar sendCC = 0; // will switch to 1 on timer
static uchar standalone;

unsigned int x[ASIZE];
unsigned int p;
volatile uchar currentNoteBuffer[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }; // 12 keys
volatile uchar lastNoteBuffer[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }; // 12 keys

/*
Guitar tuning
1 (E)	329.63 Hz	E4
2 (B)	246.94 Hz	B3
3 (G)	196.00 Hz	G3
4 (D)	146.83 Hz	D3
5 (A)	110.00 Hz	A2
6 (E)	82.41 Hz	E2

Phibow 432hz tuning (C3)
http://shakahara.com/pianopitch2.php
midi > freq > timer > string gauge > note

Actual tuning
64.2172 
68.0357 
72.0814 
76.3675 
80.9086 
85.7197 
90.8168 
96.2171 
101.938 
108.000 
114.422 
121.226 

Tested but too tight
48 == 128.434 == 87 - 5 - C >>>>> 4 - 0.71mm nw028
49 == 136.071 == 82 - 5 - C#  >>>>> 4 .
50 == 144.163 == 77 - 4 - D >>>>> 4
51 == 152.735 == 73 - 4 - D# >>>>> 3 - 0.56mm nw022
52 == 161.817 == 69 - 4 - E  >>>>> 3 .
53 == 171.439 == 65 - 4 - F >>>>>  3
54 == 181.634 == 62 - 3 - F# >>>>>  3 
56 == 192.434 == 58 - 3 - G >>>>> 3 --broke switch to 2
55 == 203.877 == 55 - 3 - G# >>>>> 2 - 0.36mm pl014
57 == 216.000 == 52 - 2 - A >>>>>  2 .
58 == 228.844 == 49 - 2 - A# >>>>> 2
59 == 242.452 == 46 - 2 - B >>>>> 2
*/

//121.226hz to 64.2172hz
unsigned int f[12] = { 91, 97, 103, 110, 115, 122, 129, 137, 145, 153, 163, 172 }; //(12 high - 1 low)


////////////////////////////////////////////////////////////////////////
// IO
////////////////////////////////////////////////////////////////////////
/*
-------- PWM ---------
1 - PB0
2 - PB1
3 - PB2
4 - PB3
5 - PB4
6 - PB5
7 - PB6
8 - PB7
9 - PE0
10 - PE1
11 - PE2
12 - PE3

--------LEDS---------
1 - PJ7
2 - PJ6
3 - PJ5
4 - PJ4
5 - PJ3
6 - PJ2
7 - PJ1
8 - PJ0
9 - PC7
10 - PC6
11 - PC5
12 - PC4

--------MOSFET---------
1 - PH0
2 - PH1
3 - PH2
4 - PH3
5 - PH4
6 - PH5
7 - PH6
8 - PH7
9 - PE4
10 - PE5
11 - PE6
12 - PE7

--------PIANO (SCREW) ADC / TOUCH---------
1 - PF0
2 - PF1
3 - PF2
4 - PF3
5 - PF4
6 - PF5
7 - PF6
8 - PF7
9 - PK0
10 - PK1
11 - PK2
12 - PK3

------------------------SLIDER VHS-------------------------
PK4 - VHS

---------------------------SWITCH--------------------------
PL0 - standalone
*/

////////////////////////////////////////////////////////////////////////
// INIT
////////////////////////////////////////////////////////////////////////
static void USBConf(void)
{
	uchar i, j;

	/* activate pull-ups except on USB lines */
	USB_CFG_IOPORT =
	    (uchar) ~ ((1 << USB_CFG_DMINUS_BIT) |
		       (1 << USB_CFG_DPLUS_BIT));
	/* all pins input except USB (-> USB reset) */
	#ifdef USB_CFG_PULLUP_IOPORT	/* use usbDeviceConnect()/usbDeviceDisconnect() if available */
		USBDDR = 0;		/* we do RESET by deactivating pullup */
		usbDeviceDisconnect();
	#else
		USBDDR = (1 << USB_CFG_DMINUS_BIT) | (1 << USB_CFG_DPLUS_BIT);
	#endif

		j = 0;
		while (--j) {		/* USB Reset by device only required on Watchdog Reset */
			i = 0;
			while (--i);	/* delay >10ms for USB reset */
		}
	#ifdef USB_CFG_PULLUP_IOPORT
		usbDeviceConnect();
	#else
		USBDDR = 0;		/*  remove USB reset condition */
	#endif

	DDRD = 0b11010000; // set debug led
}


////////////////////////////////////////////////////////////////////////
// IO CONFIGURATION
////////////////////////////////////////////////////////////////////////
void IOConf()
{
	// PWM / MOSFET - output
	DDRB = 0xFF;
	DDRH = 0xFF;
	DDRE = 0xFF;
	
	// LED - output
	DDRJ = 0xFF;
	DDRC = 0xFF;
	
	// SWITCH - input
	DDRL = 0x00;
	PORTL = 0xFF; // pull-up enable
	
	// ANALOG - input
	DDRF = 0x00;
	DDRK = 0x00;
}



////////////////////////////////////////////////////////////////////////
// VHS - CC throttle
////////////////////////////////////////////////////////////////////////
void timerCC_init() {
	TCCR0A |= (1<<WGM01);
	TCCR0B |= (1<<CS02) | (1<<CS00);
	OCR0A = 255;
	TIMSK0 |= (1 << OCIE0A); // enable compare interrupt
}

ISR (TIMER0_COMPA_vect) {
	sendCC = 1;
}



////////////////////////////////////////////////////////////////////////
// PWM SOFTWARE
// see this thread: http://www.electro-tech-online.com/threads/12-simultaneous-unique-frequencies-pwm.140532
////////////////////////////////////////////////////////////////////////
void timerPWM_init() {
	TCCR2A |= (1<<WGM21);
	TCCR2B |= (1<<CS21);
	OCR2A = 180; // 11098hz (cannot run lower because of V-USB)
	TIMSK2 |= (1 << OCIE2A); // enable compare interrupt
}

void pwm_init() {
  unsigned int i;
  unsigned int mask;
  for (i = 0; i < ASIZE; i++) {
    x[i] = 0;
  }
  mask = 1;
  for (i = 0; i < 12; i++) {
    x[f[i]] |= mask;
    mask <<= 1;
  }
  p = 0;
}

ISR (TIMER2_COMPA_vect) {
  unsigned int i;
  unsigned int z;
  unsigned int mask;
 
  p++;
  z = x[p&MASK];
 
  if (z) {
    x[p&MASK] = 0;
    mask = 1;
    
    // this is slower
    for (i = 0; i <12; i++) {
      if (z&mask) {
        if(i > 7) {
		PORTE ^= (1 << (i - 8));
		} else {
			PORTB ^= (1 << i);
		}
        x[(p+f[i])&MASK] |= mask;
      }
      mask <<= 1;
    }
    /*
    // this should be quicker
    PORTE ^= z >> 8;
	PORTB ^= z; // assumes 8-bit CPU
	for (i = 0; i <12; i++) {
	   if (z&mask) {
	     x[(p+f[i])&MASK] |= mask;
	   }
	   mask <<= 1;
	}
	*/

  }
}


/////////////////////////////////////////////////////////////////////////////////////////////////
// CAPACITIVE TOUCH on ADC
// this is causing noise in the pick-up / ground even if the string are isolated
/////////////////////////////////////////////////////////////////////////////////////////////////
// min 780 or higher if unstable
// play with touch.c _delay_us(100)
// play with threshold in main()
static touch_channel_t btn11 = { .mux = 11, .port = &PORTK, .portmask = (1<<PK3), .min = 780, .max = 1024 };
static touch_channel_t btn10 = { .mux = 10, .port = &PORTK, .portmask = (1<<PK2), .min = 780, .max = 1024 };
static touch_channel_t btn9 = { .mux = 9, .port = &PORTK, .portmask = (1<<PK1), .min = 780, .max = 1024 };
static touch_channel_t btn8 = { .mux = 8, .port = &PORTK, .portmask = (1<<PK0), .min = 780, .max = 1024 };
static touch_channel_t btn7 = { .mux = 7, .port = &PORTF, .portmask = (1<<PF7), .min = 780, .max = 1024 };
static touch_channel_t btn6 = { .mux = 6, .port = &PORTF, .portmask = (1<<PF6), .min = 780, .max = 1024 };
static touch_channel_t btn5 = { .mux = 5, .port = &PORTF, .portmask = (1<<PF5), .min = 780, .max = 1024 };
static touch_channel_t btn4 = { .mux = 4, .port = &PORTF, .portmask = (1<<PF4), .min = 780, .max = 1024 };
static touch_channel_t btn3 = { .mux = 3, .port = &PORTF, .portmask = (1<<PF3), .min = 780, .max = 1024 };
static touch_channel_t btn2 = { .mux = 2, .port = &PORTF, .portmask = (1<<PF2), .min = 780, .max = 1024 };
static touch_channel_t btn1 = { .mux = 1, .port = &PORTF, .portmask = (1<<PF1), .min = 780, .max = 1024 }; 
static touch_channel_t btn0 = { .mux = 0, .port = &PORTF, .portmask = (1<<PF0), .min = 780, .max = 1024 };



/////////////////////////////////////////////////////////////////////////////////////////////////
// PARSE INCOMING MIDI
/////////////////////////////////////////////////////////////////////////////////////////////////
void parseMidiMessage(uchar *data, uchar len) {
	uchar cin = (*data) & 0x0f;
	uchar Rch = (*(data+1)) & 0x0f;
	uchar note = *(data+2);
	// velocity *(data + 3) // not use
	// PBBuf = (*(data + 3)) & 0x7f; /* use only MSB(7bit) */
	if (Rch != 0)
		return;
	switch(cin) {
		case 8:	/* NOTE OFF */
				if(note == 48) {
					PORTC &= ~(1 << PINC4);
					PORTE &= ~(1 << PINE7);
					
				} else if(note == 49) {
					PORTC &= ~(1 << PINC5);
					PORTE &= ~(1 << PINE6);
					
				} else if(note == 50) {
					PORTC &= ~(1 << PINC6);
					PORTE &= ~(1 << PINE5);
					
				} else if(note == 51) {
					PORTC &= ~(1 << PINC7);
					PORTE &= ~(1 << PINE4);
					
				} else if(note == 52) {
					PORTJ &= ~(1 << PINJ0);
					PORTH &= ~(1 << PINH7);
					
				} else if(note == 53) {
					PORTJ &= ~(1 << PINJ1);
					PORTH &= ~(1 << PINH6);
					
				} else if(note == 54) {
					PORTJ &= ~(1 << PINJ2);
					PORTH &= ~(1 << PINH5);
					
				} else if(note == 55) {
					PORTJ &= ~(1 << PINJ3);
					PORTH &= ~(1 << PINH4);
					
				} else if(note == 56) {
					PORTJ &= ~(1 << PINJ4);
					PORTH &= ~(1 << PINH3);
					
				} else if(note == 57) {
					PORTJ &= ~(1 << PINJ5);
					PORTH &= ~(1 << PINH2);
					
				} else if(note == 58) {
					PORTJ &= ~(1 << PINJ6);
					PORTH &= ~(1 << PINH1);
					
				} else if(note == 59) {
					PORTJ &= ~(1 << PINJ7);
					PORTH &= ~(1 << PINH0);
					
				}
		break;
		case 9:	/* NOTE ON */
			if( *(data + 3) == 0){ // hum should not happen?
				if(note == 48) {
					PORTC &= ~(1 << PINC4);
					PORTE &= ~(1 << PINE7);
					
				} else if(note == 49) {
					PORTC &= ~(1 << PINC5);
					PORTE &= ~(1 << PINE6);
					
				} else if(note == 50) {
					PORTC &= ~(1 << PINC6);
					PORTE &= ~(1 << PINE5);
					
				} else if(note == 51) {
					PORTC &= ~(1 << PINC7);
					PORTE &= ~(1 << PINE4);
					
				} else if(note == 52) {
					PORTJ &= ~(1 << PINJ0);
					PORTH &= ~(1 << PINH7);
					
				} else if(note == 53) {
					PORTJ &= ~(1 << PINJ1);
					PORTH &= ~(1 << PINH6);
					
				} else if(note == 54) {
					PORTJ &= ~(1 << PINJ2);
					PORTH &= ~(1 << PINH5);
					
				} else if(note == 55) {
					PORTJ &= ~(1 << PINJ3);
					PORTH &= ~(1 << PINH4);
					
				} else if(note == 56) {
					PORTJ &= ~(1 << PINJ4);
					PORTH &= ~(1 << PINH3);
					
				} else if(note == 57) {
					PORTJ &= ~(1 << PINJ5);
					PORTH &= ~(1 << PINH2);
					
				} else if(note == 58) {
					PORTJ &= ~(1 << PINJ6);
					PORTH &= ~(1 << PINH1);
					
				} else if(note == 59) {
					PORTJ &= ~(1 << PINJ7);
					PORTH &= ~(1 << PINH0);
					
				}
			} else {
				if(note == 48) {
					PORTC |= (1 << PINC4);
					PORTE |= (1 << PINE7);
					
				} else if(note == 49) {
					PORTC |= (1 << PINC5);
					PORTE |= (1 << PINE6);
					
				} else if(note == 50) {
					PORTC |= (1 << PINC6);
					PORTE |= (1 << PINE5);
					
				} else if(note == 51) {
					PORTC |= (1 << PINC7);
					PORTE |= (1 << PINE4);
					
				} else if(note == 52) {
					PORTJ |= (1 << PINJ0);
					PORTH |= (1 << PINH7);
					
				} else if(note == 53) {
					PORTJ |= (1 << PINJ1);
					PORTH |= (1 << PINH6);
					
				} else if(note == 54) {
					PORTJ |= (1 << PINJ2);
					PORTH |= (1 << PINH5);
					
				} else if(note == 55) {
					PORTJ |= (1 << PINJ3);
					PORTH |= (1 << PINH4);
					
				} else if(note == 56) {
					PORTJ |= (1 << PINJ4);
					PORTH |= (1 << PINH3);
					
				} else if(note == 57) {
					PORTJ |= (1 << PINJ5);
					PORTH |= (1 << PINH2);
					
				} else if(note == 58) {
					PORTJ |= (1 << PINJ6);
					PORTH |= (1 << PINH1);
					
				} else if(note == 59) {
					PORTJ |= (1 << PINJ7);
					PORTH |= (1 << PINH0);
					
				}
			}
		break;
	}
	if (len > 4) {
		parseMidiMessage(data+4, len-4);
	}
}


/////////////////////////////////////////////////////////////////////////////////////////////////
// USB<->MIDI
/////////////////////////////////////////////////////////////////////////////////////////////////
uchar usbFunctionDescriptor(usbRequest_t * rq)
{
	if (rq->wValue.bytes[1] == USBDESCR_DEVICE) {
		usbMsgPtr = (uchar *) deviceDescrMIDI;
		return sizeof(deviceDescrMIDI);
	} else {
		usbMsgPtr = (uchar *) configDescrMIDI;
		return sizeof(configDescrMIDI);
	}
}

uchar usbFunctionSetup(uchar data[8])
{
	usbRequest_t *rq = (void *) data;
	if ((rq->bmRequestType & USBRQ_TYPE_MASK) == USBRQ_TYPE_CLASS) {
		if ((rq->bmRequestType & USBRQ_DIR_MASK) ==
		    USBRQ_DIR_HOST_TO_DEVICE)
			sendEmptyFrame = 1;
	}
	return 0xff;
}

void usbFunctionWriteOut(uchar * data, uchar len) // from computer
{
	parseMidiMessage(data, len);
}

uchar usbFunctionWrite(uchar * data, uchar len)
{
	return 1;
}

uchar usbFunctionRead(uchar * data, uchar len)
{
	return 7;
}


////////////////////////////////////////////////////////////////////////
// BOOTLOADER
////////////////////////////////////////////////////////////////////////
// ------------------------------------------------------------------------------
// - Write to EEPROM
// ------------------------------------------------------------------------------
void eepromWrite(unsigned int uiAddress, unsigned char ucData) {
  while(EECR & (1<<EEPE));
  EEAR = uiAddress;
  EEDR = ucData;
  cli();
  EECR |= (1<<EEMPE);
  EECR |= (1<<EEPE);
  sei();
}

/* ------------------------------------------------------------------------- */
/* ------------------------------- BOOTLOADER ------------------------------ */
/* ------------------------------------------------------------------------- */
void (*jump_to_bootloader)(void) = 0x7000; __attribute__ ((unused))

void startBootloader(void) {
    eepromWrite(0 , 1);        // stay in bootloader
    TCCR2A = 0;
    TCCR2B = 0;
    TCCR0A = 0;
    TCCR0B = 0;
	TIMSK0 &= ~(1<<TOIE0);            // disable timer overflow
	TIMSK1 &= ~(1<<TOIE1);            // disable timer overflow
	TIMSK2 &= ~(1<<TOIE2);            // disable timer overflow
	TIMSK3 &= ~(1<<TOIE3);            // disable timer overflow
	TIMSK4 &= ~(1<<TOIE4);            // disable timer overflow
	TIMSK5 &= ~(1<<TOIE5);            // disable timer overflow
    cli();              // turn off interrupts
    ADCSRA &= ~(1<<ADIE);  // disable ADC interrupts
    ADCSRA &= ~(1<<ADEN);  // disable ADC (turn off ADC power)       
	PORTJ = 0;						// pull all pins low
	PORTB = 0;
	PORTC = 0;	
	PORTD = 0;	
	PORTE = 0;	
	PORTF = 0;	
	PORTG = 0;	
	PORTH = 0;	
	PORTL = 0;         
    wdt_disable();          // disable watchdog timer
    usbDeviceDisconnect();       // disconnect from USB bus
    jump_to_bootloader();
}

////////////////////////////////////////////////////////////////////////
// UTILS
////////////////////////////////////////////////////////////////////////
void AllOff() {
	PORTC &= ~(1 << 7);
	PORTC &= ~(1 << 6);
	PORTC &= ~(1 << 5);
	PORTC &= ~(1 << 4);
	PORTE &= ~(1 << 7);
	PORTE &= ~(1 << 6);
	PORTE &= ~(1 << 5);
	PORTE &= ~(1 << 4);
	PORTJ = 0x00;
	PORTH = 0x00;
}
		
////////////////////////////////////////////////////////////////////////
// MAIN
////////////////////////////////////////////////////////////////////////
int main(void)
{
	IOConf();
	USBConf();
	usbInit();
	timerCC_init();
	timerPWM_init();
	pwm_init();
	touch_init();
	sei();
	
	
	
	uchar midiMsg[4]; // 8bit midi
    uint16_t i = 0; // check capacitive touch every i
    uchar j = 0; // 12 strings
    uint16_t sample[12]; // touch adc
	
	for (;;) {

		i++; // capacitive delay counter

		if(!standalone) {
			usbPoll(); // every 50ms min
		}
		
		// go to BOOTLOADER
        if(!(PIND & (1<<PIND5))) {
            startBootloader();
        }

		// standalone SWITCH
        standalone = !(PINL & (1<<PINL0)) ? 1 : 0;
		
		// capacitive TOUCH ADC READ
		if(i == 100) {
			if(touch_measure(&btn0) > 340) {
				currentNoteBuffer[0] = 1;
			} else {
				currentNoteBuffer[0] = 0;
			}
		} else if(i == 200) {
			if(touch_measure(&btn1) > 340) {
				currentNoteBuffer[1] = 1;
			} else {
				currentNoteBuffer[1] = 0;
			}
		} else if(i == 300) {
			if(touch_measure(&btn2) > 340) {
				currentNoteBuffer[2] = 1;
			} else {
				currentNoteBuffer[2] = 0;
			}
		} else if(i == 400) {
			if(touch_measure(&btn3) > 340) {
				currentNoteBuffer[3] = 1;
			} else {
				currentNoteBuffer[3] = 0;
			}
		} else if(i == 500) {
			if(touch_measure(&btn4) > 340) {
				currentNoteBuffer[4] = 1;
			} else {
				currentNoteBuffer[4] = 0;
			}
		} else if(i == 600) {
			if(touch_measure(&btn5) > 340) {
				currentNoteBuffer[5] = 1;
			} else {
				currentNoteBuffer[5] = 0;
			}
		} else if(i == 700) {
			if(touch_measure(&btn6) > 340) {
				currentNoteBuffer[6] = 1;
			} else {
				currentNoteBuffer[6] = 0;
			}
		} else if(i == 800) {
			if(touch_measure(&btn7) > 340) {
				currentNoteBuffer[7] = 1;
			} else {
				currentNoteBuffer[7] = 0;
			}
		} else if(i == 900) {
			if(touch_measure(&btn8) > 340) {
				currentNoteBuffer[8] = 1;
			} else {
				currentNoteBuffer[8] = 0;
			}
		} else if(i == 1000) {
			if(touch_measure(&btn9) > 340) {
				currentNoteBuffer[9] = 1;
			} else {
				currentNoteBuffer[9] = 0;
			}
		} else if(i == 1200) {
			if(touch_measure(&btn10) > 340) {
				currentNoteBuffer[10] = 1;
			} else {
				currentNoteBuffer[10] = 0;
			}
		} else if(i == 1300) {
			if(touch_measure(&btn11) > 340) {
				currentNoteBuffer[11] = 1;
			} else {
				currentNoteBuffer[11] = 0;
			}
		} else if(i > 1400) {
			PORTD ^= (1 << PIND6);
			i = 0;
		}

		
		// CAPACITIVE TOUCH -> PC via MIDI NOTE
		for(j = 0; j < 12; j++) {
			
			// blue led monitor bandwidth
			PORTD ^= (1 << PIND7);
			
			if(currentNoteBuffer[j] != lastNoteBuffer[j]) {
				lastNoteBuffer[j] = currentNoteBuffer[j];

				if(currentNoteBuffer[j] == 1) { // NOTE ON
					if(j < 8) {
						PORTJ |= (1 << j + 7 - ((j * 2))); // led
						PORTH |= (1 << j); // mosfet
					} else {
						if(j == 8) {
							PORTC |= (1 << 7);
						} else if(j == 9) {
							PORTC |= (1 << 6);
						} else if(j == 10) {
							PORTC |= (1 << 5);
						} else if(j == 11) {
							PORTC |= (1 << 4);
						}
						PORTE |= (1 << (j - 4));
					}
					midiMsg[0] = 0x09;
					midiMsg[1] = 0x90;
					midiMsg[3] = 0x7f;
					
				} else { // NOTE OFF
					if(j < 8) {
						PORTJ &= ~(1 << j + 7 - ((j * 2))); // led
						PORTH &= ~(1 << j); // mosfet
					} else {
						if(j == 8) {
							PORTC &= ~(1 << 7);
						} else if(j == 9) {
							PORTC &= ~(1 << 6);
						} else if(j == 10) {
							PORTC &= ~(1 << 5);
						} else if(j == 11) {
							PORTC &= ~(1 << 4);
						}
						PORTE &= ~(1 << (j - 4));
					}
					midiMsg[0] = 0x08; 
					midiMsg[1] = 0x80;
					midiMsg[3] = 0x00;
				}
				midiMsg[2] = j + 59 - (j * 2);

				sendEmptyFrame = 0;
				if(!standalone) {
					while (!usbInterruptIsReady()) {
						usbPoll();
					}
					usbSetInterrupt(midiMsg, 4);
				}
			}
		}
		
		// VHS -> PC via MIDI CC
		if(sendCC) {
			sendCC = 0;
			midiMsg[0] = 0x0b;
			midiMsg[1] = 0xb0;
			midiMsg[2] = 70;
			midiMsg[3] = adc_read(ADC_PRESCALER_128, ADC_VREF_AVCC, 12) >> 3; // vhs slider
			
			sendEmptyFrame = 0;
			if(!standalone) {
				while (!usbInterruptIsReady()) {
					usbPoll();
				}
				usbSetInterrupt(midiMsg, 4);
			}
			

			if(midiMsg[3] > 0 && midiMsg[3] < 10) {
				AllOff();
				PORTC |= (1 << PINC4);
				PORTE |= (1 << PINE7);
				PORTC |= (1 << PINC6);
				PORTE |= (1 << PINE5);
				PORTJ |= (1 << PINJ0);
				PORTH |= (1 << PINH7);
				
			} else if(midiMsg[3] > 10 && midiMsg[3] < 20) {
				AllOff();
				PORTC |= (1 << PINC5);
				PORTE |= (1 << PINE6);
				PORTC |= (1 << PINC7);
				PORTE |= (1 << PINE4);
				
			} else if(midiMsg[3] > 20 && midiMsg[3] < 30) {
				AllOff();
				PORTJ |= (1 << PINJ0);
				PORTH |= (1 << PINH7);
				
			} else if(midiMsg[3] > 30 && midiMsg[3] < 40) {
				AllOff();
				PORTJ |= (1 << PINJ1);
				PORTH |= (1 << PINH6);
				
			} else if(midiMsg[3] > 40 && midiMsg[3] < 50) {
				AllOff();
				PORTJ |= (1 << PINJ2);
				PORTH |= (1 << PINH5);
				
			} else if(midiMsg[3] > 50 && midiMsg[3] < 60) {
				AllOff();
				PORTJ |= (1 << PINJ3);
				PORTH |= (1 << PINH4);
				
			} else if(midiMsg[3] > 60 && midiMsg[3] < 70) {
				AllOff();
				PORTJ |= (1 << PINJ4);
				PORTH |= (1 << PINH3);
				
			} else if(midiMsg[3] > 70 && midiMsg[3] < 80) {
				AllOff();
				PORTJ |= (1 << PINJ5);
				PORTH |= (1 << PINH2);
				
			} else if(midiMsg[3] > 80 && midiMsg[3] < 90) {
				AllOff();
				PORTJ |= (1 << PINJ6);
				PORTH |= (1 << PINH1);
				
			} else if(midiMsg[3] > 90 && midiMsg[3] < 100) {
				AllOff();
				PORTJ |= (1 << PINJ7);
				PORTH |= (1 << PINH0);
				
			} else if(midiMsg[3] > 100 && midiMsg[3] < 116) {
				AllOff();
			}
			// above 118 == not touching the vsh
		}
		
	}

	return 0;
}
