// UIKit uses Core Text for shaping, font fallback and text layout.
#include "platform/text_raster.h"
#import <UIKit/UIKit.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace text_raster {
void init(){}
void shutdown(){}
bitmap render(const std::string& text,float pixels,int width){
  @autoreleasepool {
    NSString* string=[[NSString alloc] initWithBytes:text.data() length:text.size() encoding:NSUTF8StringEncoding];
    if(!string)throw std::runtime_error("Invalid UTF-8 text");
    auto paragraph=[[NSMutableParagraphStyle alloc] init];paragraph.lineBreakMode=NSLineBreakByTruncatingTail;
    auto attrs=@{NSFontAttributeName:[UIFont systemFontOfSize:pixels],NSForegroundColorAttributeName:UIColor.whiteColor,NSParagraphStyleAttributeName:paragraph};
    auto size=[string sizeWithAttributes:attrs];
    int w=std::max(1,int(std::ceil(size.width))+2);if(width>0)w=std::min(w,width);
    int h=std::max(1,int(std::ceil(size.height))+2);
    bitmap result{w,h,std::vector<uint8_t>(size_t(w)*h*4)};
    auto space=CGColorSpaceCreateDeviceRGB();
    auto context=CGBitmapContextCreate(result.pixels.data(),w,h,8,w*4,space,uint32_t(kCGImageAlphaPremultipliedLast)|uint32_t(kCGBitmapByteOrder32Big));
    CGColorSpaceRelease(space);if(!context)throw std::runtime_error("Text raster allocation failed");
    CGContextTranslateCTM(context,0,h);CGContextScaleCTM(context,1,-1);
    UIGraphicsPushContext(context);[string drawInRect:CGRectMake(0,0,w,h) withAttributes:attrs];UIGraphicsPopContext();
    CGContextRelease(context);return result;
  }
}
}
