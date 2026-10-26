#include "platform/text_raster.h"
#import <UIKit/UIKit.h>
bool text_raster_init(void) { return true; }
void text_raster_shutdown(void) {}
bool text_raster_render(const char *text, float pixels, int width, int height, text_bitmap *out) {
  memset(out, 0, sizeof(*out));
  @autoreleasepool {
    NSString *string = [[NSString alloc] initWithBytes:text
                                                length:strlen(text)
                                              encoding:NSUTF8StringEncoding];
    if (!string)
      return false;
    NSMutableParagraphStyle *paragraph = [NSMutableParagraphStyle new];
    paragraph.lineBreakMode = height > 0 ? NSLineBreakByWordWrapping : NSLineBreakByTruncatingTail;
    /* UIKit chooses optical font metrics in points. Making a font at the pixel
     * size changes its glyph widths; drawing it smaller later cannot undo that. */
    CGFloat raster_scale = height > 0 ? UIScreen.mainScreen.scale : 1;
    NSDictionary *attrs = @{
      NSFontAttributeName : [UIFont systemFontOfSize:pixels / raster_scale],
      NSForegroundColorAttributeName : UIColor.whiteColor,
      NSParagraphStyleAttributeName : paragraph
    };
    CGSize size = [string sizeWithAttributes:attrs];
    int w = (int)maximum(1, ceil(size.width) + 2);
    if (width > 0)
      w = (int)minimum(w, width);
    int h = (int)maximum(1, ceil(size.height) + 2);
    NSLayoutManager *layout = nil;
    __attribute__((objc_precise_lifetime)) NSTextStorage *storage = nil;
    NSRange glyphs = NSMakeRange(0, 0);
    if (height > 0) {
      /* Same TextKit metrics and zero container padding as UITextView. */
      w = (int)maximum(1, width);
      storage = [[NSTextStorage alloc] initWithString:string attributes:attrs];
      layout = [NSLayoutManager new];
      NSTextContainer *container = [[NSTextContainer alloc]
          initWithSize:CGSizeMake(w / raster_scale, height / raster_scale)];
      container.lineFragmentPadding = 0;
      [storage addLayoutManager:layout];
      [layout addTextContainer:container];
      glyphs = [layout glyphRangeForTextContainer:container];
      h = (int)maximum(
          1, minimum(height,
                     ceil([layout usedRectForTextContainer:container].size.height * raster_scale)));
    }
    if ((size_t)w > SIZE_MAX / 4 / (size_t)h)
      return false;
    uint8_t *data = calloc((size_t)w * h, 4);
    if (!data)
      return false;
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGContextRef context = CGBitmapContextCreate(
        data, w, h, 8, w * 4, space, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(space);
    if (!context) {
      free(data);
      return false;
    }
    CGContextTranslateCTM(context, 0, h);
    CGContextScaleCTM(context, 1, -1);
    CGContextScaleCTM(context, raster_scale, raster_scale);
    UIGraphicsPushContext(context);
    if (layout)
      [layout drawGlyphsForGlyphRange:glyphs atPoint:CGPointZero];
    else
      [string drawInRect:CGRectMake(0, 0, w, h) withAttributes:attrs];
    UIGraphicsPopContext();
    CGContextRelease(context);
    *out = (text_bitmap){w, h, data, (size_t)w * h * 4};
    return true;
  }
}
