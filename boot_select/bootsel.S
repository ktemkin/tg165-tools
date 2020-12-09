/**
 * Minimal program that allows the user to select between several
 * target applications. Note that this program does not have a vector table--
 * instead, we patch the main application's vector table to point to this code,
 * and jump back when we want to run the main application.
 *
 * This is nice, as we don't have to worry about relocating the main
 * application-- it can run in the location it was originally linked for.
 * The downside is that we don't get to use enable interrupts until we reach our
 * target firmware.
 */

.section .text
.global _start

// The entry point of the unpatched main firmware.
// This is the second word in the firmware, or the third in an Upgrade.bin.
#define MAIN_FW_ENTRY_POINT          0x080136b5

// The vector tables for any alternate firmwares we may want to load.
#define ALT_BOOTLOADER_VECTOR_TABLE  0x08050100
#define ALT_FW_VECTOR_TABLE          0x08053000

// The base address of the port that houses our buttons.
// All of the buttons we're interested in are on PORTC.
#define BUTTON_CONFIG_REG_LOW        0x40011000
#define BUTTON_INPUT_REGISTER        0x40011008

// UP + OK
#define BOOTLOADER_BUTTON_MASK       0x00000018

// Just OK
#define ALTERNATE_BUTTON_MASK        0x00000008

// VTOR register address
#define VECTOR_TABLE_BASE            0xE000ED08


_start:
    .code   16

    // First, we'll check to see if UP and OK are both pressed.
    // In this case, we'll jump to the alternate bootloader.
    ldr r5, =BOOTLOADER_BUTTON_MASK
    bl buttons_match_mask
    cbnz r0, run_alt_bootloader

    // Check to see if just OK is pressed.
    // In this case, we'll jump to the alternate firmware.
    ldr r5, =ALTERNATE_BUTTON_MASK
    bl buttons_match_mask
    cbnz r0, run_alt_firmware

    // If both of those checks fail, continue immediately to
    // the main FLIR firmware.
    ldr r0, =MAIN_FW_ENTRY_POINT
    bx r0


// Run the alternate bootloader.
// Does not return.
run_alt_bootloader:
    ldr r5, =ALT_BOOTLOADER_VECTOR_TABLE
    b boot_from_vector_table

// Run the alternate firmware.
// Does not return.
run_alt_firmware:
    ldr r5, =ALT_FW_VECTOR_TABLE
    b boot_from_vector_table


// arguments:
//    r5 = vector table to boot from
// does not return!
boot_from_vector_table:
    ldr r1, [r5] // new stack pointer
    ldr r0, [r5, #4] // new entry point

    // Relocate the vector table to reflect the target program.
    ldr r4, =VECTOR_TABLE_BASE
    str r5, [r4]

    // Apply the new stack pointer, and jump to the relevant
    // program.
    mov sp, r1
    bx r0


// arguments:
//    r5 = mask to match
// returns:
//    r0 = 1 if the buttom mask is matche exactly, or 0 otherwise
buttons_match_mask:
    ldr r4, =BUTTON_INPUT_REGISTER

    // Read the input value from our input register. Normally, we'd
    // set up the GPIO and AFIO first, but the FLIR bootloader
    // has just done this for us, so we'll skip doing so for now.
    // Hey, this /is/ a hack. =P
    ldr r0, [r4]

    // Invert the result, as our butons are active low.
    mvn r0, r0

    // Mask away the unneeded bits, and see if both relevant bits
    // are set.
    and r0, r5
    cmp r0, r5
    beq match

    mov r0, #0
    bx lr

match:
    mov r0, #1
    bx lr


