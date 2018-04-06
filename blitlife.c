#include "blitter.h"
#include <string.h>
#include <stdio.h>
#include <osbind.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <gem.h>

// repeats two 2-bit numbers in the upper or lower byte of a word
#define R2(x,y) (\
    (((uint8_t)(x)) * 0x0500U) | \
    (((uint8_t)(y)) * 0x0005U) )

// Map for counting the three pixels above/below a pair of pixels
// counts 4 pixels into a two overlapping groups of 3.
// The leftmost count is put into the upper byte of the destination word,
// the lower count into the lower byte.
static const uint16_t pixel_count[] = {
    R2(0,0), // 0000: 000 000
    R2(0,1), // 0001: 000 001
    R2(1,1), // 0010: 001 010
    R2(1,2), // 0011: 001 011
    R2(1,1), // 0100: 010 100
    R2(1,2), // 0101: 010 101
    R2(2,2), // 0110: 011 110
    R2(2,3), // 0111: 011 111
    R2(1,0), // 1000: 100 000
    R2(1,1), // 1001: 100 001
    R2(2,1), // 1010: 101 010
    R2(2,2), // 1011: 101 011
    R2(2,1), // 1100: 110 100
    R2(2,2), // 1101: 110 101
    R2(3,2), // 1110: 111 110
    R2(3,3), // 1111: 111 111
};

// Version of the count table for the leftmost two pixels on each line
static const uint16_t pixel_count_lc[] = {
    R2(0,0), // 0000: X00 000
    R2(0,0), // 0001: X00 000
    R2(0,1), // 0010: X00 001
    R2(0,1), // 0011: X00 001
    R2(1,1), // 0100: X01 010
    R2(1,1), // 0101: X01 010
    R2(1,2), // 0110: X01 011
    R2(1,2), // 0111: X01 011
    R2(1,1), // 1000: X10 100
    R2(1,1), // 1001: X10 100
    R2(1,2), // 1010: X10 101
    R2(1,2), // 1011: X10 101
    R2(2,2), // 1100: X11 110
    R2(2,2), // 1101: X11 110
    R2(2,3), // 1110: X11 111
    R2(2,3), // 1111: X11 111
};

// Ditto for the right column, nees zero shift
static const uint16_t pixel_count_rc[] = {
    R2(0,0), // 0000: 000 00X
    R2(0,1), // 0001: 000 01X
    R2(1,1), // 0010: 010 10X
    R2(2,2), // 0011: 011 11X
    R2(1,0), // 0100: 100 00X
    R2(2,1), // 0101: 101 01X
    R2(2,1), // 0110: 110 10X
    R2(3,2), // 0111: 111 11X
    R2(0,0), // 1000: 000 00X
    R2(0,1), // 1001: 000 01X
    R2(1,1), // 1010: 010 10X
    R2(2,2), // 1011: 011 11X
    R2(1,0), // 1100: 100 00X
    R2(2,1), // 1101: 101 01X
    R2(2,1), // 1110: 110 10X
    R2(3,2), // 1111: 111 11X
};

// Sums upper and lower two bits in a nibble resulting in a 3 bit sum shifted
// 2 bits to the left

static const uint16_t threebit_sum[] = {
    (0+0)*0x0404,
    (0+1)*0x0404,
    (0+2)*0x0404,
    (0+3)*0x0404,
    (1+0)*0x0404,
    (1+1)*0x0404,
    (1+2)*0x0404,
    (1+3)*0x0404,
    (2+0)*0x0404,
    (2+1)*0x0404,
    (2+2)*0x0404,
    (2+3)*0x0404,
    (3+0)*0x0404,
    (3+1)*0x0404,
    (3+2)*0x0404,
    (3+3)*0x0404,
};

#define C2(c1,p1,p2,c2) (\
    ((p1&1) * 0x2000) | \
    ((c1&3) * 0x0100) | \
    ((p2&1) * 0x0020) | \
    ((c2&3) * 0x0001))

static const uint16_t pixel_count_current_row[] = {
                 // patt: ln l r rn
    C2(0,0,0,0), // 0000: 00 0 0 00
    C2(0,0,0,1), // 0001: 00 0 0 01
    C2(1,0,1,0), // 0010: 01 0 1 00
    C2(1,0,1,1), // 0011: 01 0 1 01
    C2(0,1,0,1), // 0100: 00 1 0 10
    C2(0,1,0,2), // 0101: 00 1 0 11
    C2(1,1,1,1), // 0110: 01 1 1 10
    C2(1,1,1,2), // 0111: 01 1 1 11
    C2(1,0,0,0), // 1000: 10 0 0 00
    C2(1,0,0,1), // 1001: 10 0 0 01
    C2(2,0,1,0), // 1010: 11 0 1 00
    C2(2,0,1,1), // 1011: 11 0 1 01
    C2(1,1,0,1), // 1100: 10 1 0 10
    C2(1,1,0,2), // 1101: 10 1 0 11
    C2(2,1,1,1), // 1110: 11 1 1 10
    C2(2,1,1,2), // 1111: 11 1 1 11

};

// Separate table for the first column.
static const uint16_t pixel_count_current_row_fix_l[]= {
                 // patt: ln l r rn
    C2(0,0,0,0), // 000: 0 0 0 00
    C2(0,0,0,1), // 001: 0 0 0 01
    C2(1,0,1,0), // 010: 1 0 1 00
    C2(1,0,1,1), // 011: 1 0 1 01
    C2(0,1,0,1), // 100: 0 1 0 10
    C2(0,1,0,2), // 101: 0 1 0 11
    C2(1,1,1,1), // 110: 1 1 1 10
    C2(1,1,1,2), // 111: 1 1 1 11
    C2(0,0,0,0), // 000: 0 0 0 00
    C2(0,0,0,1), // 001: 0 0 0 01
    C2(1,0,1,0), // 010: 1 0 1 00
    C2(1,0,1,1), // 011: 1 0 1 01
    C2(0,1,0,1), // 100: 0 1 0 10
    C2(0,1,0,2), // 101: 0 1 0 11
    C2(1,1,1,1), // 110: 1 1 1 10
    C2(1,1,1,2), // 111: 1 1 1 11
};

// Another for the rightmost column this time right pixel has only one neighbor
static const uint16_t pixel_count_current_row_fix_r[] = {
                  // patt: ln l r rn
     C2(0,0,0,0), // 000: 00 0 0 0
     C2(1,0,1,0), // 001: 01 0 1 0
     C2(0,1,0,1), // 010: 00 1 0 1
     C2(1,1,1,1), // 011: 01 1 1 1
     C2(1,0,0,0), // 100: 10 0 0 0
     C2(2,0,1,0), // 101: 11 0 1 0
     C2(1,1,0,1), // 110: 10 1 0 1
     C2(2,1,1,1), // 111: 11 1 1 1
     C2(0,0,0,0), // 000: 00 0 0 0
     C2(1,0,1,0), // 001: 01 0 1 0
     C2(0,1,0,1), // 010: 00 1 0 1
     C2(1,1,1,1), // 011: 01 1 1 1
     C2(1,0,0,0), // 100: 10 0 0 0
     C2(2,0,1,0), // 101: 11 0 1 0
     C2(1,1,0,1), // 110: 10 1 0 1
     C2(2,1,1,1), // 111: 11 1 1 1
};

// Encode the rules for game of life.
// The four source bits are taken to represent the current pixel in the
// most significant bit and a 3 bit count of surrounding pixels in the lower.
static const uint16_t game_of_life_rules[] = {
            // patt: cp count
    0x0000, // 0000:  0 0  -> 0
    0x0000, // 0001:  0 1  -> 0
    0x0000, // 0010:  0 2  -> 0
    0xFFFF, // 0011:  0 3  -> 1
    0x0000, // 0100:  0 4  -> 0
    0x0000, // 0101:  0 5  -> 0
    0x0000, // 0110:  0 6  -> 0
    0x0000, // 0111:  0 7  -> 0
    0x0000, // 1000:  1 0  -> 0
    0x0000, // 1001:  1 1  -> 0
    0xFFFF, // 1010:  1 2  -> 1
    0xFFFF, // 1011:  1 3  -> 1
    0x0000, // 1100:  1 4  -> 0
    0x0000, // 1101:  1 5  -> 0
    0x0000, // 1110:  1 6  -> 0
    0x0000, // 1111:  1 7  -> 0
};

#if 0
// Debug ruleset that instead of playing game of life implements a
// really rouandabout way of inversing the bitmap :)
static const uint16_t game_of_inverse_video[] = {
    // patt: cp count
    0xFFFF, // 0000:  0 0  -> 0
    0xFFFF, // 0001:  0 1  -> 0
    0xFFFF, // 0010:  0 2  -> 0
    0xFFFF, // 0011:  0 3  -> 1
    0xFFFF, // 0100:  0 4  -> 0
    0xFFFF, // 0101:  0 5  -> 0
    0xFFFF, // 0110:  0 6  -> 0
    0xFFFF, // 0111:  0 7  -> 0
    0x0000, // 1000:  1 0  -> 0
    0x0000, // 1001:  1 1  -> 0
    0x0000, // 1010:  1 2  -> 1
    0x0000, // 1011:  1 3  -> 1
    0x0000, // 1100:  1 4  -> 0
    0x0000, // 1101:  1 5  -> 0
    0x0000, // 1110:  1 6  -> 0
    0x0000, // 1111:  1 7  -> 0
};

#endif

static volatile blitter_t* BLiTTER = (blitter_t*)(BLITTER_BASE);
static inline void run(uint16_t* dest, const uint16_t* src, uint16_t xcount, uint16_t ycount)
{
    BLiTTER->src = src;
    BLiTTER->dst = dest;
    BLiTTER->x_count = xcount;
    BLiTTER->y_count = ycount;

    // Inline assembly to run and poll the blitter in non-hog mode,
    // repeatedly granting it bus access instead of waiting for 64 cycles.
    // This will enable handing of interrupts during long blitter transactions.
    asm  (
        "   or.b #0x80, 0xFFFF8A3C.w\n"                // start the BLiTTER
        // loop: (note jump target is calculated manually due to how asm sections make it hard to inluce local labels)
        "   bset.b  #7, 0xFFFF8A3C.w\n  "              // Restart and test
        "   nop\n"                                     // Give time for blitter to gain control of the bus
        "   dc.w 0x66f6\n" // bnes.s loop  (f6 == -10) // Loop if registers shows "busy"
    );
}

static inline void setHalftone (const uint16_t* source)
{
    //memcpy((uint16_t*)(BLiTTER->halftone), source, 32);
    asm ("movem.l %0@, %%d3-%%d7/%%a2-%%a4; "
          "movem.l %%d3-%%d7/%%a2-%%a4, 0xFFFF8A00.w;"
        :
        : "a" (source)
        : "a2","a3","a4","d3",
          "d4","d5","d6","d7"
    );
}

// Use the blitter to clear the buffer
static void clear_buffer (uint16_t* dest, int width, int height)
{
    BLiTTER->endmask[2]=BLiTTER->endmask[1]=BLiTTER->endmask[0]=-1;
    BLiTTER->src_Xinc = 2;
    BLiTTER->src_Yinc = 2;
    BLiTTER->dst_Xinc = 2;
    BLiTTER->dst_Yinc = 2;
    BLiTTER->hop = HOP_SRC;
    BLiTTER->op = OP_ZERO;
    BLiTTER->skew = 0;
    BLiTTER->control =  0;
    run(dest, NULL, width, height);
}

// Process the entire screen using the table to map 4 pixels into a bit pattern
// fix_left and fix_right are used to fix the left and right column
static void process_pixels(uint16_t* dest, const uint16_t* src,
    const uint16_t* table, const uint16_t* fix_left, const uint16_t* fix_right,
    uint16_t mask, int words, int lines
)
{
    // 0002222444466660
    // 7111133335555777
    // 0011223344556677
    // 0(8) : Shift 13,
    // 1    : Shift 11,
    // 2    : Shift 9,
    // 3    : Shift 7,
    // 4    : Shift 5,
    // 5    : Shift 3,
    // 6    : Shift 1,
    // 7    : Shift 15, FXSR

    setHalftone(table);
    BLiTTER->src_Xinc = 2;
    BLiTTER->src_Yinc = 2;
    BLiTTER->dst_Xinc = 16;
    BLiTTER->dst_Yinc = 16;
    BLiTTER->hop = HOP_HALFTONE ;
    BLiTTER->op = OP_SRC;
    BLiTTER->control = CTRL_SMUDGE;

    BLiTTER->endmask[2]=BLiTTER->endmask[1]=BLiTTER->endmask[0]=mask;

    // Macro for unrolling a single blitter pass
    #define PASS(s,i) \
        BLiTTER->skew = (uint8_t)(s); \
        run(&dest[(i)], src, words, lines);

    // Pixel pairs 0 through 6
    PASS(                        SKEW(13), 0)
    PASS(                        SKEW(11), 1)
    PASS(                        SKEW( 9), 2)
    PASS(                        SKEW( 7), 3)
    PASS(                        SKEW( 5), 4)
    PASS(                        SKEW( 3), 5)
    PASS(                        SKEW( 1), 6)
    // Pixel pair 7 needs FXSR
    PASS(SKEW_FXSR | SKEW_NFSR | SKEW(15), 7)

    #undef PASS

    BLiTTER->src_Yinc = words*2;
    BLiTTER->dst_Yinc = words*16;

    // Fix left column (pass in nullptr if not required)
    if(fix_left)
    {
        setHalftone(fix_left);
        BLiTTER->skew = SKEW(13);
        run(dest,src,1,lines);
    }

    // Fix right column if needed
    if (fix_right)
    {
        setHalftone(fix_right);
        BLiTTER->skew = SKEW(0);
        run(&dest[words*8-1],&src[words-1],1,lines);
    }
}

static void process_buffer(uint16_t* buffer, const uint16_t* halftone,
    uint16_t mask1, uint16_t mask2, int extra_skew, int width, int height)
{
    int half_width = width/2;
    setHalftone(halftone);
    BLiTTER->hop = HOP_HALFTONE ;
    BLiTTER->op = OP_SRC;
    BLiTTER->control = CTRL_SMUDGE;
    BLiTTER->src_Xinc = BLiTTER->dst_Xinc = 2;
    BLiTTER->src_Yinc = BLiTTER->dst_Yinc = 2;

    // proces first byte
    BLiTTER->skew = SKEW(8+extra_skew);
    BLiTTER->endmask[2]=BLiTTER->endmask[1]=BLiTTER->endmask[0]=mask1;
    run(buffer,buffer,half_width,height);
    // process second byte
    BLiTTER->skew = SKEW(extra_skew);
    BLiTTER->endmask[2]=BLiTTER->endmask[1]=BLiTTER->endmask[0]=mask2;
    run(buffer,buffer,half_width,height);
}


static void sum_nibbles(uint16_t* buffer, uint16_t mask, int width, int height)
{
    process_buffer(buffer, threebit_sum, (mask&0xFF00), (mask&0x00FF), 0, width, height);
}

static void apply_rules(uint16_t* buffer, int width, int height)
{
    process_buffer(buffer, game_of_life_rules, 0xFF00, 0x00FF, 2, width, height);
}

static void copy_back(const uint16_t* buffer, uint16_t* dest, int words, int lines)
{
    BLiTTER->hop = HOP_SRC ;
    BLiTTER->op = OP_SRC;
    BLiTTER->control = 0;
    BLiTTER->src_Xinc = 16;
    BLiTTER->src_Yinc = 16;
    BLiTTER->dst_Xinc = 2;
    BLiTTER->dst_Yinc = 2;

    // Copy 2 bits at a time
    #define PASS(s,m,n) \
        BLiTTER->skew = (uint16_t)(s); \
        BLiTTER->endmask[0]=BLiTTER->endmask[1]=BLiTTER->endmask[2] = (uint16_t)(m); \
        run(dest, &buffer[(n)], words, lines);

    PASS(SKEW_FXSR | SKEW_NFSR |  9, 0xFFFF, 0)
    PASS(SKEW_FXSR | SKEW_NFSR | 11, 0x3000, 1)
    PASS(SKEW_FXSR | SKEW_NFSR | 13, 0x0C00, 2)
    PASS(SKEW_FXSR | SKEW_NFSR | 15, 0x0300, 3)
    PASS(                         1, 0x00FF, 4)
    PASS(                         3, 0x0030, 5)
    PASS(                         5, 0x000C, 6)
    PASS(                         7, 0x0003, 7)

    #undef PASS
}

static uint16_t* buffer = NULL;
static size_t buffer_size = 0;

void blitlife_init(int16_t max_w, int16_t max_h)
{
    size_t new_buffer_size = max_w * max_h;
    if (buffer != NULL)
    {
        if ( new_buffer_size == buffer_size)
            return;

        free(buffer);
    }
    buffer_size = new_buffer_size;
    buffer = malloc(new_buffer_size);
}

static void supexec_blitlife(void);

static MFDB* current_image;
void blitlife(MFDB* image)
{
    if(!buffer || buffer_size < image->fd_w * image->fd_h)
    {
        blitlife_init(image->fd_w, image->fd_h);
    }
    // This is by definition very un-rentrant and single-TOS like
    current_image = image;
    Supexec(supexec_blitlife);

}

// Since we are using the blitter hardware directly, the work horse needs to run
// in supervisor mode.
static void supexec_blitlife(void)
{
    uint16_t* screen = Physbase();
    int16_t width = current_image->fd_w;
    int16_t height = current_image->fd_h;
    int16_t scanline_words = current_image->fd_wdwidth;
    int16_t half_width = width/2;

    uint16_t* image_data = (uint16_t*)current_image->fd_addr;

    // Clear the first line in the buffer (the rest gets initilized in the first call to process_pixels)
    clear_buffer(buffer, width, 1);

    // Count pixels in the row above each pixel:
    process_pixels(buffer+half_width,image_data,
         pixel_count,
         pixel_count_lc,
         pixel_count_rc,
         0xFFFF, // No masking as this is the first write to the buffer (saves 2 cycles per word)
         scanline_words,
         height - 1 // The last line is not above any pixels
    );

    // Now count the pixels below each pixel
    process_pixels(buffer, image_data + scanline_words,
         pixel_count,
         pixel_count_lc,
         pixel_count_rc,
         0x0c0c, // place the pixel count in next 2 bits of each byte
         scanline_words,
         height - 1  // -1 because we skip the first row, which is not beneath any pixels
    );

    // Sum up the counts
    sum_nibbles(buffer, 0xFFFF, width, height);

    // Now insert information about the current row:
    process_pixels(buffer, image_data,
         pixel_count_current_row,
         pixel_count_current_row_fix_l,
         pixel_count_current_row_fix_r,
         // Preserve the 3 bits in each destination byte containing the count
         // of pixels in the above and below rows
         0x2323,
         scanline_words,
         height // There is no line offset so process all lines
    );

    // Now we have three bit count and a two bit count to sum up
    // let's cheat and only sum the lower 4 bits (this will work due to how
    // the rules work)
    sum_nibbles(buffer, 0x0F0F, width, height);


    // Last step before copying the pixels back. Based on the calculated
    // counts and the current pixel value, determine whether a pixel lives
    // or dies.
    apply_rules(buffer, width, height);

    // Now, teh bytes of the buffer represent  the pixels to copy back to the
    // screen. This final step can happen without any mapping through the halftone map:
    copy_back(buffer, image_data, scanline_words, height);
}
