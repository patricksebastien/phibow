/* Name: main.c
 * Project: SX-150 AVR-MIDI interface
 * Author: Yoshitaka Kuwata 
 * based on  Martin Hom
 * License: GPL v2 or later.
 */

#include <string.h>
#include <avr/io.h>
#include <avr/eeprom.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/wdt.h>
#include <util/delay.h>

#include "usbdrv.h"
#include "oddebug.h"


#if DEBUG_LEVEL > 0
#	warning "Never compile production devices with debugging enabled"
#endif

extern void parseMidiMessage(uchar *, uchar );

//-----------------------------------------------
// define

#define TRIG_COUNT      100	/* トリガを確実にかけるためメインループを回す回数 */

/* UNUSED */
#define GATE1		PB4	
#define GATE1_ON	(PORTB &= ~(1 << GATE1))
#define GATE1_OFF	(PORTB |= (1 << GATE1))
/* UNUSED */

#define	TRUE		1
#define	FALSE		0
#define ON		1
#define OFF		0

#define MAX_NOTE_CNT	8

typedef	int		int16;
typedef	unsigned int	uint16;
typedef	long		int32;
typedef	unsigned long	uint32;
typedef	unsigned char	BOOL;

//-----------------------------------------------
// グローバル変数

uchar   noteSave;		/* 演奏中のNoteを保持 */
uchar	gate;
uchar	trig;

uchar	PBBuf;
uchar	NoteCnt;
uchar	NoteBuf[MAX_NOTE_CNT];
uint16  CVduty;

// This deviceDescrMIDI[] is based on 
// http://www.usb.org/developers/devclass_docs/midi10.pdf
// Appendix B. Example: Simple MIDI Adapter (Informative)

// B.1 Device Descriptor
static PROGMEM char deviceDescrMIDI[] = {	/* USB device descriptor */
  18,			/* sizeof(usbDescriptorDevice): length of descriptor in bytes */
  USBDESCR_DEVICE,	/* descriptor type */
  0x10, 0x01,		/* USB version supported */
  0,			/* device class: defined at interface level */
  0,			/* subclass */
  0,			/* protocol */
  8,			/* max packet size */
  USB_CFG_VENDOR_ID,	/* 2 bytes */
  USB_CFG_DEVICE_ID,	/* 2 bytes */
  USB_CFG_DEVICE_VERSION,	/* 2 bytes */
  1,			/* manufacturer string index */
  2,			/* product string index */
  0,			/* serial number string index */
  1,			/* number of configurations */
};

// B.2 Configuration Descriptor
static PROGMEM char configDescrMIDI[] = {	/* USB configuration descriptor */
  9,			/* sizeof(usbDescrConfig): length of descriptor in bytes */
  USBDESCR_CONFIG,	/* descriptor type */
  101, 0,		/* total length of data returned (including inlined descriptors) */
  2,			/* number of interfaces in this configuration */
  1,			/* index of this configuration */
  0,			/* configuration name string index */
#if USB_CFG_IS_SELF_POWERED
  USBATTR_SELFPOWER,	/* attributes */
#else
  USBATTR_BUSPOWER,	/* attributes */
#endif
  USB_CFG_MAX_BUS_POWER / 2,	/* max USB current in 2mA units */

// B.3 AudioControl Interface Descriptors
// The AudioControl interface describes the device structure (audio function topology) 
// and is used to manipulate the Audio Controls. This device has no audio function 
// incorporated. However, the AudioControl interface is mandatory and therefore both 
// the standard AC interface descriptor and the classspecific AC interface descriptor 
// must be present. The class-specific AC interface descriptor only contains the header 
// descriptor.

// B.3.1 Standard AC Interface Descriptor
// The AudioControl interface has no dedicated endpoints associated with it. It uses the 
// default pipe (endpoint 0) for all communication purposes. Class-specific AudioControl 
// Requests are sent using the default pipe. There is no Status Interrupt endpoint provided.
  /* descriptor follows inline: */
  9,			/* sizeof(usbDescrInterface): length of descriptor in bytes */
  USBDESCR_INTERFACE,	/* descriptor type */
  0,			/* index of this interface */
  0,			/* alternate setting for this interface */
  0,			/* endpoints excl 0: number of endpoint descriptors to follow */
  1,			/* */
  1,			/* */
  0,			/* */
  0,			/* string index for interface */

// B.3.2 Class-specific AC Interface Descriptor
// The Class-specific AC interface descriptor is always headed by a Header descriptor 
// that contains general information about the AudioControl interface. It contains all 
// the pointers needed to describe the Audio Interface Collection, associated with the 
// described audio function. Only the Header descriptor is present in this device 
// because it does not contain any audio functionality as such.
  /* descriptor follows inline: */
  9,			/* sizeof(usbDescrCDC_HeaderFn): length of descriptor in bytes */
  36,			/* descriptor type */
  1,			/* header functional descriptor */
  0x0, 0x01,		/* bcdADC */
  9, 0,			/* wTotalLength */
  1,			/* */
  1,			/* */

// B.4 MIDIStreaming Interface Descriptors

// B.4.1 Standard MS Interface Descriptor
  /* descriptor follows inline: */
  9,			/* length of descriptor in bytes */
  USBDESCR_INTERFACE,	/* descriptor type */
  1,			/* index of this interface */
  0,			/* alternate setting for this interface */
  2,			/* endpoints excl 0: number of endpoint descriptors to follow */
  1,			/* AUDIO */
  3,			/* MS */
  0,			/* unused */
  0,			/* string index for interface */

// B.4.2 Class-specific MS Interface Descriptor
  /* descriptor follows inline: */
  7,			/* length of descriptor in bytes */
  36,			/* descriptor type */
  1,			/* header functional descriptor */
  0x0, 0x01,		/* bcdADC */
  65, 0,		/* wTotalLength */

// B.4.3 MIDI IN Jack Descriptor
  /* descriptor follows inline: */
  6,			/* bLength */
  36,			/* descriptor type */
  2,			/* MIDI_IN_JACK desc subtype */
  1,			/* EMBEDDED bJackType */
  1,			/* bJackID */
  0,			/* iJack */
  
  /* descriptor follows inline: */
  6,			/* bLength */
  36,			/* descriptor type */
  2,			/* MIDI_IN_JACK desc subtype */
  2,			/* EXTERNAL bJackType */
  2,			/* bJackID */
  0,			/* iJack */

//B.4.4 MIDI OUT Jack Descriptor
  /* descriptor follows inline: */
  9,			/* length of descriptor in bytes */
  36,			/* descriptor type */
  3,			/* MIDI_OUT_JACK descriptor */
  1,			/* EMBEDDED bJackType */
  3,			/* bJackID */
  1,			/* No of input pins */
  2,			/* BaSourceID */
  1,			/* BaSourcePin */
  0,			/* iJack */

  /* descriptor follows inline: */
  9,			/* bLength of descriptor in bytes */
  36,			/* bDescriptorType */
  3,			/* MIDI_OUT_JACK bDescriptorSubtype */
  2,			/* EXTERNAL bJackType */
  4,			/* bJackID */
  1,			/* bNrInputPins */
  1,			/* baSourceID (0) */
  1,			/* baSourcePin (0) */
  0,			/* iJack */


// B.5 Bulk OUT Endpoint Descriptors

//B.5.1 Standard Bulk OUT Endpoint Descriptor
  /* descriptor follows inline: */
  9,			/* bLenght */
  USBDESCR_ENDPOINT,	/* bDescriptorType = endpoint */
  0x1,			/* bEndpointAddress OUT endpoint number 1 */
  3,			/* bmAttributes: 2:Bulk, 3:Interrupt endpoint */
  8, 0,			/* wMaxPacketSize */
  10,			/* bIntervall in ms */
  0,			/* bRefresh */
  0,			/* bSyncAddress */
  
// B.5.2 Class-specific MS Bulk OUT Endpoint Descriptor
  /* descriptor follows inline: */
  5,			/* bLength of descriptor in bytes */
  37,			/* bDescriptorType */
  1,			/* bDescriptorSubtype */
  1,			/* bNumEmbMIDIJack  */
  1,			/* baAssocJackID (0) */


//B.6 Bulk IN Endpoint Descriptors

//B.6.1 Standard Bulk IN Endpoint Descriptor
  /* descriptor follows inline: */
  9,			/* bLenght */
  USBDESCR_ENDPOINT,	/* bDescriptorType = endpoint */
  0x81,			/* bEndpointAddress IN endpoint number 1 */
  3,			/* bmAttributes: 2: Bulk, 3: Interrupt endpoint */
  8, 0,			/* wMaxPacketSize */
  10,			/* bIntervall in ms */
  0,			/* bRefresh */
  0,			/* bSyncAddress */

// B.6.2 Class-specific MS Bulk IN Endpoint Descriptor
  /* descriptor follows inline: */
  5,			/* bLength of descriptor in bytes */
  37,			/* bDescriptorType */
  1,			/* bDescriptorSubtype */
  1,			/* bNumEmbMIDIJack (0) */
  3,			/* baAssocJackID (0) */
};


uchar usbFunctionDescriptor(usbRequest_t * rq) {
  if (rq->wValue.bytes[1] == USBDESCR_DEVICE) {
    usbMsgPtr = (uchar *) deviceDescrMIDI;
    return sizeof(deviceDescrMIDI);
  } else {		/* must be config descriptor */
    usbMsgPtr = (uchar *) configDescrMIDI;
    return sizeof(configDescrMIDI);
  }
}


static uchar sendEmptyFrame;


/* ------------------------------------------------------------------------- */
/* ----------------------------- USB interface ----------------------------- */
/* ------------------------------------------------------------------------- */

uchar usbFunctionSetup(uchar data[8]) {
  usbRequest_t *rq = (void *) data;

  if ((rq->bmRequestType & USBRQ_TYPE_MASK) == USBRQ_TYPE_CLASS) {/* class request type */
    
    /*  Prepare bulk-in endpoint to respond to early termination   */
    if ((rq->bmRequestType & USBRQ_DIR_MASK) ==
	USBRQ_DIR_HOST_TO_DEVICE)
      sendEmptyFrame = 1;
  }
  return 0xff;
}


/*---------------------------------------------------------------------------*/
/* usbFunctionRead                                                           */
/*---------------------------------------------------------------------------*/

uchar usbFunctionRead(uchar * data, uchar len) {
  data[0] = 0;
  data[1] = 0;
  data[2] = 0;
  data[3] = 0;
  data[4] = 0;
  data[5] = 0;
  data[6] = 0;
  
  return 7;
}


/*---------------------------------------------------------------------------*/
/* usbFunctionWrite                                                          */
/*                                                                           */
/* this Function is called if a MIDI Out message (from PC) arrives.          */
/*                                                                           */
/*---------------------------------------------------------------------------*/

uchar usbFunctionWrite(uchar * data, uchar len) {
  PORTB ^= 0x10;		// DEBUG LED (PB4)
  // PORTB ^= 0x08;		// DEBUG LED (PB3)
  // parseMidiMessage(data);
  return 1;
}

/*---------------------------------------------------------------------------*/
/* usbFunctionWriteOut                                                       */
/*                                                                           */
/* this Function is called if a MIDI Out message (from PC) arrives.          */
/*                                                                           */
/*---------------------------------------------------------------------------*/

void usbFunctionWriteOut(uchar * data, uchar len) {
  PORTB ^= 0x08;		// DEBUG LED PB3
  parseMidiMessage(data, len);
}



//-----------------------------------------------
// CV出力電圧セット

void SetCV(uchar playNote) {
  uchar m;

  if( playNote < 24)
    playNote = 24;
  if( playNote > 84)
    playNote = 84;
  
  noteSave = playNote;

  m = ((playNote - 24) * 3) / 4;
  
  CVduty = playNote * 8 + (PBBuf / 4) + 248 + m;
}

//-----------------------------------------------
// ノートON処理

void NoteON(uchar note) {
  uchar i;
  uchar max = 0;
  
  // バッファに登録済のノートは無視
  for(i = 0; i < NoteCnt; i++) {
    if(NoteBuf[i] == note) {
      return;
    }
  }
  
  // バッファがいっぱい？
  if(NoteCnt == MAX_NOTE_CNT) {
    // 玉突き処理
    for(i = 0; i < (MAX_NOTE_CNT - 1); i++) {
      NoteBuf[i] = NoteBuf[i+1];
    }
    NoteBuf[MAX_NOTE_CNT - 1] = note;
  } else {
    NoteBuf[NoteCnt] = note;
    NoteCnt++;
  }
  
  // 最高音
  for(i = 0; i < NoteCnt; i++) {
    if(max < NoteBuf[i]) {
      max = NoteBuf[i];
    }
  }
  SetCV(max);
  //  GATE1_ON;
  gate = 1;
  trig = TRIG_COUNT;
}

//-----------------------------------------------
// ノートOFF処理

void NoteOFF(uchar note) {
  uchar i;
  uchar max = 0;
  BOOL flg = FALSE;
  
  // バッファに登録されている場合は削除
  for(i = 0; i < NoteCnt; i++) {
    if(flg) {
      NoteBuf[i-1] = NoteBuf[i];
    }
    if(NoteBuf[i] == note) {
      flg = TRUE;
    }
  }
  if(flg) NoteCnt--;
  
  if(NoteCnt == 0) {
    // バッファがカラの場合はGATEオフ
    // GATE1_OFF;
    gate = 0;
  } else {
    // 最高音
    for(i = 0; i < NoteCnt; i++) {
      if(max < NoteBuf[i]) {
	max = NoteBuf[i];
      }
    }
    SetCV(max);
  }
}

void parseMidiMessage(uchar *data, uchar len) {
  uchar cin = (*data) & 0x0f;	/* CABLE NOを無視する */
  uchar Rch = (*(data+1)) & 0x0f;
  uchar note = *(data+2);

  if (Rch != 0)			/* only channel 0 */
    return;

  // PORTB ^= 0x10;		// DEBUG LED (PB4)

  switch(cin) {
  case 8:			/* NOTE OFF */
    NoteOFF(note);
    break;
  case 9:			/* NOTE ON */
    if( *(data + 3) == 0){
      NoteOFF(note);
    } else {
      NoteON(note);
    }
    break;
  case 14:			/* PITCH BEND */
    PBBuf = (*(data + 3)) & 0x7f; /* use only MSB(7bit) */
    SetCV(noteSave);
    break;
    //  case 0:			/* MISC FUNCTION CODE */
    //  case 1:			/* CABLE EVENT */
    //  case 2:			/* SYSTEM COMMON (2 byte) */
    //  case 3:			/* SYSTEM COMMON (3 byte) */
    //  case 4:			/* SYS EX START */
    //  case 5:			/* SYSTEM EX end (with 1 byte) */
    //  case 6:			/* SYSTEM EX end (with 2 byte) */
    //  case 7:			/* SYSTEM EX end (with 3 byte) */
    //  case 10:		/* POLY KEY PRESS */
    //  case 11:		/* CONTROL CHANGE */
    //  case 12:		/* PROGRAM CHANGE */
    //  case 13:		/* CHANNEL PRESSURE */
    //  case 15:		/* SINGLE BYTE */
    //    break;
  }

  if (len > 4) {
    parseMidiMessage(data+4, len-4);
  }
}

/*---------------------------------------------------------------------------*/
/* hardwareInit                                                              */
/*---------------------------------------------------------------------------*/

static void hardwareInit(void) {
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

}


/* ------------------------------------------------------------------------- */
/* ------------------------ Oscillator Calibration ------------------------- */
/* ------------------------------------------------------------------------- */

/* Calibrate the RC oscillator to 8.25 MHz. The core clock of 16.5 MHz is
 * derived from the 66 MHz peripheral clock by dividing. Our timing reference
 * is the Start Of Frame signal (a single SE0 bit) available immediately after
 * a USB RESET. We first do a binary search for the OSCCAL value and then
 * optimize this value with a neighboorhod search.
 * This algorithm may also be used to calibrate the RC oscillator directly to
 * 12 MHz (no PLL involved, can therefore be used on almost ALL AVRs), but this
 * is wide outside the spec for the OSCCAL value and the required precision for
 * the 12 MHz clock! Use the RC oscillator calibrated to 12 MHz for
 * experimental purposes only!
 */
static void calibrateOscillator(void) {
  uchar    step = 128;
  uchar    trialValue = 0, optimumValue;
  int      x, optimumDev, targetValue = (unsigned)(1499 * (double)F_CPU / 10.5e6 + 0.5);

    /* do a binary search: */
    do{
        OSCCAL = trialValue + step;
        x = usbMeasureFrameLength();    /* proportional to current real frequency */
        if(x < targetValue)             /* frequency still too low */
            trialValue += step;
        step >>= 1;
    }while(step > 0);
    /* We have a precision of +/- 1 for optimum OSCCAL here */
    /* now do a neighborhood search for optimum value */
    optimumValue = trialValue;
    optimumDev = x; /* this is certainly far away from optimum */
    for(OSCCAL = trialValue - 1; OSCCAL <= trialValue + 1; OSCCAL++){
        x = usbMeasureFrameLength() - targetValue;
        if(x < 0)
            x = -x;
        if(x < optimumDev){
            optimumDev = x;
            optimumValue = OSCCAL;
        }
    }
    OSCCAL = optimumValue;
}
/*
Note: This calibration algorithm may try OSCCAL values of up to 192 even if
the optimum value is far below 192. It may therefore exceed the allowed clock
frequency of the CPU in low voltage designs!
You may replace this search algorithm with any other algorithm you like if
you have additional constraints such as a maximum CPU clock.
For version 5.x RC oscillators (those with a split range of 2x128 steps, e.g.
ATTiny25, ATTiny45, ATTiny85), it may be useful to search for the optimum in
both regions.
*/

void    usbEventResetReady(void) {
  calibrateOscillator();
  eeprom_write_byte(0, OSCCAL);   /* store the calibrated value in EEPROM */
}

int main(void) {
  uchar i;
  uchar   calibrationValue;

  calibrationValue = eeprom_read_byte(0); /* calibration value from last time */
  if(calibrationValue != 0xff){
    OSCCAL = calibrationValue;
  }
  odDebugInit();
  usbDeviceDisconnect();
  for(i=0;i<20;i++){  /* 300 ms disconnect */
    _delay_ms(15);
  }

  // グローバル変数の初期化
  PBBuf = 64;
  NoteCnt = 0;
  CVduty = 0;
  gate = 0;
  trig = 0;
  noteSave = 0;

  wdt_enable(WDTO_1S);
  hardwareInit();
  odDebugInit();
  usbInit();

  //PORTB = 0xff;		/* activate all pull-ups */
  DDRB |= 0x19;		/* PB0, PB3,PB4 output */

  /* OC0Aを初期化 */
  TCCR0A = 0x83;		/* clear OC0A (non inverting mode), unuse OC0B */
  TCCR0B = 0x01;		/* 1/1 clock(no prescaling) */
  OCR0A = 0;			/* for CV */

  sendEmptyFrame = 0;

  sei();

  for (;;) {		/* main event loop */
    wdt_reset();
    usbPoll();

    // USB送信処理
    if (usbInterruptIsReady()) {
      // send midi message here
      // 何もしない
    }	// usbInterruptIsReady()

    // CVを出力する
    if(trig > 0) {
      OCR0A = 255;
      trig--;
    } else {
      OCR0A = gate ? (CVduty >>2) : 0;
    }
  }			// main event loop
  return 0;
}
