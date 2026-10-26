#include "platform.h"
#include "loop.h"
#include "text_buffers/native.h"
#include "sokol/sokol_app.h"
#import <UIKit/UIKit.h>
static NSString *directory;
static id keyboard_observer;
static CGRect keyboard;
const char *platform_init(void) {
  UIWindow *window = (__bridge UIWindow *)sapp_ios_get_window();
#ifdef CRDT_DEMO_TIMING
  UIApplication.sharedApplication.idleTimerDisabled = YES;
#endif
  window.overrideUserInterfaceStyle = UIUserInterfaceStyleLight;
  window.backgroundColor = UIColor.whiteColor;
  editor_apple_init((__bridge void *)window);
  directory =
      NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES)
          .firstObject;
  if (![[NSFileManager defaultManager] createDirectoryAtPath:directory
                                 withIntermediateDirectories:YES
                                                  attributes:nil
                                                       error:nil])
    return NULL;
  keyboard_observer = [[NSNotificationCenter defaultCenter]
      addObserverForName:UIKeyboardWillChangeFrameNotification
                  object:nil
                   queue:nil
              usingBlock:^(NSNotification *note) {
                keyboard = [note.userInfo[UIKeyboardFrameEndUserInfoKey] CGRectValue];
                loop_wake();
              }];
  return directory.UTF8String;
}
float platform_scale(void) {
  UIWindow *window = (__bridge UIWindow *)sapp_ios_get_window();
  return window.screen.scale;
}
editor_rect platform_bounds(void) {
  UIWindow *window = (__bridge UIWindow *)sapp_ios_get_window();
  CGRect r = UIEdgeInsetsInsetRect(window.bounds, window.safeAreaInsets);
  CGRect overlap = CGRectIntersection(r, [window convertRect:keyboard fromWindow:nil]);
  if (!CGRectIsNull(overlap) && overlap.size.height > 0)
    r.size.height = MAX(1, CGRectGetMinY(overlap) - r.origin.y);
  r = CGRectInset(r, 16, 16);
  CGFloat s = window.screen.scale;
  return (editor_rect){r.origin.x * s, r.origin.y * s, r.size.width * s, MAX(1, r.size.height) * s};
}
void platform_error(const char *message) {
  NSLog(@"CRDT demo: %s", message);
  UIAlertController *alert =
      [UIAlertController alertControllerWithTitle:@"CRDT demo"
                                          message:[NSString stringWithUTF8String:message]
                                   preferredStyle:UIAlertControllerStyleAlert];
  [alert addAction:[UIAlertAction actionWithTitle:@"OK"
                                            style:UIAlertActionStyleDefault
                                          handler:nil]];
  UIWindow *window = (__bridge UIWindow *)sapp_ios_get_window();
  [window.rootViewController presentViewController:alert animated:YES completion:nil];
}
void platform_shutdown(void) {
  if (keyboard_observer)
    [[NSNotificationCenter defaultCenter] removeObserver:keyboard_observer];
  keyboard_observer = nil;
}
