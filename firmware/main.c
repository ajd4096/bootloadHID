/* Name: main.c
 * Project: AVR bootloader HID
 * Author: Christian Starkjohann
 * Creation Date: 2007-03-19
 * Tabsize: 4
 * Copyright: (c) 2007 by OBJECTIVE DEVELOPMENT Software GmbH
 * License: GNU GPL v2 (see License.txt)
 * This Revision: $Id: main.c 788 2010-05-30 20:54:41Z cs $
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/wdt.h>
#include <avr/boot.h>
#include <string.h>
#include <util/delay.h>

static void leaveBootloader() __attribute__((__noreturn__));

#include "bootloaderconfig.h"
#include "usbdrv.c"

/* ------------------------------------------------------------------------ */

#ifndef ulong
#   define ulong    unsigned long
#endif
#ifndef uint
#   define uint     unsigned int
#endif

#ifndef TIMEOUT_ENABLED
#   define TIMEOUT_ENABLED 0
#endif
#ifndef TIMEOUT_DURATION
#   define TIMEOUT_DURATION 10
#endif

#if (FLASHEND) > 0xffff /* we need long addressing */
#   define addr_t           ulong
#else
#   define addr_t           uint
#endif

static addr_t           currentAddress; /* in bytes */
static uchar            offset;         /* data already processed in current transfer */
#if BOOTLOADER_CAN_EXIT
static uchar            exitMainloop;
#endif


PROGMEM char usbHidReportDescriptor[33] = {
    0x06, 0x00, 0xff,              // USAGE_PAGE (Generic Desktop)
    0x09, 0x01,                    // USAGE (Vendor Usage 1)
    0xa1, 0x01,                    // COLLECTION (Application)
    0x15, 0x00,                    //   LOGICAL_MINIMUM (0)
    0x26, 0xff, 0x00,              //   LOGICAL_MAXIMUM (255)
    0x75, 0x08,                    //   REPORT_SIZE (8)

    0x85, 0x01,                    //   REPORT_ID (1)
    0x95, 0x06,                    //   REPORT_COUNT (6)
    0x09, 0x00,                    //   USAGE (Undefined)
    0xb2, 0x02, 0x01,              //   FEATURE (Data,Var,Abs,Buf)

    0x85, 0x02,                    //   REPORT_ID (2)
    0x95, 0x83,                    //   REPORT_COUNT (131)
    0x09, 0x00,                    //   USAGE (Undefined)
    0xb2, 0x02, 0x01,              //   FEATURE (Data,Var,Abs,Buf)
    0xc0                           // END_COLLECTION
};

/* allow compatibility with avrusbboot's bootloaderconfig.h: */
#ifdef BOOTLOADER_INIT
#   define bootLoaderInit()         BOOTLOADER_INIT
#endif
#ifdef BOOTLOADER_CONDITION
#   define bootLoaderCondition()    BOOTLOADER_CONDITION
#endif

/* device compatibility: */
#ifndef TIFR1    /* ATMega8 uses TIFR instead of TIFR1 */
#   define TIFR1     TIFR
#endif
#ifndef GICR    /* ATMega*8 don't have GICR, use MCUCR instead */
#   define GICR     MCUCR
#endif
/* compatibility with ATMega88 and other new devices: */
#ifndef TCCR0
#define TCCR0   TCCR0B
#endif
#ifndef GICR
#define GICR    MCUCR
#endif

#if TIMEOUT_ENABLED
static uint8_t          inactivity_timer_nsec;
#endif

/* ------------------------------------------------------------------------ */

#if TIMEOUT_ENABLED
static void inactivity_timer_start(void);
static void inactivity_timer_stop(void);
#endif
static void (*nullVector)(void) __attribute__((__noreturn__));

static void leaveBootloader()
{
    DBG1(0x01, 0, 0);
    cli();
    boot_rww_enable();
    USB_INTR_ENABLE = 0;
    USB_INTR_CFG = 0;       /* also reset config bits */
#if F_CPU == 12800000
    TCCR0 = 0;              /* default value */
#endif
    GICR = (1 << IVCE);     /* enable change of interrupt vectors */
    GICR = (0 << IVSEL);    /* move interrupts to application flash section */
/* We must go through a global function pointer variable instead of writing
 *  ((void (*)(void))0)();
 * because the compiler optimizes a constant 0 to "rcall 0" which is not
 * handled correctly by the assembler.
 */
    nullVector();
}

uchar   usbFunctionSetup(uchar data[8])
{
usbRequest_t    *rq = (void *)data;
#if TIMEOUT_ENABLED
    inactivity_timer_stop();
#endif
static uchar    replyBuffer[7] = {
        1,                              /* report ID */
        SPM_PAGESIZE & 0xff,
        SPM_PAGESIZE >> 8,
        ((long)FLASHEND + 1) & 0xff,
        (((long)FLASHEND + 1) >> 8) & 0xff,
        (((long)FLASHEND + 1) >> 16) & 0xff,
        (((long)FLASHEND + 1) >> 24) & 0xff
    };

    if(rq->bRequest == USBRQ_HID_SET_REPORT){
        if(rq->wValue.bytes[0] == 2){
            offset = 0;
            return USB_NO_MSG;
        }
#if BOOTLOADER_CAN_EXIT
        else{
            exitMainloop = 1;
        }
#endif
    }else if(rq->bRequest == USBRQ_HID_GET_REPORT){
        usbMsgPtr = replyBuffer;
        return 7;
    }
#if TIMEOUT_ENABLED
    inactivity_timer_start();
#endif

    return 0;
}

uchar usbFunctionWrite(uchar *data, uchar len)
{
union {
    addr_t  l;
    uint    s[sizeof(addr_t)/2];
    uchar   c[sizeof(addr_t)];
}       address;
uchar   isLast;

#if TIMEOUT_ENABLED
    inactivity_timer_stop();
#endif
    address.l = currentAddress;
    if(offset == 0){
        DBG1(0x30, data, 3);
        address.c[0] = data[1];
        address.c[1] = data[2];
#if (FLASHEND) > 0xffff /* we need long addressing */
        address.c[2] = data[3];
        address.c[3] = 0;
#endif
        data += 4;
        len -= 4;
    }
    DBG1(0x31, (void *)&currentAddress, 4);
    offset += len;
    isLast = offset & 0x80; /* != 0 if last block received */
    do{
        addr_t prevAddr;
#if SPM_PAGESIZE > 256
        uint pageAddr;
#else
        uchar pageAddr;
#endif
        DBG1(0x32, 0, 0);
        pageAddr = address.s[0] & (SPM_PAGESIZE - 1);
        if(pageAddr == 0){              /* if page start: erase */
            DBG1(0x33, 0, 0);
#ifndef TEST_MODE
            cli();
            boot_page_erase(address.l); /* erase page */
            sei();
            boot_spm_busy_wait();       /* wait until page is erased */
#endif
        }
        cli();
        boot_page_fill(address.l, *(short *)data);
        sei();
        prevAddr = address.l;
        address.l += 2;
        data += 2;
        /* write page when we cross page boundary */
        pageAddr = address.s[0] & (SPM_PAGESIZE - 1);
        if(pageAddr == 0){
            DBG1(0x34, 0, 0);
#ifndef TEST_MODE
            cli();
            boot_page_write(prevAddr);
            sei();
            boot_spm_busy_wait();
#endif

        }
        len -= 2;
    }while(len);
    currentAddress = address.l;
    DBG1(0x35, (void *)&currentAddress, 4);

#if TIMEOUT_ENABLED
    inactivity_timer_start();
#endif

    return isLast;
}

#if TIMEOUT_ENABLED
static void inactivity_timer_start(void)
{
    inactivity_timer_nsec = 0;
    TCNT1 = 0; // reset timer to 0
    TCCR1B |= ((1 << CS12) | (1 << CS10));    // start timer, 1024 prescale
}

static void inactivity_timer_stop(void)
{
    TCCR1B &= ~((1 << CS12) | (1 << CS11) | (1 << CS10)); // stop timer
}
#endif

/* ------------------------------------------------------------------------ */

static void initForUsbConnectivity(void)
{
uchar   i = 0;

#if F_CPU == 12800000
    TCCR0 = 3;          /* 1/64 prescaler */
#endif
    usbInit();
    /* enforce USB re-enumerate: */
    usbDeviceDisconnect();  /* do this while interrupts are disabled */
    do{             /* fake USB disconnect for > 250 ms */
        wdt_reset();
        _delay_ms(1);
    }while(--i);
    usbDeviceConnect();

#if TIMEOUT_ENABLED
    TCCR1B |= (1 << WGM12); // put timer1 in CTC mode
    OCR1A = F_CPU/1024; // number of prescaled ticks per second
#endif

    sei();

#if TIMEOUT_ENABLED
    inactivity_timer_start();
#endif
}

int __attribute__((noreturn)) main(void)
{
    /* initialize hardware */
    bootLoaderInit();
    odDebugInit();
    DBG1(0x00, 0, 0);
    /* jump to application if jumper is set */
    if(bootLoaderCondition()){
        uchar i = 0, j = 0;
#ifndef TEST_MODE
        GICR = (1 << IVCE);  /* enable change of interrupt vectors */
        GICR = (1 << IVSEL); /* move interrupts to boot flash section */
#endif
        initForUsbConnectivity();
        do{ /* main event loop */
            wdt_reset();
            usbPoll();
#if TIMEOUT_ENABLED
            if (TIFR1 & (1 << OCF1A)){
                inactivity_timer_nsec++;
                TIFR1 = (1 << OCF1A); // clear interrupt
            }
            if (inactivity_timer_nsec >= TIMEOUT_DURATION){
                /* turn on red LED to signal boot loader has timed out */
                DDRC |= (1 << PC1);  // turn on red LED
                break;
            }
#endif
#if BOOTLOADER_CAN_EXIT
            if(exitMainloop){
#if F_CPU == 12800000
                break;  /* memory is tight at 12.8 MHz, save exit delay below */
#endif
                if(--i == 0){
                    if(--j == 0)
                        break;
                }
            }
#endif
        }while(bootLoaderCondition());
    }
    leaveBootloader();
}

