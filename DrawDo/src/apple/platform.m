#include "platform.h"
#include "loop.h"
#include "text_buffers/native.h"
#include "accessible_ui/native.h"
#include "sokol/sokol_app.h"
#import <UIKit/UIKit.h>
static NSString *directory;

const char *platform_init(void) {
  UIWindow *window = (__bridge UIWindow *)sapp_ios_get_window();

  window.overrideUserInterfaceStyle = UIUserInterfaceStyleLight;
  window.backgroundColor = UIColor.whiteColor;
  editor_apple_init((__bridge void *)window);
  access_apple_init((__bridge void *)window);
  directory =
      NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES)
          .firstObject;
  if (![[NSFileManager defaultManager] createDirectoryAtPath:directory
                                 withIntermediateDirectories:YES
                                                  attributes:nil
                                                       error:nil])
    return NULL;
  return directory.UTF8String;
}
float platform_scale(void) {
  UIWindow *window = (__bridge UIWindow *)sapp_ios_get_window();
  return window.screen.scale;
}
editor_rect platform_bounds(void) {
  UIWindow *window = (__bridge UIWindow *)sapp_ios_get_window();
  CGRect r = UIEdgeInsetsInsetRect(window.bounds, window.safeAreaInsets);
  CGFloat s = window.screen.scale;
  return (editor_rect){r.origin.x * s, r.origin.y * s, r.size.width * s, MAX(1, r.size.height) * s};
}
void platform_error(const char *message) {
  NSLog(@"DrawDo: %s", message);
  UIAlertController *alert =
      [UIAlertController alertControllerWithTitle:@"DrawDo"
                                          message:[NSString stringWithUTF8String:message]
                                   preferredStyle:UIAlertControllerStyleAlert];
  [alert addAction:[UIAlertAction actionWithTitle:@"OK"
                                            style:UIAlertActionStyleDefault
                                          handler:nil]];
  UIWindow *window = (__bridge UIWindow *)sapp_ios_get_window();
  [window.rootViewController presentViewController:alert animated:YES completion:nil];
}
void platform_shutdown(void) {}
