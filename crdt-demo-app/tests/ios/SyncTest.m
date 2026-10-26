#import <XCTest/XCTest.h>
@interface SyncTest : XCTestCase
@end
@implementation SyncTest
- (void)expect:(NSString *)text editor:(XCUIElement *)editor timeout:(NSTimeInterval)timeout {
  NSPredicate *matches = [NSPredicate predicateWithFormat:@"value == %@", text];
  XCTNSPredicateExpectation *wait = [[XCTNSPredicateExpectation alloc] initWithPredicate:matches object:editor];
  XCTAssertEqual([XCTWaiter waitForExpectations:@[wait] timeout:timeout], XCTWaiterResultCompleted);
}
- (void)testSync {
  self.continueAfterFailure = NO;
  XCUIApplication *app = [[XCUIApplication alloc] initWithBundleIdentifier:@"dev.drawdo.crdt"];
  [app launch];
  XCUIElement *editor = app.textViews[@"Shared text"];
  XCTAssertTrue([editor waitForExistenceWithTimeout:15]);
  [self expect:@"Android" editor:editor timeout:45];
  [XCUIDevice.sharedDevice pressButton:XCUIDeviceButtonHome];
  [app activate];
  [self expect:@"Android" editor:editor timeout:10];
  CGRect before = editor.frame;
  [[editor coordinateWithNormalizedOffset:CGVectorMake(0.9, 0.02)] tap];
  XCTAssertTrue([app.keyboards.firstMatch waitForExistenceWithTimeout:5]);
  XCTAssertEqualWithAccuracy(editor.frame.origin.y, before.origin.y, 1);
  [editor typeText:@" iPhone "];
  [self expect:@"Android iPhone synced" editor:editor timeout:45];
  XCTAssertTrue(app.keyboards.firstMatch.exists); // Remote refresh while editing.
  XCTAttachment *shot = [XCTAttachment attachmentWithScreenshot:app.screenshot];
  shot.name = @"Black text on white, synchronized while keyboard is open";
  shot.lifetime = XCTAttachmentLifetimeKeepAlways;
  [self addAttachment:shot];
  [app terminate];
  [app launch];
  editor = app.textViews[@"Shared text"];
  XCTAssertTrue([editor waitForExistenceWithTimeout:15]);
  [self expect:@"Android iPhone synced" editor:editor timeout:15];
  [[editor coordinateWithNormalizedOffset:CGVectorMake(0.9, 0.02)] tap];
  NSMutableString *backspaces = [NSMutableString string];
  for (NSUInteger i = 0; i < [@"Android iPhone synced" length]; ++i)
    [backspaces appendString:XCUIKeyboardKeyDelete];
  [editor typeText:backspaces];
  [self expect:@"" editor:editor timeout:10];
}
@end
