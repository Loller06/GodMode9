/*
 Written by Wolfvak, specially sublicensed under the GPLv2
 Read LICENSE for more details
*/

#include "common.h"
#include "fsinit.h"
#include "fsutil.h"
#include "qrcodegen.h"
#include "power.h"
#include "rtc.h"
#include "hid.h"
#include "ui.h"

#include <arm.h>

#define PC_DUMPRAD (0x30)
#define SP_DUMPLEN (0x60)

#define XRQ_DUMPDATAFUNC(type, size) \
int XRQ_DumpData_##type(char *b, u32 s, u32 e) \
{ \
    char *c = b; \
    while(s<e) { \
        b+=sprintf(b, "%08lX: ",s); \
        type *dl = (type*)s; \
        for (u32 i=0; i<(16/((size)/2)); i++) { \
            b+=sprintf(b, "%0" #size "lX ", (u32)dl[i]); \
        } \
        b+=sprintf(b, "\n"); \
        s+=16; \
    } \
    return (int)(b-c); \
}
XRQ_DUMPDATAFUNC(u8,  2)
XRQ_DUMPDATAFUNC(u16, 4)
XRQ_DUMPDATAFUNC(u32, 8)


const char *XRQ_Name[] = {
    "Reset", "Undefined", "SWI", "Prefetch Abort",
    "Data Abort", "Reserved", "IRQ", "FIQ"
};

extern char __stack_top;

void XRQ_DumpRegisters(u32 xrq, u32 *regs)
{
    u32 sp, st, pc;
    char dumpstr[2048], *wstr = dumpstr;
    
    DsTime dstime;
    get_dstime(&dstime);

    
    /* Dump registers */
    wstr += sprintf(wstr, "Exception: %s (%lu)\n", XRQ_Name[xrq&7], xrq);
    wstr += sprintf(wstr, "20%02lX-%02lX-%02lX %02lX:%02lX:%02lX\n \n",
        (u32) dstime.bcd_Y, (u32) dstime.bcd_M, (u32) dstime.bcd_D,
        (u32) dstime.bcd_h, (u32) dstime.bcd_m, (u32) dstime.bcd_s);
    for (int i = 0; i < 8; i++) {
        int i_ = i*2;
        wstr += sprintf(wstr,
        "R%02d: %08lX | R%02d: %08lX\n", i_, regs[i_], i_+1, regs[i_+1]);
    }
    wstr += sprintf(wstr, "CPSR: %08lX\n\n", regs[16]);

    
    /* Output registers to main screen */
    u32 draw_width = GetDrawStringWidth(dumpstr);
    u32 draw_height = GetDrawStringHeight(dumpstr);
    u32 draw_x = (SCREEN_WIDTH_MAIN - draw_width) / 2;
    u32 draw_y = (SCREEN_HEIGHT - draw_height) / 2;
    u32 draw_y_upd = draw_y + draw_height - 10;
    
    ClearScreen(MAIN_SCREEN, COLOR_STD_BG);
    DrawStringF(MAIN_SCREEN, draw_x, draw_y, COLOR_STD_FONT, COLOR_STD_BG, dumpstr);


    /* Dump STACK */
    sp = regs[13] & ~0xF;
    st = (u32)&__stack_top;
    wstr += sprintf(wstr, "Stack dump:\n");
    wstr += XRQ_DumpData_u8(wstr, sp, min(sp+SP_DUMPLEN, st));
    wstr += sprintf(wstr, "\n");


    /* Dump TEXT */
    pc = regs[15] & ~0xF;
    wstr += sprintf(wstr, "Code dump:\n");
    if (regs[16] & SR_THUMB) {
        wstr += XRQ_DumpData_u16(wstr, pc-PC_DUMPRAD, pc+PC_DUMPRAD);
    } else {
        wstr += XRQ_DumpData_u32(wstr, pc-PC_DUMPRAD, pc+PC_DUMPRAD);
    }
    
    
    /* Draw QR Code */
    if ((MAIN_BUFFER_SIZE >= qrcodegen_BUFFER_LEN_MAX) &&
        (TEMP_BUFFER_SIZE >= qrcodegen_BUFFER_LEN_MAX * 8 * BYTES_PER_PIXEL)) {
        u8* qrcode = (u8*) MAIN_BUFFER;
        u8* temp = (u8*) TEMP_BUFFER;
        u8* bitmap = temp;
        DrawStringF(MAIN_SCREEN, draw_x, draw_y_upd, COLOR_STD_FONT, COLOR_STD_BG,
            "%-29.29s", "Generating QR code...");
        if (qrcodegen_encodeText(dumpstr, temp, qrcode, qrcodegen_Ecc_HIGH,
            qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true)) {
            u8* ptr = bitmap;
            u32 size = qrcodegen_getSize(qrcode);
            for (u32 y = 0; y < size; y++) {
                for (u32 x = 0; x < size; x++) {
                    u8 color = qrcodegen_getModule(qrcode, x, y) ? 0x00 : 0xFF;
                    *(ptr++) = color;
                    *(ptr++) = color;
                    *(ptr++) = color;
                }
            }
            ClearScreen(ALT_SCREEN, COLOR_WHITE);
            DrawBitmap(ALT_SCREEN, (SCREEN_WIDTH_ALT - size) / 2, (SCREEN_HEIGHT - size) / 2, size, size, bitmap);
        }
    }

    
    /* Reinitialize SD */
    DrawStringF(MAIN_SCREEN, draw_x, draw_y_upd, COLOR_STD_FONT, COLOR_STD_BG,
        "%-29.29s", "Reinitializing SD card...");
    while (!InitSDCardFS()) {
        if (InputWait(1) & BUTTON_POWER) PowerOff();
        DeinitSDCardFS();
    }


    /* Dump to SD */
    char path[64];
    snprintf(path, 64, "%s/exception_dump_%02lX%02lX%02lX%02lX%02lX%02lX.txt", OUTPUT_PATH,
        (u32) dstime.bcd_Y, (u32) dstime.bcd_M, (u32) dstime.bcd_D,
        (u32) dstime.bcd_h, (u32) dstime.bcd_m, (u32) dstime.bcd_s);
    DrawStringF(MAIN_SCREEN, draw_x, draw_y_upd, COLOR_STD_FONT, COLOR_STD_BG,
        "%-29.29s", "Dumping state to SD card...");
    FileSetData(path, dumpstr, wstr - dumpstr, 0, true);
    
    
    /* Deinit SD */
    DeinitSDCardFS();
    
    /* Done, wait for user power off */
    DrawStringF(MAIN_SCREEN, draw_x, draw_y_upd, COLOR_STD_FONT, COLOR_STD_BG,
        "%-29.29s", "Press POWER to turn off");
    while (!(InputWait(0) & BUTTON_POWER));
    PowerOff();

    
    /* We will not return */
    return;
}
