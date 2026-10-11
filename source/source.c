#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <malloc.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <gccore.h>
#include <wiiuse/wpad.h>
#include <fat.h>
#include <ogc/machine/processor.h>
#include <dirent.h>
#include <network.h>
#include <time.h>
#include <ogc/lwp_watchdog.h>
#include "grabmii.h"

bool backupRflDb = true;

static void *xfb = NULL;
static GXRModeObj *rmode = NULL;

void init_video() {
    VIDEO_Init();
    rmode = VIDEO_GetPreferredMode(NULL);
    xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    console_init(xfb, 20, 20, rmode->fbWidth, rmode->xfbHeight, rmode->fbWidth * VI_DISPLAY_PIX_SZ);
    VIDEO_Configure(rmode);
    VIDEO_SetNextFramebuffer(xfb);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();
}

u32 readButtons() {
    WPAD_ScanPads();
    PAD_ScanPads();
    u32 wii = WPAD_ButtonsDown(0);
    u32 gc = PAD_ButtonsDown(0);
    u32 out = wii & 0xFFFF;
    if (wii & WPAD_CLASSIC_BUTTON_UP) out |= WPAD_BUTTON_UP;
    if (wii & WPAD_CLASSIC_BUTTON_DOWN) out |= WPAD_BUTTON_DOWN;
    if (wii & WPAD_CLASSIC_BUTTON_A) out |= WPAD_BUTTON_A;
    if (wii & WPAD_CLASSIC_BUTTON_B) out |= WPAD_BUTTON_B;
    if (wii & WPAD_CLASSIC_BUTTON_PLUS) out |= WPAD_BUTTON_PLUS;
    if (wii & WPAD_CLASSIC_BUTTON_MINUS) out |= WPAD_BUTTON_MINUS;
    if (wii & WPAD_CLASSIC_BUTTON_HOME) out |= WPAD_BUTTON_HOME;
    if (wii & WPAD_CLASSIC_BUTTON_X) out |= WPAD_BUTTON_1;
    if (wii & WPAD_CLASSIC_BUTTON_Y) out |= WPAD_BUTTON_2;
    if (gc & PAD_BUTTON_UP) out |= WPAD_BUTTON_UP;
    if (gc & PAD_BUTTON_DOWN) out |= WPAD_BUTTON_DOWN;
    if (gc & PAD_BUTTON_A) out |= WPAD_BUTTON_A;
    if (gc & PAD_BUTTON_B) out |= WPAD_BUTTON_B;
    if (gc & PAD_BUTTON_START) out |= WPAD_BUTTON_PLUS;
    if (gc & PAD_TRIGGER_L) out |= WPAD_BUTTON_MINUS;
    if (gc & PAD_TRIGGER_Z) out |= WPAD_BUTTON_HOME;
    if (gc & PAD_BUTTON_X) out |= WPAD_BUTTON_1;
    if (gc & PAD_BUTTON_Y) out |= WPAD_BUTTON_2;
    return out;
}

static void waitForA() {
    printf("\nPress A to continue\n");
    while (1) {
        if (readButtons() & WPAD_BUTTON_A) break;
        VIDEO_WaitVSync();
    }
}

// CCITT/XMODEM CRC16 boring stuff
u16 calculate_crc16(const u8 *buf, u32 len) {
    u16 crc = 0x0000;
    for (u32 i = 0; i < len; i++) {
        crc ^= (u16)buf[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc <<= 1;
        }
    }
    return crc;
}
u8 calcMiiChecksum8(const u8 macPrefix[3]) {
    u32 total = (u32)macPrefix[0] + macPrefix[1] + macPrefix[2];
    bool isSpecialPrefix = (macPrefix[0] == 0x00 && macPrefix[1] == 0x17 && macPrefix[2] == 0xAB);
    if (!isSpecialPrefix) {
        total &= 0x7F;
    }
    return (u8)total;
}

// Get your Wii's Client ID so that when you install a Mii to the NAND, you can actually edit it.
bool getLocalMiiSystemID(u8 out[4]) {
    u8 mac[6];
    s32 res = net_get_mac_address(mac);
    if (res < 0) return false;
    out[0] = calcMiiChecksum8(mac); 
    out[1] = mac[3];
    out[2] = mac[4];
    out[3] = mac[5]; 
    return true;
}

void extractNickname(const u8 *mii, char *out) {
    int pos = 0;
    for (int i = 0; i < 10; i++) {
        u8 lowByte = mii[MII_OFF_NAME + 1 + i * 2];
        if (lowByte == 0) break;
        if (lowByte < 128)
            out[pos++] = (char)lowByte;
        else
            out[pos++] = '?';
    }
    out[pos] = '\0';
}

static void copyName(char *dst, size_t size, const char *src) {
    size_t len = strlen(src);
    if (len >= size) len = size - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

int loadFilesystemMiisCFL(const char *directory, MiiItem *items, int maxCount) {
    DIR *dir = opendir(directory);
    if (!dir) return 0;
    struct dirent *entry;
    int count = 0;
    int skipped = 0;
    while ((entry = readdir(dir)) != NULL && count < maxCount) {
        if (entry->d_name[0] == '.') continue;
        const char *ext = strrchr(entry->d_name, '.');
        if (ext && (strcasecmp(ext, ".ffsd") == 0 || strcasecmp(ext, ".cfsd") == 0)) {
            char path[512];
            snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
            FILE *f = fopen(path, "rb");
            if (f) {
                u8 raw96[96];
                if (fread(raw96, 1, 96, f) == 96) {
                    if (convertFflStoreData(raw96, items[count].rawData)) {
                        copyName(items[count].filename, sizeof(items[count].filename), entry->d_name);
                        extractNickname(items[count].rawData, items[count].nickname);
                        items[count].selected = false;
                        items[count].slotIndex = -1;
                        count++;
                    } else {
                        printf("\nSkipped %s: checksum invalid (not a full/valid file)\n", entry->d_name);
                        skipped++;
                    }
                } else {
                    printf("\nSkipped %s: file is too small\n", entry->d_name);
                    skipped++;
                }
                fclose(f);
            }
        }
    }
    closedir(dir);
    if (skipped > 0) waitForA();
    return count;
}

int loadFilesystemMiis(const char *directory, MiiItem *items) {
    DIR *dir = opendir(directory);
    if (!dir) return 0;
    struct dirent *entry;
    int count = 0;
    int skipped = 0;
    while ((entry = readdir(dir)) != NULL && count < MAX_MIIS) {
        if (entry->d_name[0] == '.') continue;
        const char *ext = strrchr(entry->d_name, '.');
        if (ext && (strcasecmp(ext, ".mae") == 0 || strcasecmp(ext, ".miigx") == 0 || strcasecmp(ext, ".rcd") == 0)) {
            char path[512];
            snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
            FILE *f = fopen(path, "rb");
            if (f) {
                fseek(f, 0, SEEK_END);
                long size = ftell(f);
                fseek(f, 0, SEEK_SET);
                if ((size == MII_SIZE || size == MII_SIZE + 2) && fread(items[count].rawData, 1, MII_SIZE, f) == MII_SIZE) {
                    copyName(items[count].filename, sizeof(items[count].filename), entry->d_name);
                    extractNickname(items[count].rawData, items[count].nickname);
                    items[count].selected = false;
                    items[count].slotIndex = -1;
                    count++;
                } else {
                    printf("\nSkipped %s: unexpected size (%ld bytes)\n", entry->d_name, size);
                    skipped++;
                }
                fclose(f);
            }
        }
    }
    closedir(dir);
    if (skipped > 0) waitForA();
    return count;
}

int loadNandMiis(MiiItem *items, u8 *dbBufferOut) {
    s32 fd = ISFS_Open(MII_DB_PATH, ISFS_OPEN_READ);
    if (fd < 0) return fd;
    u8 *buffer = (u8 *)memalign(32, DB_TOTAL_SIZE);
    if (!buffer) {
        ISFS_Close(fd);
        return -1;
    }
    if (ISFS_Read(fd, buffer, DB_TOTAL_SIZE) != DB_TOTAL_SIZE) {
        free(buffer);
        ISFS_Close(fd);
        return -2;
    }

    // Really just verifying the Mii
    int count = 0;
    for (int i = 0; i < MAX_MIIS; i++) {
        int offset = DB_HEADER_SIZE + (i * MII_SIZE);
        if (buffer[offset + MII_OFF_MIIID + 0] != 0 ||
            buffer[offset + MII_OFF_MIIID + 1] != 0 ||
            buffer[offset + MII_OFF_MIIID + 2] != 0 ||
            buffer[offset + MII_OFF_MIIID + 3] != 0) {
            items[count].slotIndex = i;
            memcpy(items[count].rawData, buffer + offset, MII_SIZE);
            extractNickname(items[count].rawData, items[count].nickname);
            char safe[16];
            int len = 0;
            for (const char *p = items[count].nickname; *p && len < 10; p++) {
                safe[len++] = isalnum((unsigned char)*p) ? *p : '_';
            }
            safe[len] = '\0';
            if (len == 0) strcpy(safe, "mii");
            snprintf(items[count].filename, sizeof(items[count].filename), "%s_%02d", safe, i);
            items[count].selected = false;
            count++;
        }
    }
    if (dbBufferOut) memcpy(dbBufferOut, buffer, DB_TOTAL_SIZE);
    free(buffer);
    ISFS_Close(fd);
    return count;
}
int find_first_empty_slot(const u8 *dbBuffer) {
    for (int i = 0; i < MAX_MIIS; i++) {
        int offset = DB_HEADER_SIZE + (i * MII_SIZE);
        if (dbBuffer[offset + MII_OFF_MIIID + 0] == 0 &&
            dbBuffer[offset + MII_OFF_MIIID + 1] == 0 &&
            dbBuffer[offset + MII_OFF_MIIID + 2] == 0 &&
            dbBuffer[offset + MII_OFF_MIIID + 3] == 0) {
            return i;
        }
    }
    return -1;
}
static int miiIdSlot(const u8 *dbBuffer, const u8 *id) {
    for (int i = 0; i < MAX_MIIS; i++) {
        int offset = DB_HEADER_SIZE + (i * MII_SIZE);
        if (memcmp(dbBuffer + offset + MII_OFF_MIIID, id, 4) == 0) {
            return i;
        }
    }
    return -1;
}

static const char *nandErrorText(s32 code) {
    switch (code) {
        case -1: return "Out of memory";
        case -2: return "Couldn't read RFL_DB.dat";
        case -3: return "Write failed, Mii Channel data may be damaged";
        case -4: return "RFL_DB.dat checksum doesn't match, nothing was changed";
        case -5: return "Couldn't write the RFL_DB.dat backup, nothing was changed";
        default: return "Couldn't open RFL_DB.dat";
    }
}

static void showFailed(const char *verb) {
    printf("\nOOPS Wii haz failed :( 0 Miis %s.\n", verb);
    printf("Press the Home button to go back with depression.\n");
    while (1) {
        if (readButtons() & WPAD_BUTTON_HOME) break;
        VIDEO_WaitVSync();
    }
}

static bool writeDbBackup(const u8 *dbBuffer, const char *backupDir) {
    mkdir(backupDir, 0777);
    char path[128];
    for (int n = 1; n <= 99; n++) {
        snprintf(path, sizeof(path), "%s/RFL_DB_%02d.bak", backupDir, n);
        FILE *probe = fopen(path, "rb");
        if (!probe) break;
        fclose(probe);
    }
    FILE *bak = fopen(path, "wb");
    if (!bak) return false;
    bool ok = fwrite(dbBuffer, 1, DB_TOTAL_SIZE, bak) == DB_TOTAL_SIZE;
    fclose(bak);
    if (ok) printf("\nBacked up RFL_DB.dat to %s\n", path);
    return ok;
}

// Install selected Miis to NAND
s32 batch_install_to_nand(MiiItem *items, int count, const char *backupDir) {
    s32 fd = ISFS_Open(MII_DB_PATH, ISFS_OPEN_READ | ISFS_OPEN_WRITE);
    if (fd < 0) return fd;
    u8 *dbBuffer = (u8 *)memalign(32, DB_TOTAL_SIZE);
    if (!dbBuffer) {
        ISFS_Close(fd);
        return -1;
    }
    if (ISFS_Read(fd, dbBuffer, DB_TOTAL_SIZE) != DB_TOTAL_SIZE) {
        free(dbBuffer);
        ISFS_Close(fd);
        return -2;
    }
    u16 storedCrc = ((u16)dbBuffer[DB_TOTAL_SIZE - 2] << 8) | dbBuffer[DB_TOTAL_SIZE - 1];
    if (calculate_crc16(dbBuffer, DB_TOTAL_SIZE - 2) != storedCrc) {
        free(dbBuffer);
        ISFS_Close(fd);
        return -4;
    }

    // Allow you to edit the installed Miis
    u8 localSystemID[4];
    bool haveLocalID = getLocalMiiSystemID(localSystemID);
    int installedCount = 0;
    bool backedUp = false;
    for (int i = 0; i < count; i++) {
        if (!items[i].selected) continue;
        u8 *id = items[i].rawData + MII_OFF_MIIID;
        if (id[0] == 0 && id[1] == 0 && id[2] == 0 && id[3] == 0) {
            generateWiiMiiID(items[i].rawData, MII_SIZE, id);
        }
        int existing = miiIdSlot(dbBuffer, id);
        if (existing >= 0) {
            const u8 *other = dbBuffer + DB_HEADER_SIZE + (existing * MII_SIZE);
            const u8 *mine = items[i].rawData;
            bool same = memcmp(other, mine, MII_OFF_SYSTEMID) == 0 &&
                        memcmp(other + MII_OFF_SYSTEMID + 4, mine + MII_OFF_SYSTEMID + 4, MII_SIZE - MII_OFF_SYSTEMID - 4) == 0;
            if (same) {
                printf("\nSkipped %s: already in NAND (slot %d, ID %02X%02X%02X%02X)\n", items[i].nickname, existing, id[0], id[1], id[2], id[3]);
                continue;
            }
            generateWiiMiiID(mine, MII_SIZE, id);
            if (miiIdSlot(dbBuffer, id) >= 0) {
                printf("\nSkipped %s: couldn't find a free Mii ID\n", items[i].nickname);
                continue;
            }
            printf("\n%s had the same ID as a different Mii (slot %d), gave it a new ID\n", items[i].nickname, existing);
        }
        int slot = find_first_empty_slot(dbBuffer);
        if (slot == -1) {
            printf("\nWarning: NAND Mii database is full!");
            break;
        }
        if (haveLocalID) {
            memcpy(items[i].rawData + MII_OFF_SYSTEMID, localSystemID, 4);
        }
        if (backupRflDb && !backedUp) {
            if (!writeDbBackup(dbBuffer, backupDir)) {
                free(dbBuffer);
                ISFS_Close(fd);
                return -5;
            }
            backedUp = true;
        }
        int targetOffset = DB_HEADER_SIZE + (slot * MII_SIZE);
        memcpy(dbBuffer + targetOffset, items[i].rawData, MII_SIZE);
        installedCount++;
    }
    if (!haveLocalID) {
        printf("\nWarning: could not read console MAC address (for some reason) - installed Miis may be not-editable.\n");
    }
    if (installedCount > 0) {
        // Recalculate CRC16
        u16 newCrc = calculate_crc16(dbBuffer, DB_TOTAL_SIZE - 2);
        dbBuffer[DB_TOTAL_SIZE - 2] = (newCrc >> 8) & 0xFF;
        dbBuffer[DB_TOTAL_SIZE - 1] = newCrc & 0xFF;

        ISFS_Seek(fd, 0, SEEK_SET);
        s32 bytesWritten = ISFS_Write(fd, dbBuffer, DB_TOTAL_SIZE);
        free(dbBuffer);
        ISFS_Close(fd);
        return (bytesWritten == DB_TOTAL_SIZE) ? installedCount : -3;
    }
    free(dbBuffer);
    ISFS_Close(fd);
    return 0;
}

// Render the little checklist
void renderChecklist(MiiItem *items, int count, int cursor, const char *title, const char *device, bool showExt) {
    int selectedCount = 0;
    for (int i = 0; i < count; i++) {
        if (items[i].selected) selectedCount++;
    }
    printf(CLS);
    printf("==========================================\n");
    printf(" %s\n", title);
    printf(" Target Device: [ %s ]  Selected: %d | %d/%d\n", device, selectedCount, count > 0 ? cursor + 1 : 0, count);
    printf("==========================================\n");
    printf(" [Up/Down]: Navigate | [A]: Select | [1]: All/None\n");
    printf(" [+]: Go! Go! Go! | [B]: Cancel/Back\n");
    printf("------------------------------------------\n");

    if (count == 0) {
        printf(" No Miis found!\n");
        return;
    }

    // Scroll and checklist logic
    int windowSize = 10;
    int start = cursor - (windowSize / 2);
    if (start < 0) start = 0;
    if (start + windowSize > count) start = count - windowSize;
    if (start < 0) start = 0;
    for (int i = start; i < start + windowSize && i < count; i++) {
        const char *cursorStr = (i == cursor) ? "->" : "  ";
        const char *checkStr = items[i].selected ? "[X]" : "[ ]";
        if (showExt) {
            char shown[128];
            const char *ext = strrchr(items[i].filename, '.');
            if (strlen(items[i].filename) <= 34) {
                snprintf(shown, sizeof(shown), "%s", items[i].filename);
            } else {
                int keep = 32 - (ext ? (int)strlen(ext) : 0);
                if (keep < 1) keep = 1;
                snprintf(shown, sizeof(shown), "%.*s..%s", keep, items[i].filename, ext ? ext : "");
            }
            printf(" %s %s %2d. %s (%s)\n", cursorStr, checkStr, i + 1, items[i].nickname, shown);
        } else {
            const u8 *id = items[i].rawData + MII_OFF_MIIID;
            printf(" %s %s %2d. %s (ID %02X%02X%02X%02X)\n", cursorStr, checkStr, i + 1, items[i].nickname, id[0], id[1], id[2], id[3]);
        }
    }
}

static const char *exportExts[3] = { ".miigx", ".mae", ".rcd" };

int askExportFormat(const char *word) {
    int sel = 0;
    while (1) {
        printf(CLS);
        printf("==========================================\n");
        printf(" Which file format would you like the\n");
        printf(" %s Mii(s)?\n", word);
        printf("==========================================\n");
        printf(" [Up/Down]: Choose | [A]: Confirm | [B]: Back\n");
        printf("------------------------------------------\n");
        for (int i = 0; i < 3; i++) {
            printf(" %s %s\n", (i == sel) ? "[X]" : "[ ]", exportExts[i]);
        }
        while (1) {
            u32 pressed = readButtons();
            if (pressed & WPAD_BUTTON_UP) {
                if (sel > 0) sel--;
                break;
            }
            if (pressed & WPAD_BUTTON_DOWN) {
                if (sel < 2) sel++;
                break;
            }
            if (pressed & WPAD_BUTTON_A) return sel;
            if (pressed & WPAD_BUTTON_B) return -1;
            VIDEO_WaitVSync();
        }
    }
}

static void toggleAll(MiiItem *items, int count) {
    bool all = true;
    for (int i = 0; i < count; i++) {
        if (!items[i].selected) all = false;
    }
    for (int i = 0; i < count; i++) {
        items[i].selected = !all;
    }
}

static bool writeMiiFile(const char *directory, const char *baseName, const char *ext, const u8 *data) {
    char path[160];
    for (int n = 1; n < 100; n++) {
        if (n == 1) {
            snprintf(path, sizeof(path), "%s/%s%s", directory, baseName, ext);
        } else {
            snprintf(path, sizeof(path), "%s/%s_%d%s", directory, baseName, n, ext);
        }
        FILE *f = fopen(path, "rb");
        if (!f) {
            f = fopen(path, "wb");
            if (!f) return false;
            size_t written = fwrite(data, 1, MII_SIZE, f);
            fclose(f);
            return written == MII_SIZE;
        }
        u8 existing[MII_SIZE];
        size_t got = fread(existing, 1, MII_SIZE, f);
        fclose(f);
        if (got == MII_SIZE && memcmp(existing, data, MII_SIZE) == 0) return true;
    }
    return false;
}

// Run the little checklist
void runChecklistMenu(MiiItem *items, int count, bool isInstalling, const char *backupDir, const char *device) {
    if (count <= 0) {
        printf(CLS);
        printf("No Miis found to process.\nPress B to return.");
        while (1) {
            if (readButtons() & WPAD_BUTTON_B) break;
            VIDEO_WaitVSync();
        }
        return;
    }
    int cursor = 0;

    while (1) {
        renderChecklist(items, count, cursor, isInstalling ? "Install Miis to NAND" : "Backup Miis to Storage", device, isInstalling);
        while (1) {
            u32 pressed = readButtons();

            if (pressed & WPAD_BUTTON_UP) {
                if (cursor > 0) cursor--;
                break;
            }
            if (pressed & WPAD_BUTTON_DOWN) {
                if (cursor < count - 1) cursor++;
                break;
            }
            if (pressed & WPAD_BUTTON_A) {
                items[cursor].selected = !items[cursor].selected;
                break;
            }
            if (pressed & WPAD_BUTTON_1) {
                toggleAll(items, count);
                break;
            }
            if (pressed & WPAD_BUTTON_B) {
                return; 
            }
            if (pressed & WPAD_BUTTON_PLUS) {
                int format = 0;
                if (!isInstalling) {
                    format = askExportFormat("dumped");
                    if (format < 0) break;
                }
                printf("\nWorking on it...\n");

                if (isInstalling) {
                    s32 res = batch_install_to_nand(items, count, backupDir);
                    if (res == 0) {
                        showFailed("installed");
                        return;
                    } else if (res > 0) {
                        printf("\nWii haz success! Installed %d Mii(s) to NAND.\n", res);
                    } else {
                        printf("\nError installing to NAND (%d): %s\n", res, nandErrorText(res));
                    }
                } else {
                    mkdir(backupDir, 0777);
                    int dumped = 0;
                    for (int i = 0; i < count; i++) {
                        if (items[i].selected) {
                            char baseName[64];
                            snprintf(baseName, sizeof(baseName), "%s", items[i].filename);
                            char *dot = strrchr(baseName, '.');
                            if (dot) *dot = '\0';
                            if (writeMiiFile(backupDir, baseName, exportExts[format], items[i].rawData)) {
                                dumped++;
                            }
                        }
                    }
                    if (dumped == 0) {
                        showFailed("dumped");
                        return;
                    }
                    printf("\nWii haz success! %d Mii(s) dumped to %s\n", dumped, backupDir);
                }
                waitForA();
                return;
            }
            VIDEO_WaitVSync();
        }
    }
}

// Displays a safety warning when installing (so basically just what Priiloader says but for Mii Channel)
bool showNandWarning() {
    printf(CLS);
    printf("==========================================\n");
    printf("                 WARNING!                  \n");
    printf("==========================================\n\n");
    printf(" INSTALLING MIIS REQUIRES WRITING DIRECTLY\n");
    printf(" TO THE WII SYSTEM NAND (RFL_DB.dat to be specific).\n\n");
    printf(" IF SOMETHING GOES WRONG OR IT IS INTERRUPTED,\n");
    printf(" YOUR MII CHANNEL DATA COULD BE CORRUPTED,\n");
    printf(" AND YOU MAY BE REQUIRED TO FORMAT\n");
    printf(" THE MII CHANNEL, LOSING ALL SAVED MIIS.\n\n");
    printf(" * DO NOT TURN OFF THE CONSOLE.\n");
    printf(" * DO NOT REMOVE YOUR STORAGE DEVICE.\n\n");
    printf(" THIS TOOL COMES WITHOUT ANY WARRANTIES\n");
    printf(" AND YOU ARE USING THIS APP AT YOUR OWN RISK.\n\n");
    printf(" This is version BETA %s. App is almost done!\n", GRABMII_VERSION);
    printf(" but I need beta testers XD\n");
    printf("------------------------------------------\n");

    // Haha better read this warning buddy
    for (int i = 5; i > 0; i--) {
        printf("\r You may proceed in %d ", i);
        fflush(stdout);
        sleep(1);
    }
    printf("\r Press [A] to Accept & Continue | [B] to Cancel\n");

    while (1) {
        u32 pressed = readButtons();

        if (pressed & WPAD_BUTTON_A) {
            return true; 
        }
        if (pressed & WPAD_BUTTON_B) {
            return false; 
        }
        VIDEO_WaitVSync();
    }
}

// Checks if the target device (sd:/ or usb:/) is ready to go
bool isDeviceReady(const char *devicePath) {
    DIR *dir = opendir(devicePath);
    if (dir) {
        closedir(dir);
        return true;
    }
    return false;
}

void showNandReadError(s32 code) {
    printf(CLS);
    printf("Couldn't read Miis from NAND (%d): %s\nPress B to return.\n", (int)code, nandErrorText(code));
    while (1) {
        if (readButtons() & WPAD_BUTTON_B) break;
        VIDEO_WaitVSync();
    }
}

s32 delete_from_nand(MiiItem *items, int count, const char *backupDir) {
    s32 fd = ISFS_Open(MII_DB_PATH, ISFS_OPEN_READ | ISFS_OPEN_WRITE);
    if (fd < 0) return fd;
    u8 *dbBuffer = (u8 *)memalign(32, DB_TOTAL_SIZE);
    if (!dbBuffer) {
        ISFS_Close(fd);
        return -1;
    }
    if (ISFS_Read(fd, dbBuffer, DB_TOTAL_SIZE) != DB_TOTAL_SIZE) {
        free(dbBuffer);
        ISFS_Close(fd);
        return -2;
    }
    u16 storedCrc = ((u16)dbBuffer[DB_TOTAL_SIZE - 2] << 8) | dbBuffer[DB_TOTAL_SIZE - 1];
    if (calculate_crc16(dbBuffer, DB_TOTAL_SIZE - 2) != storedCrc) {
        free(dbBuffer);
        ISFS_Close(fd);
        return -4;
    }

    int deleted = 0;
    bool backedUp = false;
    for (int i = 0; i < count; i++) {
        if (!items[i].selected) continue;
        if (items[i].slotIndex < 0 || items[i].slotIndex >= MAX_MIIS) continue;
        if (backupRflDb && !backedUp) {
            if (!writeDbBackup(dbBuffer, backupDir)) {
                free(dbBuffer);
                ISFS_Close(fd);
                return -5;
            }
            backedUp = true;
        }
        memset(dbBuffer + DB_HEADER_SIZE + (items[i].slotIndex * MII_SIZE), 0, MII_SIZE);
        deleted++;
    }
    if (deleted > 0) {
        u16 newCrc = calculate_crc16(dbBuffer, DB_TOTAL_SIZE - 2);
        dbBuffer[DB_TOTAL_SIZE - 2] = (newCrc >> 8) & 0xFF;
        dbBuffer[DB_TOTAL_SIZE - 1] = newCrc & 0xFF;

        ISFS_Seek(fd, 0, SEEK_SET);
        s32 bytesWritten = ISFS_Write(fd, dbBuffer, DB_TOTAL_SIZE);
        free(dbBuffer);
        ISFS_Close(fd);
        return (bytesWritten == DB_TOTAL_SIZE) ? deleted : -3;
    }
    free(dbBuffer);
    ISFS_Close(fd);
    return 0;
}

bool confirmDelete(int selectedCount) {
    printf(CLS);
    printf("==========================================\n");
    printf(" Delete %d Mii(s) from the NAND?\n", selectedCount);
    printf("==========================================\n");
    if (backupRflDb) {
        printf(" A backup of RFL_DB.dat is saved first.\n\n");
    } else {
        printf(" This can't be undone.\n\n");
    }
    printf(" [A]: Delete | [B]: Back\n");
    while (1) {
        u32 pressed = readButtons();
        if (pressed & WPAD_BUTTON_A) return true;
        if (pressed & WPAD_BUTTON_B) return false;
        VIDEO_WaitVSync();
    }
}

void runDeleteMenu(const char *backupDir, const char *device) {
    MiiItem items[MAX_MIIS];
    int count = loadNandMiis(items, NULL);
    if (count < 0) {
        showNandReadError(count);
        return;
    }
    for (int i = 0; i < count; i++) {
        char label[48];
        snprintf(label, sizeof(label), "[%02d] %s", items[i].slotIndex, items[i].nickname);
        copyName(items[i].nickname, sizeof(items[i].nickname), label);
    }
    if (count == 0) {
        printf(CLS);
        printf("No Miis found in the NAND.\nPress B to return.");
        while (1) {
            if (readButtons() & WPAD_BUTTON_B) break;
            VIDEO_WaitVSync();
        }
        return;
    }
    int cursor = 0;

    while (1) {
        renderChecklist(items, count, cursor, "Delete Miis from NAND ([slot] name)", device, false);
        while (1) {
            u32 pressed = readButtons();

            if (pressed & WPAD_BUTTON_UP) {
                if (cursor > 0) cursor--;
                break;
            }
            if (pressed & WPAD_BUTTON_DOWN) {
                if (cursor < count - 1) cursor++;
                break;
            }
            if (pressed & WPAD_BUTTON_A) {
                items[cursor].selected = !items[cursor].selected;
                break;
            }
            if (pressed & WPAD_BUTTON_1) {
                toggleAll(items, count);
                break;
            }
            if (pressed & WPAD_BUTTON_B) {
                return;
            }
            if (pressed & WPAD_BUTTON_PLUS) {
                int selectedCount = 0;
                for (int i = 0; i < count; i++) {
                    if (items[i].selected) selectedCount++;
                }
                if (selectedCount == 0) break;
                if (!confirmDelete(selectedCount)) break;
                printf("\nWorking on it...\n");
                s32 res = delete_from_nand(items, count, backupDir);
                if (res > 0) {
                    printf("\nWii haz success! Deleted %d Mii(s) from NAND.\n", res);
                } else if (res == 0) {
                    showFailed("deleted");
                    return;
                } else {
                    printf("\nError deleting from NAND (%d): %s\n", res, nandErrorText(res));
                }
                waitForA();
                return;
            }
            VIDEO_WaitVSync();
        }
    }
}

// Explains that some Wii U/3DS Mii features may not look the same
bool showMiiExplanation() {
    printf(CLS);
    printf("==========================================\n");
    printf("                Hey there...                  \n");
    printf("==========================================\n\n");
    printf(" Converting a Wii U/3DS Mii to a Wii Mii\n");
    printf(" will cause some Mii characteristics to not\n");
    printf(" appear on the Wii Mii after converting/\n");
    printf(" installing. This is perfectly normal, \n");
    printf(" as some characteristics from the 3DS/Wii U\n");
    printf(" Mii editor don't actually exist in the\n");
    printf(" Wii's Mii editor.\n\n");
    printf(" So if you think your Mii doesn't look directly\n");
    printf(" comparable, you aren't going coo-coo crazy.\n\n");
    printf("===========================================\n");

    // haha better read this too
    for (int i = 5; i > 0; i--) {
        printf("\r You may proceed in %d ", i);
        fflush(stdout);
        sleep(1);
    }

    printf("\r Press [A] to Accept & Continue | [B] to Cancel\n");

    while (1) {
        u32 pressed = readButtons();

        if (pressed & WPAD_BUTTON_A) {
            return true; 
        }
        if (pressed & WPAD_BUTTON_B) {
            return false; 
        }
        VIDEO_WaitVSync();
    }
}

bool askInstallConverted() {
    int sel = 0;
    while (1) {
        printf(CLS);
        printf("==========================================\n");
        printf(" Wii haz success! Would you like to install\n");
        printf(" the converted Mii(s) to the nand?\n");
        printf("==========================================\n");
        printf(" [Up/Down]: Choose | [A]: Confirm\n");
        printf("------------------------------------------\n");
        printf(" %s Yes\n", (sel == 0) ? "[X]" : "[ ]");
        printf(" %s Nah\n", (sel == 1) ? "[X]" : "[ ]");
        while (1) {
            u32 pressed = readButtons();
            if (pressed & WPAD_BUTTON_UP) {
                sel = 0;
                break;
            }
            if (pressed & WPAD_BUTTON_DOWN) {
                sel = 1;
                break;
            }
            if (pressed & WPAD_BUTTON_A) return sel == 0;
            if (pressed & WPAD_BUTTON_B) return false;
            VIDEO_WaitVSync();
        }
    }
}

// Yay another checklist
void runConvertMenu(const char *directory, const char *device) {
    MiiItem items[MAX_MIIS];
    int count = loadFilesystemMiisCFL(directory, items, MAX_MIIS);
    if (count <= 0) {
        printf(CLS);
        printf("No .ffsd/.cfsd Miis found in %s.\nPress B to return.", directory);
        while (1) {
            if (readButtons() & WPAD_BUTTON_B) break;
            VIDEO_WaitVSync();
        }
        return;
    }
    int cursor = 0;

    while (1) {
        renderChecklist(items, count, cursor, "Convert 3DS/Wii U Miis", device, true);
        while (1) {
            u32 pressed = readButtons();

            if (pressed & WPAD_BUTTON_UP) {
                if (cursor > 0) cursor--;
                break;
            }
            if (pressed & WPAD_BUTTON_DOWN) {
                if (cursor < count - 1) cursor++;
                break;
            }
            if (pressed & WPAD_BUTTON_A) {
                items[cursor].selected = !items[cursor].selected;
                break;
            }
            if (pressed & WPAD_BUTTON_1) {
                toggleAll(items, count);
                break;
            }
            if (pressed & WPAD_BUTTON_B) {
                return;
            }
            if (pressed & WPAD_BUTTON_PLUS) {
                int format = askExportFormat("converted");
                if (format < 0) break;
                printf("\nConverting Miis...\n");
                int converted = 0;
                for (int i = 0; i < count; i++) {
                    if (!items[i].selected) continue;
                    char outPath[512];
                    char baseName[256];
                    strcpy(baseName, items[i].filename);
                    char *dot = strrchr(baseName, '.');
                    if (dot) *dot = '\0';
                    snprintf(outPath, sizeof(outPath), "%s/%s%s", directory, baseName, exportExts[format]);

                    FILE *f = fopen(outPath, "wb");
                    if (f) {
                        fwrite(items[i].rawData, 1, MII_SIZE, f);
                        fclose(f);
                        converted++;
                    }
                }
                if (converted > 0 && askInstallConverted()) {
                    printf("\nWii haz success! Converted %d Mii(s) to %s\n", converted, directory);
                    sleep(2);
                    if (showNandWarning()) {
                        MiiItem installItems[MAX_MIIS];
                        int installCount = loadFilesystemMiis(directory, installItems);
                        runChecklistMenu(installItems, installCount, true, directory, device);
                    } else {
                        printf("\nCancelled.\n");
                        sleep(1);
                    }
                    return;
                }
                if (converted == 0) {
                    showFailed("converted");
                    return;
                }
                printf("\nWii haz success! Converted %d Mii(s) to %s\n", converted, directory);
                waitForA();
                return; 
            }
            VIDEO_WaitVSync();
        }
    }
}