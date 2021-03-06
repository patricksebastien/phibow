/*
 * ebarn.c
 *
 * Created: 2/27/2014 9:37:44 PM
 *  Author: psc
 */ 

#define F_CPU 16000000UL
#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>

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
	DDRA = 0x00;
	
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

void timer1_init()
{
	TCCR0A |= (1<<WGM01) | (1<<COM0A0 ) | (1<<COM0B0 );
	TCCR0B |= (1<<CS02) | (1<<CS00);
	//TCCR0B |= (1<<CS00); //fullspeed
	// initialize counter
	//TCNT0 = 0;
	
	// initialize compare value
	OCR0A = 20;
	OCR0B = 10;
	// enable compare interrupt
	TIMSK0 |= (1 << OCIE0A) | (1 << OCIE0B);
}

volatile unsigned long test = 0;
volatile unsigned long testb = 0;
ISR (TIMER0_COMPA_vect)
{
	test++;
	// toggle led here
	PORTA ^= (1 << 0);
	//_delay_ms(500);
}

ISR (TIMER0_COMPB_vect)
{
	testb++;
	// toggle led here
	PORTA ^= (1 << 0);
	//_delay_ms(500);
}

int main(void)
{
	InitIO();
	//InitPWM();
	timer1_init();
	sei();
	
	while(1) {

		/*
		if(TCNT0 >= 227) {
			test++;
		}
		*/
		if(!(PINJ & (1<<PINJ0))) {
			PORTC ^= (1 << PINC0);
		} else if(!(PINC & (1<<PINC4))) {
			PORTC ^= (1 << PINC3);
		}
		PORTA ^= (1 << PINA0);
		PORTA ^= (1 << PINA7);
		//_delay_ms(500);
	}
}


