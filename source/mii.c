#include <string.h>
#include "grabmii.h"

/*  Ok so quick little note here: 
    The 3DS/Wii U Mii editor has way more characteristics to add/choose from than the Wii does.
    This means that the converted Mii will look similar, but most certainly not identical.
    Below is, in a nutshell, a conversion chart to try and make everything as similar as possible.
    Have fun */

typedef struct {
    const u8 *buf;
    int bitPos;
} BitReader;

static inline u32 br_bits(BitReader *r, int n) {
    u32 v = 0;
    for (int i = 0; i < n; i++) {
        int byteIdx = r->bitPos >> 3;
        int bitIdx = 7 - (r->bitPos & 7);
        v = (v << 1) | ((r->buf[byteIdx] >> bitIdx) & 1);
        r->bitPos++;
    }
    return v;
}
static inline void br_skip(BitReader *r, int n) { r->bitPos += n; }
typedef struct {
    u8 *buf;
    int bitPos;
} BitWriter;
static inline void bw_bits(BitWriter *w, u32 value, int n) {
    for (int i = n - 1; i >= 0; i--) {
        int byteIdx = w->bitPos >> 3;
        int bitIdx = 7 - (w->bitPos & 7);
        u32 bit = (value >> i) & 1;
        if (bit) w->buf[byteIdx] |= (u8)(1 << bitIdx);
        else     w->buf[byteIdx] &= (u8)~(1 << bitIdx);
        w->bitPos++;
    }
}
static inline int clampRange(int value, int maxInclusive) {
    if (value < 0) return 0;
    if (value > maxInclusive) return maxInclusive;
    return value;
}
static inline int clampRangeMinMax(int value, int minInclusive, int maxInclusive) {
    if (value < minInclusive) return minInclusive;
    if (value > maxInclusive) return maxInclusive;
    return value;
}

typedef struct {
    int color, gender, birthDay, birthMonth;
    u16 name[11];
    int size, fatness;
    int blushType, faceStyle, faceColor, faceType;
    bool hairMirrored;
    int hairColor, hairType;
    int eyeScale, eyeColor, eyeType, eyeHeight, eyeDistance, eyeRotation;
    int eyebrowScale, eyebrowColor, eyebrowType, eyebrowHeight, eyebrowDistance, eyebrowRotation;
    int noseHeight, noseScale, noseType;
    int mouthScale, mouthColor, mouthType, mouthHeight;
    int mustacheType, mustacheHeight, mustacheScale;
    int beardColor, beardType;
    int glassHeight, glassScale, glassColor, glassType;
    int moleYPos, moleXPos, moleScale;
    bool moleEnabled;
    u16 creatorName[11];
    u8 createId[18];
} FflMiiData;

static u16 cleanNameChar(u32 ch) {
    if (ch == 0) return 0;
    if (ch >= 0x20 && ch < 0x7F) return (u16)ch;
    if (ch >= 0xA1 && ch <= 0xFF) return (u16)ch;
    if (ch >= 0x3041 && ch <= 0x30FF) return (u16)ch;
    return '?';
}

static void fflPreSwapEndian(u8 buf[0x60]) {
    u8 t;
    t = buf[0]; buf[0] = buf[3]; buf[3] = t;
    t = buf[1]; buf[1] = buf[2]; buf[2] = t;
    for (int i = 0x18; i < 0x2E; i += 2) { t = buf[i]; buf[i] = buf[i + 1]; buf[i + 1] = t; }
    for (int i = 0x30; i < 0x48; i += 2) { t = buf[i]; buf[i] = buf[i + 1]; buf[i + 1] = t; }
    for (int i = 0x48; i < 0x5C; i += 2) { t = buf[i]; buf[i] = buf[i + 1]; buf[i + 1] = t; }
    t = buf[0x5C]; buf[0x5C] = buf[0x5D]; buf[0x5D] = t;
}

static bool parseFflStoreData(const u8 rawIn[96], FflMiiData *out) {
    if (calculate_crc16(rawIn, 96) != 0) {
        return false;
    }
    u8 buf[0x60];
    memcpy(buf, rawIn, 0x60);
    fflPreSwapEndian(buf);
    BitReader r = { buf, 0 };
    memset(out, 0, sizeof(*out));
    memcpy(out->createId, rawIn + 4, 18);
    br_skip(&r, 4 + 4 + 4 + 4 + 4 + 2 + 1 + 1);
    br_skip(&r, 8); 
    br_skip(&r, 8 * 8);
    br_skip(&r, 8 * 10);
    br_skip(&r, 16);
    br_skip(&r, 1);
    br_skip(&r, 1);
    out->color = (int)br_bits(&r, 4);
    out->birthDay = (int)br_bits(&r, 5);
    out->birthMonth = (int)br_bits(&r, 4);
    out->gender = (int)br_bits(&r, 1);
    for (int i = 0; i < 10; i++) {
        out->name[i] = cleanNameChar(br_bits(&r, 16));
    }

    out->name[10] = 0;
    out->size = (int)br_bits(&r, 8);
    out->fatness = (int)br_bits(&r, 8);
    out->blushType = (int)br_bits(&r, 4);
    out->faceStyle = (int)br_bits(&r, 4);
    out->faceColor = (int)br_bits(&r, 3);
    out->faceType = (int)br_bits(&r, 4);
    br_skip(&r, 1);
    out->hairMirrored = br_bits(&r, 5) != 0;
    out->hairColor = (int)br_bits(&r, 3);
    out->hairType = (int)br_bits(&r, 8);
    br_skip(&r, 3);
    out->eyeScale = (int)br_bits(&r, 4);
    out->eyeColor = (int)br_bits(&r, 3);
    out->eyeType = (int)br_bits(&r, 6);
    out->eyeHeight = (int)br_bits(&r, 7);
    out->eyeDistance = (int)br_bits(&r, 4);
    out->eyeRotation = (int)br_bits(&r, 5);
    br_skip(&r, 4); 
    out->eyebrowScale = (int)br_bits(&r, 4);
    out->eyebrowColor = (int)br_bits(&r, 3);
    out->eyebrowType = (int)br_bits(&r, 5);
    out->eyebrowHeight = (int)br_bits(&r, 7);
    out->eyebrowDistance = (int)br_bits(&r, 4);
    out->eyebrowRotation = (int)br_bits(&r, 5);
    out->noseHeight = (int)br_bits(&r, 7);
    out->noseScale = (int)br_bits(&r, 4);
    out->noseType = (int)br_bits(&r, 5);
    br_skip(&r, 3);
    out->mouthScale = (int)br_bits(&r, 4);
    out->mouthColor = (int)br_bits(&r, 3);
    out->mouthType = (int)br_bits(&r, 6);
    br_skip(&r, 8); 
    out->mustacheType = (int)br_bits(&r, 3);
    out->mouthHeight = (int)br_bits(&r, 5);
    out->mustacheHeight = (int)br_bits(&r, 6);
    out->mustacheScale = (int)br_bits(&r, 4);
    out->beardColor = (int)br_bits(&r, 3);
    out->beardType = (int)br_bits(&r, 3);
    out->glassHeight = (int)br_bits(&r, 5);
    out->glassScale = (int)br_bits(&r, 4);
    out->glassColor = (int)br_bits(&r, 3);
    out->glassType = (int)br_bits(&r, 4);
    br_skip(&r, 1);
    out->moleYPos = (int)br_bits(&r, 5);
    out->moleXPos = (int)br_bits(&r, 5);
    out->moleScale = (int)br_bits(&r, 4);
    out->moleEnabled = br_bits(&r, 1) != 0;
    for (int i = 0; i < 10; i++) {
        out->creatorName[i] = cleanNameChar(br_bits(&r, 16));
    }
    out->creatorName[10] = 0;
    return true;
}

void generateWiiMiiID(const u8 *seed, int len, u8 out[4]) {
    u32 hash = 2166136261u;
    for (int i = 0; i < len; i++) {
        hash ^= seed[i];
        hash *= 16777619u;
    }
    u32 idVal = (hash & 0x1FFFFFFF) | 0x80000000u;
    out[0] = (idVal >> 24) & 0xFF;
    out[1] = (idVal >> 16) & 0xFF;
    out[2] = (idVal >> 8) & 0xFF;
    out[3] = idVal & 0xFF;
}

static void buildRflFromFfl(const FflMiiData *m, u8 wiiOut[MII_SIZE]) {
    memset(wiiOut, 0, MII_SIZE);
    BitWriter w = { wiiOut, 0 };
    bw_bits(&w, 0, 1); 
    bw_bits(&w, m->gender & 1, 1);
    bw_bits(&w, (u32)clampRange(m->birthMonth, 12), 4);
    bw_bits(&w, (u32)clampRange(m->birthDay, 31), 5);
    bw_bits(&w, (u32)clampRange(m->color, 11), 4); 
    bw_bits(&w, 0, 1); 
    for (int i = 0; i < 10; i++) {
        bw_bits(&w, m->name[i], 16);
    }

    bw_bits(&w, (u32)clampRange(m->size, 127), 8);
    bw_bits(&w, (u32)clampRange(m->fatness, 127), 8); 
    u8 miiID[4];
    generateWiiMiiID(m->createId, 18, miiID);
    for (int i = 0; i < 4; i++) bw_bits(&w, miiID[i], 8);
    for (int i = 0; i < 4; i++) bw_bits(&w, 0, 8);
    int facialFeature = m->faceStyle != 0 ? clampRange(m->faceStyle, 11)
    : clampRange(m->blushType, 11);
    bw_bits(&w, (u32)clampRange(m->faceType, 7), 3);
    bw_bits(&w, (u32)clampRange(m->faceColor, 5), 3);
    bw_bits(&w, (u32)facialFeature, 4);
    bw_bits(&w, 0, 3);
    bw_bits(&w, 0, 1); 
    bw_bits(&w, 0, 1);
    bw_bits(&w, 0, 1); 
    bw_bits(&w, (u32)clampRange(m->hairType, 71), 7);
    bw_bits(&w, (u32)clampRange(m->hairColor, 7), 3);
    bw_bits(&w, m->hairMirrored ? 1u : 0u, 1);
    bw_bits(&w, 0, 5);
    bw_bits(&w, (u32)clampRange(m->eyebrowType, 23), 5);
    bw_bits(&w, 0, 1);
    bw_bits(&w, (u32)clampRange(m->eyebrowRotation, 11), 4);
    bw_bits(&w, 0, 6);
    bw_bits(&w, (u32)clampRange(m->eyebrowColor, 7), 3);
    bw_bits(&w, (u32)clampRange(m->eyebrowScale, 8), 4);
    bw_bits(&w, (u32)clampRangeMinMax(m->eyebrowHeight, 3, 18), 5);
    bw_bits(&w, (u32)clampRange(m->eyebrowDistance, 12), 4);
    bw_bits(&w, (u32)clampRange(m->eyeType, 47), 6);
    bw_bits(&w, 0, 2);
    bw_bits(&w, (u32)clampRange(m->eyeRotation, 7), 3);
    bw_bits(&w, (u32)clampRange(m->eyeHeight, 18), 5);
    bw_bits(&w, (u32)clampRange(m->eyeColor, 5), 3);
    bw_bits(&w, 0, 1);
    bw_bits(&w, (u32)clampRange(m->eyeScale, 7), 3);
    bw_bits(&w, (u32)clampRange(m->eyeDistance, 12), 4);
    bw_bits(&w, 0, 5);
    bw_bits(&w, (u32)clampRange(m->noseType, 11), 4);
    bw_bits(&w, (u32)clampRange(m->noseScale, 8), 4);
    bw_bits(&w, (u32)clampRange(m->noseHeight, 18), 5);
    bw_bits(&w, 0, 3);
    bw_bits(&w, (u32)clampRange(m->mouthType, 23), 5);
    bw_bits(&w, (u32)clampRange(m->mouthColor, 2), 2);
    bw_bits(&w, (u32)clampRange(m->mouthScale, 8), 4);
    bw_bits(&w, (u32)clampRange(m->mouthHeight, 18), 5);
    bw_bits(&w, (u32)clampRange(m->glassType, 8), 4);
    bw_bits(&w, (u32)clampRange(m->glassColor, 5), 3);
    bw_bits(&w, 0, 1);
    bw_bits(&w, (u32)clampRange(m->glassScale, 7), 3);
    bw_bits(&w, (u32)clampRange(m->glassHeight, 20), 5);
    bw_bits(&w, (u32)clampRange(m->mustacheType, 3), 2);
    bw_bits(&w, (u32)clampRange(m->beardType, 3), 2);
    bw_bits(&w, (u32)clampRange(m->beardColor, 7), 3);
    bw_bits(&w, (u32)clampRange(m->mustacheScale, 8), 4);
    bw_bits(&w, (u32)clampRange(m->mustacheHeight, 16), 5);
    bw_bits(&w, m->moleEnabled ? 1u : 0u, 1);
    bw_bits(&w, (u32)clampRange(m->moleScale, 8), 4);
    bw_bits(&w, (u32)clampRange(m->moleYPos, 30), 5);
    bw_bits(&w, (u32)clampRange(m->moleXPos, 16), 5);
    bw_bits(&w, 0, 1);
    for (int i = 0; i < 10; i++) {
        bw_bits(&w, m->creatorName[i], 16);
    }
}

bool convertFflStoreData(const u8 raw[96], u8 wiiOut[MII_SIZE]) {
    FflMiiData ffl;
    if (!parseFflStoreData(raw, &ffl)) return false;
    buildRflFromFfl(&ffl, wiiOut);
    return true;
}