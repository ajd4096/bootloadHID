/* Name: bootloaderconfig.h
 * Project: AVR bootloader HID
 * Author: Christian Starkjohann
 * Creation Date: 2007-03-19
 * Tabsize: 4
 * Copyright: (c) 2007 by OBJECTIVE DEVELOPMENT Software GmbH
 * License: GNU GPL v2 (see License.txt)
 * This Revision: $Id: bootloaderconfig.h 699 2008-11-26 19:50:32Z cs $
 */

#ifndef __bootloaderconfig_h_included__
#define __bootloaderconfig_h_included__

/*
General Description:
This file (together with some settings in Makefile) configures the boot loader
according to the hardware.

This file contains (besides the hardware configuration normally found in
usbconfig.h) two functions or macros: bootLoaderInit() and
bootLoaderCondition(). Whether you implement them as macros or as static
inline functions is up to you, decide based on code size and convenience.

bootLoaderInit() is called as one of the first actions after reset. It should
be a minimum initialization of the hardware so that the boot loader condition
can be read. This will usually consist of activating a pull-up resistor for an
external jumper which selects boot loader mode. You may call leaveBootloader()
from this function if you know that the main code should run.

bootLoaderCondition() is called immediately after initialization and in each
main loop iteration. If it returns TRUE, the boot loader will be active. If it
returns FALSE, the boot loader jumps to address 0 (the loaded application)
immediately.

For compatibility with Thomas Fischl's avrusbboot, we also support the macro
names BOOTLOADER_INIT and BOOTLOADER_CONDITION for this functionality. If
these macros are defined, the boot loader usees them.
*/

/* ---------------------------- Hardware Config ---------------------------- */

#define USB_CFG_IOPORTNAME      B
/* This is the port where the USB bus is connected. When you configure it to
 * "B", the registers PORTB, PINB and DDRB will be used.
 */
#define USB_CFG_DMINUS_BIT      0
/* This is the bit number in USB_CFG_IOPORT where the USB D- line is connected.
 * This may be any bit in the port.
 */
#define USB_CFG_DPLUS_BIT       1
/* This is the bit number in USB_CFG_IOPORT where the USB D+ line is connected.
 * This may be any bit in the port. Please note that D+ must also be connected
 * to interrupt pin INT0! [You can also use other interrupts, see section
 * "Optional MCU Description" below, or you can connect D- to the interrupt, as
 * it is required if you use the USB_COUNT_SOF feature. If you use D- for the
 * interrupt, the USB interrupt will also be triggered at Start-Of-Frame
 * markers every millisecond.]
 */
#define USB_CFG_CLOCK_KHZ       (F_CPU/1000)
/* Clock rate of the AVR in MHz. Legal values are 12000, 12800, 15000, 16000,
 * 16500 and 20000. The 12.8 MHz and 16.5 MHz versions of the code require no
 * crystal, they tolerate +/- 1% deviation from the nominal frequency. All
 * other rates require a precision of 2000 ppm and thus a crystal!
 * Default if not specified: 12 MHz
 */

/* ----------------------- Optional Hardware Config ------------------------ */

/* #define USB_CFG_PULLUP_IOPORTNAME   D */
/* If you connect the 1.5k pullup resistor from D- to a port pin instead of
 * V+, you can connect and disconnect the device from firmware by calling
 * the macros usbDeviceConnect() and usbDeviceDisconnect() (see usbdrv.h).
 * This constant defines the port on which the pullup resistor is connected.
 */
/* #define USB_CFG_PULLUP_BIT          4 */
/* This constant defines the bit number in USB_CFG_PULLUP_IOPORT (defined
 * above) where the 1.5k pullup resistor is connected. See description
 * above for details.
 */

/* --------------------------- Functional Range ---------------------------- */

/* ------------------------ Entering/Exiting Bootloader -------------------- */
/* There are two mutually exclusive ways to enter the boot loader:
 * 1) Manually by asserting bootLoaderCondition(). The boot loader waits
 *    until it is signaled to exit by either the programmer closing the
 *    connection or bootLooaderCondition() is no longer asserted. See
 *    USING_PUSHBUTTON and BOOTLOADER_CAN_EXIT to learn how to modify
 *    the exit behavior. This is the default entrance method.
 * 2) Automatically but the boot loader exits after a time out period has
 *    elapsed or the programmer closes the connection. See the comments
 *    for BOOTLOADER_CAN_EXIT to learn how to modify the exit behavior.
 */

#define BOOTLOADER_CAN_EXIT     1
/* If this macro is defined to 1, the boot loader command line utility can
 * initiate a reboot after uploading the FLASH when the "-r" command line
 * option is given. If you define it to 0 or leave it undefined, the "-r"
 * option won't work and you save a couple of bytes in the boot loader. This
 * may be of advantage if you compile with gcc 4 instead of gcc 3 because it
 * generates slightly larger code.
 * If you need to save even more memory, consider using your own vector table.
 * Since only the reset vector and INT0 (the first two vectors) are used,
 * this saves quite a bit of flash. See Alexander Neumann's boot loader for
 * an example: http://git.lochraster.org:2080/?p=fd0/usbload;a=tree
 */

#define TIMEOUT_ENABLED            1
/* If TIMEOUT_ENABLED is defined to 1 then the boot loader will always load
 * and stay active until the programmer closes the connection or the time
 * out period has elapsed. Since the boot loader always loads there is no
 * need for a jumper on the bootLoaderCondition() pin. Costs ~108 bytes.
 */

#define TIMEOUT_DURATION           60
/* The number of seconds the boot loader waits before exiting if no activity
 * has occured during the timeout interval. If TIMEOUT_ENABLED is defined
 * to 0 this define will be ignored. Maximum value is 255 seconds.
 */

/* ------------------------------------------------------------------------- */

/* Example configuration: Port D bit 3 is connected to a jumper which ties
 * this pin to GND if the boot loader is requested. Initialization allows
 * several clock cycles for the input voltage to stabilize before
 * bootLoaderCondition() samples the value.
 * We use a function for bootLoaderInit() for convenience and a macro for
 * bootLoaderCondition() for efficiency.
 */

#ifndef __ASSEMBLER__   /* assembler cannot parse function definitions */
#include <util/delay.h>

#define JUMPER_BIT  7   /* jumper is connected to this bit in port D, active low */

#ifndef MCUCSR          /* compatibility between ATMega8 and ATMega88 */
#   define MCUCSR   MCUSR
#endif

#ifndef TIMEOUT_ENABLED
#   define TIMEOUT_ENABLED 0
#endif

static inline void  bootLoaderInit(void)
{
#if !TIMEOUT_ENABLED
    PORTD |= (1 << JUMPER_BIT);     /* activate pull-up */
    _delay_us(10);  /* wait for levels to stabilize */
#endif
    if(!(MCUCSR & (1 << EXTRF)))    /* If this was not an external reset, ignore */
        leaveBootloader();
    MCUCSR = 0;                     /* clear all reset flags for next time */
}

static inline void  bootLoaderExit(void)
{

#if !TIMEOUT_ENABLED
    PORTD = 0;                      /* undo bootLoaderInit() changes */
#else
    TCCR1B = 0; // turn off timer1 and reset to initial value
#endif

DDRC = 0; // turn off LEDs
}

#if !TIMEOUT_ENABLED
#    define bootLoaderCondition()   ((PIND & (1 << JUMPER_BIT)) == 0)
#else
#    define bootLoaderCondition()    1
#endif


#endif

/* ------------------------------------------------------------------------- */

#endif /* __bootloader_h_included__ */
