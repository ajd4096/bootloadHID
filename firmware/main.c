/* Name: main.c
 * Project: AVR bootloader HID
 * Author: Christian Starkjohann
 * Creation Date: 2007-03-19
 * Tabsize: 4
 * Copyright: (c) 2007 by OBJECTIVE DEVELOPMENT Software GmbH
 * License: GNU GPL v2 (see License.txt)
 * This Revision: $Id: main.c 281 2007-03-20 13:22:10Z cs $
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/wdt.h>
#include <avr/boot.h>
#include <string.h>

#include "usbdrv.h"
#include "oddebug.h"
#include "bootloaderconfig.h"

static char             reportId = -1;
static unsigned long    currentAddress; /* in bytes */
static uchar            offset;         /* data already processed in current transfer */
static uchar            exitMainloop;

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

static void (*nullVector)(void) __attribute__((__noreturn__));

static void leaveBootloader() __attribute__((__noreturn__));
static void leaveBootloader()
{
    DBG1(0x01, 0, 0);
    cli();
    boot_rww_enable();
    GICR = (1 << IVCE);  /* enable change of interrupt vectors */
    GICR = (0 << IVSEL); /* move interrupts to application flash section */
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
static uchar    replyBuffer[7] = {
        1,                              /* report ID */
        SPM_PAGESIZE & 0xff,
        SPM_PAGESIZE >> 8,
        ((long)FLASHEND + 1) & 0xff,
        ((long)(FLASHEND + 1) >> 8) & 0xff,
        ((long)(FLASHEND + 1) >> 16) & 0xff,
        ((long)(FLASHEND + 1) >> 24) & 0xff
    };

    if(rq->bRequest == USBRQ_HID_SET_REPORT){
        reportId = rq->wValue.bytes[0];    /* store report ID */
        offset = 0;
        return 0xff;
    }else if(rq->bRequest == USBRQ_HID_GET_REPORT){
        usbMsgPtr = replyBuffer;
        return 7;
    }
    return 0;
}

uchar usbFunctionWrite(uchar *data, uchar len)
{
union {
    unsigned long   l;
    unsigned short  s[2];
    uchar           c[4];
}       address;

    if(reportId == 1){          /* leave boot loader */
        exitMainloop = 1;
        return 1;
    }else if(reportId == 2){    /* write page */
        if(offset == 0){
            data++;
            DBG1(0x30, data, 4);
            address.c[0] = *data++;
            address.c[1] = *data++;
            address.c[2] = *data++;
            address.c[3] = 0;
            len -= 4;
        }else{
            DBG1(0x31, (void *)&currentAddress, 4);
            address.l = currentAddress;
        }
        offset += len;
        len >>= 1;
        do{
            DBG1(0x32, 0, 0);
            if((address.s[0] & (SPM_PAGESIZE - 1)) == 0){   /* if page start: erase */
                DBG1(0x33, 0, 0);
#ifndef TEST_MODE
                cli();
                boot_page_erase(address.l);     /* erase page */
                sei();
                boot_spm_busy_wait();           /* wait until page is erased */
#endif
            }
            cli();
            boot_page_fill(address.l, *(short *)data);
            sei();
            address.l += 2;
            data += 2;
            /* write page when we cross page boundary */
            if((address.s[0] & (SPM_PAGESIZE - 1)) == 0){
                DBG1(0x34, 0, 0);
#ifndef TEST_MODE
                cli();
                boot_page_write(address.l - 2);
                sei();
                boot_spm_busy_wait();
#endif
            }
        }while(--len != 0);
        currentAddress = address.l;
        DBG1(0x35, (void *)&currentAddress, 4);
        if(offset < 128){
            return 0;
        }else{
            reportId = -1;
            return 1;
        }
    }
    return 1;
}

int main(void)
{
uchar   i, j = 0;

    /* initialize hardware */
    bootLoaderInit();
    odDebugInit();
    DBG1(0x00, 0, 0);
    /* jump to application if jumper is set */
    if(bootLoaderCondition()){
#ifndef TEST_MODE
        GICR = (1 << IVCE);  /* enable change of interrupt vectors */
        GICR = (1 << IVSEL); /* move interrupts to boot flash section */
#endif
#ifdef USB_CFG_PULLUP_IOPORTNAME
        while(--j){     /* USB Reset by device only required on Watchdog Reset */
            i = 0;
            while(--i); /* delay >10ms for USB reset */
        }
        usbDeviceConnect();
#else
        USBDDR = (1 << USB_CFG_DMINUS_BIT) | (1 << USB_CFG_DPLUS_BIT);  /* issue RESET */
        while(--j){     /* USB Reset by device only required on Watchdog Reset */
            i = 0;
            while(--i); /* delay >10ms for USB reset */
        }
        USBDDR = 0;
#endif
        usbInit();
        sei();
        while(bootLoaderCondition()){ /* main event loop */
            usbPoll();
            if(exitMainloop){
                i = 0;
                while(--i)
                    usbPoll();      /* try to send the response */
                break;
            }
        }
    }
    leaveBootloader();
    return 0;
}

