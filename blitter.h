#pragma once
#include <stdint.h>

#define BLITTER_BASE         0xFFFF8A00

#define HOP_ONE              0
#define HOP_HALFTONE         1
#define HOP_SRC              2
#define HOP_SRC_AND_HALFTONE (HOP_HALFTONE | HOP_SRC)


// Note one can also suffice with OP_SRC, OP_DST, OP_ZERO and logical operators

// In reality the formula used by the blitter is like the following, where bN is
// the Nth bit in the op flag, and src and dst is the current word being blitted:
// b1& src & dst | b2 & src & ~dst | b3 & ~src & dst | b4 & ~src & ~dst
// (The blitter will optimize reads to skip either src or dest if op, hop, and/or
// smudge make them irrelevant)

#define OP_ZERO              0
#define OP_SRC_AND_DST       1  // ==  OP_SRC &  OP_DST
#define OP_SRC_AND_NDST      2  // ==  OP_SRC & ~OP_DST
#define OP_SRC               3
#define OP_NSRC_AND_DST      4  // == ~OP_SRC &  OP_DST
#define OP_DST               5
#define OP_SRC_XOR_DST       6  // ==  OP_SRC ^  OP_DST
#define OP_SRC_OR_DST        7  // ==  OP_SRC |  OP_DST
#define OP_NSRC_AND_NDST     8  // == ~OP_SRC & ~OP_DST
#define OP_NSRC_XOR_DST      9  // == ~OP_SRC ^  OP_DST
#define OP_NDST             10  // ==  OP_ONE ^  OP_DST =~ ~OP_DST (blitter ignores the upper nibble)
#define OP_SRC_OR_NDST      11  // =~  OP_SRC | ~OP_DST
#define OP_NSRC             12  // =~ ~OP_SRC
#define OP_NSRC_OR_DST      13  // =~  OP_SRC |  OP_DST
#define OP_NSRC_OR_NDST     14  // =~  OP_SRC | ~OP_DST
#define OP_ONE              15  // =~ ~OP_ZERO



#define CTRL_LINENO(x)      ((x) & 0xF)
#define CTRL_SMUDGE         0x20
#define CTRL_HOG            0x40
#define CTRL_BUSY           0x80

#define SKEW(x)             ((x) & 0xF)
#define SKEW_NFSR           0x40
#define SKEW_FXSR           0x80

typedef struct blitter_regs {
    uint16_t   halftone[16];
    int16_t    src_Xinc;
    int16_t    src_Yinc;
    const uint16_t*  src;
    uint16_t   endmask[3];
    int16_t    dst_Xinc;
    int16_t    dst_Yinc;
    uint16_t*  dst;
    uint16_t   x_count;
    uint16_t   y_count;
    uint8_t    hop;
    uint8_t    op;
    uint8_t    control;
    uint8_t    skew;
} __attribute__((aligned(2),packed))
blitter_t;
