// iOS/macOS implementation of the text platform seam (text_platform.h),
// built on Core Text. Shaping via CTLine (which also does font fallback,
// bidi and complex scripts), rasterization via CTFontDrawGlyphs.
// Pure CoreFoundation API, so this file works with or without ARC.

#include "text_platform.h"

#import <CoreText/CoreText.h>
#import <CoreGraphics/CoreGraphics.h>
#include <vector>
#include <math.h>
#include <stdlib.h>
#include <string.h>

// Interned CTFontRefs; the index is the font_id handed to the portable
// layer. Fallback runs come back with distinct CTFontRefs, and the font
// size is part of the font's identity, so (font_id) alone keys a glyph
// bitmap. Fonts are retained for the lifetime of the process (bounded by
// the number of distinct fallback fonts x sizes actually used).
static std::vector<CTFontRef> g_fonts;

static uint32_t intern_font(CTFontRef font) {
    for (uint32_t i = 0; i < (uint32_t)g_fonts.size(); i++) {
        if (CFEqual(g_fonts[i], font)) {
            return i;
        }
    }
    CFRetain(font);
    g_fonts.push_back(font);
    return (uint32_t)(g_fonts.size() - 1);
}

int txt_shape(const char* utf8, float px_size, txt_glyph* out, int max,
              txt_line_metrics* lm) {
    lm->width = 0.0f;
    lm->ascent = 0.0f;
    lm->descent = 0.0f;
    if (!utf8 || !utf8[0] || max <= 0) {
        return 0;
    }
    CFStringRef str = CFStringCreateWithCString(NULL, utf8, kCFStringEncodingUTF8);
    if (!str) {
        return 0;
    }
    CTFontRef font = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, px_size, NULL);
    CFStringRef keys[] = { kCTFontAttributeName };
    CFTypeRef vals[] = { font };
    CFDictionaryRef attrs = CFDictionaryCreate(NULL, (const void**)keys, (const void**)vals, 1,
                                               &kCFTypeDictionaryKeyCallBacks,
                                               &kCFTypeDictionaryValueCallBacks);
    CFAttributedStringRef astr = CFAttributedStringCreate(NULL, str, attrs);
    CTLineRef line = CTLineCreateWithAttributedString(astr);

    CGFloat ascent = 0, descent = 0, leading = 0;
    lm->width = (float)CTLineGetTypographicBounds(line, &ascent, &descent, &leading);
    lm->ascent = (float)ascent;
    lm->descent = (float)descent;

    int n = 0;
    CFArrayRef runs = CTLineGetGlyphRuns(line);
    for (CFIndex r = 0; r < CFArrayGetCount(runs) && n < max; r++) {
        CTRunRef run = (CTRunRef)CFArrayGetValueAtIndex(runs, r);
        CTFontRef run_font = (CTFontRef)CFDictionaryGetValue(CTRunGetAttributes(run),
                                                             kCTFontAttributeName);
        uint32_t fid = intern_font(run_font ? run_font : font);
        CFIndex count = CTRunGetGlyphCount(run);
        if (count > (CFIndex)(max - n)) {
            count = (CFIndex)(max - n);
        }
        std::vector<CGGlyph> glyphs((size_t)count);
        std::vector<CGPoint> positions((size_t)count);
        CTRunGetGlyphs(run, CFRangeMake(0, count), glyphs.data());
        CTRunGetPositions(run, CFRangeMake(0, count), positions.data());
        for (CFIndex i = 0; i < count; i++) {
            out[n].font_id = fid;
            out[n].glyph_id = glyphs[(size_t)i];
            out[n].x = (float)positions[(size_t)i].x;
            out[n].y = -(float)positions[(size_t)i].y;  // Core Text is y-up
            n++;
        }
    }

    CFRelease(line);
    CFRelease(astr);
    CFRelease(attrs);
    CFRelease(font);
    CFRelease(str);
    return n;
}

bool txt_rasterize(uint32_t font_id, uint32_t glyph_id, txt_bitmap* out) {
    memset(out, 0, sizeof(*out));
    if (font_id >= (uint32_t)g_fonts.size()) {
        return false;
    }
    CTFontRef font = g_fonts[font_id];
    CGGlyph glyph = (CGGlyph)glyph_id;

    CGRect bounds = CTFontGetBoundingRectsForGlyphs(font, kCTFontOrientationDefault,
                                                    &glyph, NULL, 1);
    if (CGRectIsNull(bounds) || bounds.size.width <= 0 || bounds.size.height <= 0) {
        return true;  // whitespace or other invisible glyph
    }

    const int pad = 1;  // margin so linear filtering doesn't bleed neighbors
    int w = (int)ceil(bounds.size.width) + 2 * pad;
    int h = (int)ceil(bounds.size.height) + 2 * pad;

    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(NULL, (size_t)w, (size_t)h, 8, (size_t)w * 4, cs,
                                             kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(cs);
    if (!ctx) {
        return false;
    }

    // Monochrome glyphs are drawn white and tinted by the renderer; color
    // fonts (emoji) ignore the fill color and draw their own colors.
    CGContextSetRGBFillColor(ctx, 1, 1, 1, 1);
    CGPoint at = CGPointMake((CGFloat)pad - bounds.origin.x, (CGFloat)pad - bounds.origin.y);
    CTFontDrawGlyphs(font, &glyph, &at, 1, ctx);

    // CGBitmapContext memory is already top-row-first, matching txt_bitmap
    out->pixels = (uint8_t*)malloc((size_t)w * (size_t)h * 4);
    const uint8_t* src = (const uint8_t*)CGBitmapContextGetData(ctx);
    size_t stride = CGBitmapContextGetBytesPerRow(ctx);
    for (int row = 0; row < h; row++) {
        memcpy(out->pixels + (size_t)row * (size_t)w * 4, src + (size_t)row * stride, (size_t)w * 4);
    }
    CGContextRelease(ctx);

    out->w = w;
    out->h = h;
    out->offset_x = (float)(bounds.origin.x - pad);
    out->offset_y = -(float)(bounds.origin.y + bounds.size.height + pad);
    out->is_color = (CTFontGetSymbolicTraits(font) & kCTFontTraitColorGlyphs) != 0;
    return true;
}

void txt_bitmap_free(txt_bitmap* bm) {
    free(bm->pixels);
    bm->pixels = NULL;
}
