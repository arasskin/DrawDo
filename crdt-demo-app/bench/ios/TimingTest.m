#import <XCTest/XCTest.h>
@interface TimingTest : XCTestCase
@end
@implementation TimingTest
- (void)testCapturePermission {
  XCUIApplication *app = [[XCUIApplication alloc] initWithBundleIdentifier:@"dev.drawdo.crdt"];
  [app launch];
  XCUIApplication *spring =
      [[XCUIApplication alloc] initWithBundleIdentifier:@"com.apple.springboard"];
  XCUIElement *button = spring.buttons[@"Record Screen"];
  if ([button waitForExistenceWithTimeout:8])
    [button tap];
  else
    NSLog(@"Capture permission UI: %@", spring.debugDescription);
  [app activate];
  NSLog(@"Application after consent: %@", app.debugDescription);
  NSLog(@"CRDT_TIMING_STARTED");
  [NSThread sleepForTimeInterval:105];
}
@end
