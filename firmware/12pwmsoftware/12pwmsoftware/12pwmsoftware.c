/*
 * _12pwmsoftware.c
 *
 * Created: 3/21/2014 9:22:29 PM
 *  Author: psc
 */ 

#define F_CPU 20000000UL
#include <avr/io.h>
#include <avr/interrupt.h>

volatile unsigned long test = 0;

ISR (TIMER0_COMPA_vect) {
	test++;
}

int main(void)
{
	TCCR0A |= (1 << COM1A1);
	TCCR0B |= (1 << CS10) | (1 << WGM12) | (1 << WGM13);
	TIMSK0 |= (1 << OCIE0A);
	sei();
	
    while(1)
    {
		if(test > 100000) {
			test = 0;
		}
    }
}