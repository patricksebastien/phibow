/*
 * ebarn.c
 *
 * Created: 2/27/2014 9:37:44 PM
 *  Author: psc
 */ 

#define F_CPU 16000000UL
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/wdt.h>

#include <util/delay.h>
#include <string.h>

#include "description.h"
#include "usbdrv.h"

static uchar sendEmptyFrame;

/////////////////////////////////////////////////////////////////////////////////////////////////
// INIT
/////////////////////////////////////////////////////////////////////////////////////////////////
static void hardwareInit(void)
{
	uchar i, j;
	USB_CFG_IOPORT =
	    (uchar) ~ ((1 << USB_CFG_DMINUS_BIT) |
		       (1 << USB_CFG_DPLUS_BIT));
#ifdef USB_CFG_PULLUP_IOPORT
	USBDDR = 0;
	usbDeviceDisconnect();
#else
	USBDDR = (1 << USB_CFG_DMINUS_BIT) | (1 << USB_CFG_DPLUS_BIT);
#endif
	j = 0;
	while (--j) {
		i = 0;
		while (--i);
	}
#ifdef USB_CFG_PULLUP_IOPORT
	usbDeviceConnect();
#else
	USBDDR = 0;
#endif
}

void InitIO()
{
	// PWM - output
	DDRB = 0xFF;
	DDRG = 0xFF;
	DDRH = 0xFF;
	DDRE = 0xFF;
	DDRL = 0xFF;
	
	// Push-button - input
	DDRJ = 0x00;
	PORTJ = 0xFF; // pull-up enable
	
	// LED - output
	DDRA = 0xFF;
	
	// Push-button - input (4-7)
	// LED - output (0-3)	
	DDRC = 0x0F;
	PORTC = 0xF0; // pull-up enable
}

void InitPWM()
{
	//Timer 0 | 8bit
	TCCR0A |= (1<<WGM01) | (1<<WGM00) | (1<<COM0A1) | (1<<COM0B1);
	TCCR0B |= (1<<CS02) | (1<<CS00);
	OCR0A = 66; // a# 233.082
	OCR0B = 70; // a 220
	
	//Timer 1 | 16bit
	TCCR1A |= (1<<WGM11) | (1<<COM1A1) | (1<<COM1B1);
	TCCR1B |= (1<<WGM13) | (1<<WGM12)  | (1<<CS11);
	ICR1 = 65535;
	OCR1A = 15288; // c - 130.813
	OCR1B = 13620; //d - 146.832
	
	//Timer 2 | 8bit
	TCCR2A |= (1<<WGM21) | (1<<WGM20) | (1<<COM2A1) | (1<<COM2B1);
	TCCR2B |= (1<<CS22) | (1<<CS21) | (1<<CS20);
	OCR2A = 94; // e - 164.814
	OCR2B = 112; //c# -138.591
	
	//Timer 3 | 16bit
	TCCR3A |= (1<<WGM31) | (1<<COM3A1) | (1<<COM3B1);
	TCCR3B |= (1<<WGM33) | (1<<WGM32)  | (1<<CS31);
	ICR3 = 65535;
	OCR3A = 12856; // d# - 155.563
	OCR3B = 11453; //f - 174.614

	//Timer 4 | 16bit
	TCCR4A |= (1<<WGM41) | (1<<COM4A1) | (1<<COM4B1);
	TCCR4B |= (1<<WGM43) | (1<<WGM42)  | (1<<CS41);
	ICR4 = 65535;
	OCR4A = 10810; // f# - 184.997
	OCR4B = 10203; //g - 195.998

	//Timer 5 | 16bit
	TCCR5A |= (1<<WGM51) | (1<<COM5A1) | (1<<COM5B1);
	TCCR5B |= (1<<WGM53) | (1<<WGM52)  | (1<<CS51);
	ICR5 = 65535;
	OCR5A = 9630; // g# - 207.652
	OCR5B = 8098; //b - 246.942	
}



/////////////////////////////////////////////////////////////////////////////////////////////////
// UTIL
/////////////////////////////////////////////////////////////////////////////////////////////////
void parseMidiMessage(uchar *data, uchar len) {
	uchar cin = (*data) & 0x0f;
	uchar Rch = (*(data+1)) & 0x0f;
	uchar note = *(data+2);

	if (Rch != 0)
		return;

	switch(cin) {
		case 8:	/* NOTE OFF */
				PORTC |= (1 << PINC1);
		break;
		case 9:	/* NOTE ON */
			if( *(data + 3) == 0){
				PORTC |= (1 << PINC1);
			} else {
				if(note == 60) {
					PORTC &= ~(1 << PINC1);
					//PORTC ^= 0x02;
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

/////////////////////////////////////////////////////////////////////////////////////////////////
// MAIN
/////////////////////////////////////////////////////////////////////////////////////////////////
int main(void)
{
	InitIO();
	InitPWM();
	hardwareInit();
	sei();
	
	sendEmptyFrame = 0;
	uchar midiMsg[8];
	
	for (;;) {
		//wdt_reset();
		usbPoll();
		
		// MIDI -> COMPUTER
		if (usbInterruptIsReady()) {
			if(!(PINJ & (1<<PINJ0))) {
				midiMsg[iii++] = 0x08;
				midiMsg[iii++] = 0x80;
				midiMsg[iii++] = 55;
				midiMsg[iii++] = 0x00;
				midiMsg[iii++] = 0x09;
				midiMsg[iii++] = 0x90;
				midiMsg[iii++] = 77;
				midiMsg[iii++] = 0x7f;
				if (8 == iii)
					sendEmptyFrame = 1;
				else
					sendEmptyFrame = 0;
				usbSetInterrupt(midiMsg, iii);
			}
		}
		
		if(!(PINJ & (1<<PINJ0))) {
			PORTC ^= (1 << PINC0);
		} else if(!(PINC & (1<<PINC4))) {
			PORTC ^= (1 << PINC3);
		}
		
		PORTA ^= (1 << PINA0);
		PORTA ^= (1 << PINA7);
		_delay_ms(500);
	}
}
