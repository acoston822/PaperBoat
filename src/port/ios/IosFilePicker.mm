#include "IosFilePicker.h"

#if defined(__IOS__)

#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <utility>

@interface PaperboatPickerDelegate : NSObject <UIDocumentPickerDelegate>
@property(nonatomic, copy) void (^onDone)(NSString*);
@end

@implementation PaperboatPickerDelegate
- (void)documentPicker:(UIDocumentPickerViewController*)controller didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls {
    // asCopy: the file is already a private copy in the app's sandbox, so no security scope is needed.
    NSString* path = urls.count > 0 ? urls[0].path : @"";
    if (self.onDone) {
        self.onDone(path);
    }
}
- (void)documentPickerWasCancelled:(UIDocumentPickerViewController*)controller {
    if (self.onDone) {
        self.onDone(@"");
    }
}
@end

static UIViewController* TopViewController() {
    UIWindow* window = nil;
    for (UIScene* scene in UIApplication.sharedApplication.connectedScenes) {
        if (![scene isKindOfClass:[UIWindowScene class]]) {
            continue;
        }
        for (UIWindow* candidate in ((UIWindowScene*) scene).windows) {
            if (candidate.isKeyWindow) {
                window = candidate;
                break;
            }
        }
        if (window != nil) {
            break;
        }
    }
    UIViewController* top = window.rootViewController;
    while (top.presentedViewController != nil) {
        top = top.presentedViewController;
    }
    return top;
}

namespace Paperboat {

void IosPickFile(std::function<void(std::string)> onPicked) {
    // Held until the picker answers; the delegate is only weakly referenced by the controller.
    static PaperboatPickerDelegate* sDelegate = nil;

    dispatch_async(dispatch_get_main_queue(), ^{
        UIViewController* top = TopViewController();
        if (top == nil) {
            onPicked("");
            return;
        }
        // Any file: N64 ROM extensions have no registered type, so filtering by type would hide them.
        UIDocumentPickerViewController* picker =
            [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[ UTTypeData ] asCopy:YES];
        picker.allowsMultipleSelection = NO;
        sDelegate = [[PaperboatPickerDelegate alloc] init];
        sDelegate.onDone = ^(NSString* path) {
            sDelegate = nil;
            onPicked(std::string(path.UTF8String ? path.UTF8String : ""));
        };
        picker.delegate = sDelegate;
        [top presentViewController:picker animated:YES completion:nil];
    });
}

} // namespace Paperboat

#endif
