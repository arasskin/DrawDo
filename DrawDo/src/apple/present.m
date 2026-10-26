#include "present.h"
#import <Metal/Metal.h>
@interface DrawDoPresentation : NSObject
@property(atomic) int status;
@end
@implementation DrawDoPresentation
@end
void present_track(presentation *p, const void *raw) {
  if (p->armed || !raw)
    return;
  id<MTLDrawable> drawable = (__bridge id<MTLDrawable>)raw;
  DrawDoPresentation *stamp = [DrawDoPresentation new];
  p->native = (__bridge_retained void *)stamp;
  p->armed = true;
  [drawable addPresentedHandler:^(id<MTLDrawable> displayed) {
    stamp.status = displayed.presentedTime > 0 ? 1 : 2;
  }];
}
bool present_ready(presentation *p) {
  if (!p->native)
    return false;
  int status = ((__bridge DrawDoPresentation *)p->native).status;
  if (status == 2)
    present_reset(p);
  return status == 1;
}
void present_reset(presentation *p) {
  if (p->native)
    CFRelease(p->native);
  *p = (presentation){0};
}
