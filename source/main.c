// well hello there

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <gccore.h>
#include <wiiuse/wpad.h>
#include <fat.h>
#include "grabmii.h"

int main(int argc, char **argv) {
    init_video();
    WPAD_Init();
    PAD_Init();
    bool useUSB = false;
    fatInitDefault(); 
    ISFS_Initialize();

    while (1) {
        u32 pressed = readButtons();
        if (pressed & WPAD_BUTTON_MINUS) {
            useUSB = !useUSB;
            printf(CLS);
        }
        if (pressed & WPAD_BUTTON_PLUS) {
            backupRflDb = !backupRflDb;
            printf(CLS);
        }

        char *device = useUSB ? "usb:/" : "sd:/";
        char backupDir[64];
        sprintf(backupDir, "%smii_backup", device);
        bool deviceReady = isDeviceReady(device);

        printf("\x1b[2;0H");
        printf("==========================================\n");
        printf("         GrabMii v%s by Null          \n", GRABMII_VERSION);
        printf("==========================================\n\n");
        printf("Target Device: [ %s ] (Press - to swap)\n", device);
        if (deviceReady) {
            printf("Status: \x1b[38;5;46mInitialized\x1b[38;5;255m\n"); // Green for good
        } else {
            printf("Status: \x1b[38;5;196mNot Initialized\x1b[38;5;255m\n"); // Red for bad (not really, just wasn't initialized)
        }
        printf("------------------------------------------\n");
        printf("A: Select a Mii from NAND to Backup to %s\n", device);
        printf("B: Select a Mii from %s to Import to NAND\n", device);
        printf("1: Select a Mii from %s to Convert\n", device);
        printf("2: Select Miis from NAND to Delete\n");
        printf("Press + to toggle Mii Database backup when dealing with NAND\n");
        if (backupRflDb) {
            printf("\x1b[38;5;46m[ ON ]\x1b[38;5;255m  OFF\n\n");
        } else {
            printf("  ON  \x1b[38;5;196m[ OFF ]\x1b[38;5;255m\n\n");
        }
        printf("Home: Exit App\n");

        if (pressed & WPAD_BUTTON_HOME) {
            printf("Bye bye!\n");
            VIDEO_WaitVSync();
            sleep(1);
            ISFS_Deinitialize();
            exit(0);
        }

        // 1 BUTTON LOGIC 
        if (pressed & WPAD_BUTTON_1) {
            if (deviceReady) {
                if (showMiiExplanation()) { 
                    runConvertMenu(backupDir, device);
                } else {
                    printf("\nCancelled.\n");
                    sleep(1);
                }
                printf(CLS);
            } else {
                printf("\nCannot access %s. Make sure it is plugged in.\n", device);
                sleep(2);
                printf(CLS);
            }
        }

        // 2 BUTTON LOGIC 
        if (pressed & WPAD_BUTTON_2) {
            if (deviceReady) {
                runDeleteMenu(backupDir, device);
                printf(CLS);
            } else {
                printf("\nCannot access %s. Make sure it is plugged in.\n", device);
                sleep(2);
                printf(CLS);
            }
        }

        // B BUTTON LOGIC 
        if (pressed & WPAD_BUTTON_B) {
            if (deviceReady) {
                if (showNandWarning()) { 
                    MiiItem items[MAX_MIIS];
                    int count = loadFilesystemMiis(backupDir, items);
                    runChecklistMenu(items, count, true, backupDir, device);
                } else {
                    printf("\nCancelled.\n");
                    sleep(1);
                }
                printf(CLS);
            } else {
                printf("\nCannot access %s. Make sure it is plugged in.\n", device);
                sleep(2);
                printf(CLS);
            }
        }

        // A BUTTON LOGIC 
        if (pressed & WPAD_BUTTON_A) {
            if (deviceReady) {
                MiiItem items[MAX_MIIS];
                int count = loadNandMiis(items, NULL);
                if (count < 0) {
                    showNandReadError(count);
                } else {
                    runChecklistMenu(items, count, false, backupDir, device);
                }
                printf(CLS);
            } else {
                printf("\nCannot access %s. Make sure it is plugged in.\n", device);
                sleep(2);
                printf(CLS);
            }
        }
        VIDEO_WaitVSync();
    }
    return 0;
}