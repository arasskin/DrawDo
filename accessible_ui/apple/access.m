#include "accessible_ui/native.h"
#import <UIKit/UIKit.h>
@interface AccessElement : UIAccessibilityElement
@property(nonatomic) uint64_t key;
@property(nonatomic) unsigned flags;
@property(nonatomic) BOOL available;
@end
@implementation AccessElement
- (BOOL)accessibilityActivate {
  return self.available && !(self.flags & ACCESS_DISABLED) &&
         (self.flags & (ACCESS_BUTTON | ACCESS_CHECKBOX)) &&
         platform_accessibility_send(self.key, ACCESS_ACTIVATE);
}
- (BOOL)accessibilityScroll:(UIAccessibilityScrollDirection)direction {
  if (!self.available || !(self.flags & ACCESS_SCROLL))
    return NO;
  if (direction == UIAccessibilityScrollDirectionDown ||
      direction == UIAccessibilityScrollDirectionRight)
    return platform_accessibility_send(self.key, ACCESS_FORWARD);
  if (direction == UIAccessibilityScrollDirectionUp ||
      direction == UIAccessibilityScrollDirectionLeft)
    return platform_accessibility_send(self.key, ACCESS_BACKWARD);
  return NO;
}
@end
@interface AccessView : UIView
@end
@implementation AccessView
- (BOOL)accessibilityScroll:(UIAccessibilityScrollDirection)direction {
  for (AccessElement *element in self.accessibilityElements)
    if (element.flags & ACCESS_SCROLL)
      return [element accessibilityScroll:direction];
  return NO;
}
- (BOOL)pointInside:(CGPoint)point withEvent:(UIEvent *)event {
  (void)point;
  (void)event;
  return NO; // Semantics only; canvas still receives ordinary touches.
}
@end
static __weak UIWindow *host;
static AccessView *overlay;
void access_apple_init(void *window) {
  [overlay removeFromSuperview];
  overlay = nil;
  host = (__bridge UIWindow *)window;
}
bool platform_accessibility_set(const access_snapshot *snapshot) {
  if (!host)
    return false;
  if (!overlay) {
    overlay = [[AccessView alloc] initWithFrame:host.bounds];
    overlay.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    overlay.isAccessibilityElement = NO;
    [host addSubview:overlay];
  }
  NSArray<AccessElement *> *old = overlay.accessibilityElements ?: @[];
  NSMutableArray<AccessElement *> *next = [NSMutableArray arrayWithCapacity:snapshot->count];
  for (AccessElement *element in old)
    element.available = NO;
  BOOL structure = old.count != snapshot->count;
  CGFloat scale = host.screen.scale;
  for (size_t i = 0; i < snapshot->count; i++) {
    const access_node *node = &snapshot->nodes[i];
    AccessElement *element = nil;
    for (AccessElement *candidate in old)
      if (candidate.key == node->id) {
        element = candidate;
        break;
      }
    if (!element) {
      element = [[AccessElement alloc] initWithAccessibilityContainer:overlay];
      structure = YES;
    }
    element.key = node->id;
    element.flags = node->flags;
    element.available = YES;
    element.accessibilityIdentifier =
        [NSString stringWithFormat:@"canvas-%llu", (unsigned long long)node->id];
    element.accessibilityLabel =
        [NSString stringWithUTF8String:(const char *)snapshot->strings.data + node->label];
    NSString *value =
        [NSString stringWithUTF8String:(const char *)snapshot->strings.data + node->value];
    if (node->flags & ACCESS_CHECKBOX)
      value = node->flags & ACCESS_CHECKED ? @"Checked" : @"Unchecked";
    element.accessibilityValue = value.length ? value : nil;
    UIAccessibilityTraits traits = UIAccessibilityTraitNone;
    if (node->flags & (ACCESS_BUTTON | ACCESS_CHECKBOX))
      traits |= UIAccessibilityTraitButton;
    if (node->flags & ACCESS_HEADER)
      traits |= UIAccessibilityTraitHeader;
    if (node->flags & ACCESS_SELECTED)
      traits |= UIAccessibilityTraitSelected;
    if (node->flags & ACCESS_DISABLED)
      traits |= UIAccessibilityTraitNotEnabled;
    if (!(node->flags & (ACCESS_BUTTON | ACCESS_CHECKBOX | ACCESS_SCROLL)))
      traits |= UIAccessibilityTraitStaticText;
    element.accessibilityTraits = traits;
    CGRect r = CGRectMake(node->bounds.x / scale, node->bounds.y / scale, node->bounds.w / scale,
                          node->bounds.h / scale);
    element.accessibilityFrame = UIAccessibilityConvertFrameToScreenCoordinates(r, host);
    [next addObject:element];
    if (i < old.count && old[i] != element)
      structure = YES;
  }
  overlay.accessibilityElements = next;
  // Keep element identity on value/bounds updates so VoiceOver retains its focus.
  if (structure)
    UIAccessibilityPostNotification(UIAccessibilityLayoutChangedNotification, nil);
  return true;
}
