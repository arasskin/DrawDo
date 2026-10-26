#import <XCTest/XCTest.h>
@interface TimingTest : XCTestCase
@end
@implementation TimingTest
- (void)run:(NSString *)bundle {
  XCUIApplication *app = [[XCUIApplication alloc] initWithBundleIdentifier:bundle];
  [app launch];
  XCUIApplication *spring = [[XCUIApplication alloc] initWithBundleIdentifier:@"com.apple.springboard"];
  XCUIElement *button = spring.buttons[@"Record Screen"];
  if ([button waitForExistenceWithTimeout:8]) [button tap];
  [app activate];
  NSLog(@"Application: %@", app.debugDescription);
  NSLog(@"SCHEDULING_STARTED");
  [NSThread sleepForTimeInterval:58];
}
- (void)testNative { [self run:@"dev.drawdo.crdt"]; }
- (void)testFlutter { [self run:@"dev.drawdo.textLatency"]; }
@end
