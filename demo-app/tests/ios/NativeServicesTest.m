#import <XCTest/XCTest.h>
#include "clipboard/clipboard.h"
#include "haptics/haptics.h"
#include "accessible_ui/native.h"
@interface NativeServicesTest : XCTestCase
@end
@implementation NativeServicesTest
- (void)testNativeServices {
  void (^check)(void) = ^{
    UIPasteboard *pasteboard = UIPasteboard.generalPasteboard;
    NSArray *previous = pasteboard.items;
    @try {
      const char *sample = "Clipboard 🌱 café 日本語 e\xcc\x81\nsecond line";
      ct_bytes out = {0};
      XCTAssertTrue(clipboard_set(sample));
      XCTAssertTrue(clipboard_get(&out));
      XCTAssertEqual(strcmp(ct_bytes_string(&out), sample), 0);
      XCTAssertFalse(clipboard_set("\xff"));
      XCTAssertTrue(clipboard_get(&out));
      XCTAssertEqual(strcmp(ct_bytes_string(&out), sample), 0);
      XCTAssertTrue(clipboard_set(""));
      XCTAssertTrue(clipboard_get(&out));
      XCTAssertEqual(out.size, 0u);
      ct_bytes_free(&out);
      haptic_play(HAPTIC_SELECTION);
      haptic_play(HAPTIC_IMPACT);
      haptic_play(HAPTIC_SUCCESS);
    } @finally {
      pasteboard.items = previous;
    }

    UIWindow *window = [[UIWindow alloc] initWithFrame:CGRectMake(0, 0, 390, 844)];
    access_apple_init((__bridge void *)window);
    access_tree tree = {0};
    access_begin(&tree);
    XCTAssertTrue(
        access_add(&tree, 77, (access_rect){30, 60, 300, 90}, "Save 🌱", "", ACCESS_BUTTON));
    XCTAssertTrue(access_end(&tree));
    UIAccessibilityElement *element = window.subviews.lastObject.accessibilityElements.firstObject;
    XCTAssertEqualObjects(element.accessibilityLabel, @"Save 🌱");
    XCTAssertTrue(element.accessibilityTraits & UIAccessibilityTraitButton);
    XCTAssertTrue([element accessibilityActivate]);
    access_event event;
    XCTAssertTrue(access_poll(&event));
    XCTAssertEqual(event.id, 77u);
    access_begin(&tree);
    XCTAssertTrue(access_add(&tree, 77, (access_rect){30, 60, 300, 90}, "Save 🌱", "",
                             ACCESS_BUTTON | ACCESS_DISABLED));
    XCTAssertTrue(access_end(&tree));
    XCTAssertEqual(element, window.subviews.lastObject.accessibilityElements.firstObject);
    XCTAssertFalse([element accessibilityActivate]);
    access_begin(&tree);
    XCTAssertTrue(access_end(&tree));
    XCTAssertFalse([element accessibilityActivate]);
    access_free(&tree);
    access_apple_init(NULL);
  };
  if (NSThread.isMainThread)
    check();
  else
    dispatch_sync(dispatch_get_main_queue(), check);
}
@end
