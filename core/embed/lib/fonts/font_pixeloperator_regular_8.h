#include <stdint.h>

#if CERBERUS_FONT_BPP != 1
#error Wrong CERBERUS_FONT_BPP (expected 1)
#endif
#define Font_PixelOperator_Regular_8_HEIGHT 8
#define Font_PixelOperator_Regular_8_MAX_HEIGHT 8
#define Font_PixelOperator_Regular_8_BASELINE 1
extern const uint8_t* const Font_PixelOperator_Regular_8[126 + 1 - 32];
extern const uint8_t Font_PixelOperator_Regular_8_glyph_nonprintable[];
