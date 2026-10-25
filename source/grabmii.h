#include <gccore.h>

#ifndef GRABMII_H
#define GRABMII_H
#define GRABMII_VERSION "2.0"
#define MII_DB_PATH "/shared2/menu/FaceLib/RFL_DB.dat"
#define MII_SIZE 74
#define MAX_MIIS 100
#define DB_HEADER_SIZE 4
#define DB_TOTAL_SIZE 0x1f1e0 
#define MII_OFF_NAME 0x02 
#define MII_OFF_MIIID 0x18 
#define MII_OFF_SYSTEMID 0x1C 

typedef struct {
    char filename[64];
    char nickname[32];
    u8 rawData[MII_SIZE];
    int slotIndex; 
    bool selected;
} MiiItem;

#define CLS "\x1b[2J"
#define THE_BG "\x1b[48;5;235m"
#define WHITE_TEXT "\x1b[38;5;255m"

extern bool backupRflDb;

bool convertFflStoreData(const u8 raw[96], u8 wiiOut[MII_SIZE]);
void generateWiiMiiID(const u8 *seed, int len, u8 out[4]);
u16 calculate_crc16(const u8 *buf, u32 len);
u32 readButtons();
void init_video();
bool isDeviceReady(const char *devicePath);
bool showNandWarning();
bool showMiiExplanation();
void showNandReadError(s32 code);
int loadFilesystemMiis(const char *directory, MiiItem *items);
int loadNandMiis(MiiItem *items, u8 *dbBufferOut);
void runChecklistMenu(MiiItem *items, int count, bool isInstalling, const char *backupDir, const char *device);
void runConvertMenu(const char *directory, const char *device);
void runDeleteMenu(const char *backupDir, const char *device);

#endif