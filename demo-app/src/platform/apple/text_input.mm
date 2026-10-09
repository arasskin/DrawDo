#include "platform/text_input.h"
#include "sokol/sokol_app.h"
#import <UIKit/UIKit.h>
#include <optional>
#include <memory>
namespace {
UITextField* field;UITextView* document_field;
// Keep the callable itself alive across frame deliveries, including mutable
// captures. Copying std::function here would reset each live editor draft.
std::shared_ptr<std::function<void(std::string)>> callback;
std::optional<std::string> pending;bool final=false;
}
@interface DFTextDelegate : NSObject<UITextFieldDelegate,UITextViewDelegate>
-(void)doneDocument;
@end
@implementation DFTextDelegate
-(BOOL)textFieldShouldReturn:(UITextField*)textField {
  pending=std::string(textField.text.UTF8String ?: "");final=true;
  [textField resignFirstResponder];[textField removeFromSuperview];field=nil;return YES;
}
-(void)textViewDidChange:(UITextView*)textView {
  if(!textView.markedTextRange){pending=std::string(textView.text.UTF8String ?: "");final=false;}
}
-(void)doneDocument {
  pending=std::string(document_field.text.UTF8String ?: "");final=true;
  [document_field resignFirstResponder];[document_field removeFromSuperview];document_field=nil;
}
@end
namespace {DFTextDelegate* delegate;}
namespace editor {
void cancel(){[field resignFirstResponder];[field removeFromSuperview];field=nil;[document_field resignFirstResponder];[document_field removeFromSuperview];document_field=nil;callback={};pending.reset();}
void open(std::string text,float x,float y,float width,float height,std::function<void(std::string)> submit){
  cancel();callback=std::make_shared<std::function<void(std::string)>>(std::move(submit));
  UIWindow* window=(__bridge UIWindow*)sapp_ios_get_window();float scale=window.screen.scale;
  if(!delegate)delegate=[DFTextDelegate new];
  field=[[UITextField alloc] initWithFrame:CGRectMake(x/scale,y/scale,width/scale,std::max(height/scale,40.0f))];
  field.text=[[NSString alloc] initWithBytes:text.data() length:text.size() encoding:NSUTF8StringEncoding];
  field.delegate=delegate;field.returnKeyType=UIReturnKeyDone;field.borderStyle=UITextBorderStyleRoundedRect;
  field.textColor=UIColor.whiteColor;field.backgroundColor=[UIColor colorWithRed:.12 green:.19 blue:.24 alpha:1];
  field.font=[UIFont systemFontOfSize:17];field.autocorrectionType=UITextAutocorrectionTypeDefault;
  [window addSubview:field];[field becomeFirstResponder];
}
void open_live(std::string text,float x,float y,float width,float height,std::function<void(std::string)> changed){
  cancel();callback=std::make_shared<std::function<void(std::string)>>(std::move(changed));if(!delegate)delegate=[DFTextDelegate new];
  UIWindow* window=(__bridge UIWindow*)sapp_ios_get_window();float scale=window.screen.scale;
  document_field=[[UITextView alloc] initWithFrame:CGRectMake(x/scale,y/scale,width/scale,std::max(height/scale,80.0f))];
  document_field.text=[[NSString alloc] initWithBytes:text.data() length:text.size() encoding:NSUTF8StringEncoding];
  document_field.delegate=delegate;document_field.font=[UIFont systemFontOfSize:17];document_field.textColor=UIColor.whiteColor;
  document_field.backgroundColor=[UIColor colorWithRed:.12 green:.19 blue:.24 alpha:1];
  UIToolbar* toolbar=[[UIToolbar alloc] initWithFrame:CGRectMake(0,0,width/scale,44)];
  toolbar.items=@[[[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemFlexibleSpace target:nil action:nil],[[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemDone target:delegate action:@selector(doneDocument)]];
  document_field.inputAccessoryView=toolbar;[window addSubview:document_field];[document_field becomeFirstResponder];
}
void poll(){if(pending){auto text=std::move(*pending);pending.reset();auto submit=callback;if(final)callback={};if(submit)(*submit)(std::move(text));}}
}
