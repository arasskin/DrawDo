#include "profiling/presentation.h"
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
@interface DFPresentationStamp : NSObject
@property(atomic) int64_t time;
@end
@implementation DFPresentationStamp
@end
bool presentation_init(void) { return true; }
void presentation_begin(presentation_stamp *s) {
  *s = (presentation_stamp){0, -1, -1, -1, -1, NULL};
}
void presentation_track(presentation_stamp *s, const void *raw) {
  if (!raw)
    return;
  id<MTLDrawable> drawable = (__bridge id<MTLDrawable>)raw;
  DFPresentationStamp *stamp = [DFPresentationStamp new];
  stamp.time = -1;
  s->native = (__bridge_retained void *)stamp;
  s->id = drawable.drawableID;
  int64_t offset = app_now() - (int64_t)(CACurrentMediaTime() * 1e9);
  [drawable addPresentedHandler:^(id<MTLDrawable> displayed) {
    if (displayed.presentedTime > 0)
      stamp.time = (int64_t)(displayed.presentedTime * 1e9) + offset;
  }];
}
void presentation_collect(presentation_stamp *s) {
  if (s->native)
    s->present = ((__bridge DFPresentationStamp *)s->native).time;
}
void presentation_release(presentation_stamp *s) {
  if (s->native) {
    CFRelease(s->native);
    s->native = NULL;
  }
}
