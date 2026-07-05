// Objective-C++ bridge for macOS window appearance customization.
// Window.cpp calls the C-linkage function below; this file must stay isolated
// so no other translation unit pulls in AppKit headers.

// clang-format off
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
// clang-format on

#import <AppKit/AppKit.h>

extern "C" void ApplyDarkTitleBarMac(GLFWwindow *glfwWindow) {
  if (!glfwWindow)
    return;

  NSWindow *window = glfwGetCocoaWindow(glfwWindow);
  if (!window)
    return;

  window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
  window.titlebarAppearsTransparent = YES;
  window.backgroundColor =
      [NSColor colorWithSRGBRed:1.0 / 255.0
                          green:7.0 / 255.0
                           blue:17.0 / 255.0
                          alpha:1.0];
}

extern "C" void ApplyAppIconMac(const char *iconPath) {
  if (!iconPath || !*iconPath)
    return;

  NSString *path = [NSString stringWithUTF8String:iconPath];
  NSImage *icon = [[NSImage alloc] initWithContentsOfFile:path];
  if (icon) {
    [NSApp setApplicationIconImage:icon];
    [icon release];
  }
}

extern "C" void InvokeFileNew();
extern "C" void InvokeFileOpen();
extern "C" void InvokeFileResetLayout();
extern "C" void InvokeFileSettings();
extern "C" void InvokeFileCloseEditor();
extern "C" void InvokeAddPanel();
extern "C" void InvokeAddProp();
extern "C" void InvokeAddLight();
extern "C" void InvokeAddCamera();
extern "C" void InvokeAddPropFromAsset();
extern "C" void InvokeEditUndo();
extern "C" void InvokeEditRedo();
extern "C" void InvokeEditRename();
extern "C" void InvokeEditCreatePrefab();
extern "C" void InvokeEditDuplicate();
extern "C" void InvokeEditDelete();
extern "C" void InvokeViewFlyMode();
extern "C" void InvokeViewHelp();
extern "C" void InvokeViewQuickOpen();
extern "C" void InvokeViewCommandPalette();
extern "C" void InvokeViewResetLayout();
extern "C" void InvokeBuildRelease();

@interface MenuHandler : NSObject
+ (void)fileNew:(id)sender;
+ (void)fileOpen:(id)sender;
+ (void)fileResetLayout:(id)sender;
+ (void)fileSettings:(id)sender;
+ (void)fileCloseEditor:(id)sender;
+ (void)addPanel:(id)sender;
+ (void)addProp:(id)sender;
+ (void)addLight:(id)sender;
+ (void)addCamera:(id)sender;
+ (void)addPropFromAsset:(id)sender;
+ (void)editUndo:(id)sender;
+ (void)editRedo:(id)sender;
+ (void)editRename:(id)sender;
+ (void)editCreatePrefab:(id)sender;
+ (void)editDuplicate:(id)sender;
+ (void)editDelete:(id)sender;
+ (void)viewFlyMode:(id)sender;
+ (void)viewHelp:(id)sender;
+ (void)viewQuickOpen:(id)sender;
+ (void)viewCommandPalette:(id)sender;
+ (void)viewResetLayout:(id)sender;
+ (void)buildRelease:(id)sender;
@end

@implementation MenuHandler
+ (void)fileNew:(id)sender { InvokeFileNew(); }
+ (void)fileOpen:(id)sender { InvokeFileOpen(); }
+ (void)fileResetLayout:(id)sender { InvokeFileResetLayout(); }
+ (void)fileSettings:(id)sender { InvokeFileSettings(); }
+ (void)fileCloseEditor:(id)sender { InvokeFileCloseEditor(); }
+ (void)addPanel:(id)sender { InvokeAddPanel(); }
+ (void)addProp:(id)sender { InvokeAddProp(); }
+ (void)addLight:(id)sender { InvokeAddLight(); }
+ (void)addCamera:(id)sender { InvokeAddCamera(); }
+ (void)addPropFromAsset:(id)sender { InvokeAddPropFromAsset(); }
+ (void)editUndo:(id)sender { InvokeEditUndo(); }
+ (void)editRedo:(id)sender { InvokeEditRedo(); }
+ (void)editRename:(id)sender { InvokeEditRename(); }
+ (void)editCreatePrefab:(id)sender { InvokeEditCreatePrefab(); }
+ (void)editDuplicate:(id)sender { InvokeEditDuplicate(); }
+ (void)editDelete:(id)sender { InvokeEditDelete(); }
+ (void)viewFlyMode:(id)sender { InvokeViewFlyMode(); }
+ (void)viewHelp:(id)sender { InvokeViewHelp(); }
+ (void)viewQuickOpen:(id)sender { InvokeViewQuickOpen(); }
+ (void)viewCommandPalette:(id)sender { InvokeViewCommandPalette(); }
+ (void)viewResetLayout:(id)sender { InvokeViewResetLayout(); }
+ (void)buildRelease:(id)sender { InvokeBuildRelease(); }
@end

/** @brief Creates a native menu item wired to the static bridge target.
 *
 *  Cocoa disables targetless menu items when no object in the responder chain
 *  handles the selector. Horo routes native app-menu commands through the
 *  MenuHandler bridge instead, so every command item must carry that explicit
 *  target. The caller owns the returned item, matching Cocoa's alloc/init rule.
 */
static NSMenuItem *CreateNativeMenuItem(NSString *title, SEL action, NSString *keyEquivalent) {
  NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:title action:action keyEquivalent:keyEquivalent];
  [item setTarget:[MenuHandler class]];
  return item;
}

extern "C" void SetupNativeMenuBarMac() {
  NSMenu *mainMenu = [[NSMenu alloc] init];

  NSMenu *fileMenu = [[NSMenu alloc] initWithTitle:@"File"];
  NSMenuItem *item = nil;
  item = CreateNativeMenuItem(@"New Scene", @selector(fileNew:), @"n");
  [fileMenu addItem:item];
  [item release];
  item = CreateNativeMenuItem(@"Open Scene...", @selector(fileOpen:), @"o");
  [fileMenu addItem:item];
  [item release];
  [fileMenu addItem:[NSMenuItem separatorItem]];
  item = CreateNativeMenuItem(@"Reset Layout", @selector(fileResetLayout:), @"");
  [fileMenu addItem:item];
  [item release];
  item = CreateNativeMenuItem(@"Settings...", @selector(fileSettings:), @",");
  [fileMenu addItem:item];
  [item release];
  [fileMenu addItem:[NSMenuItem separatorItem]];
  item = CreateNativeMenuItem(@"Close Editor", @selector(fileCloseEditor:), @"w");
  [fileMenu addItem:item];
  [item release];
  NSMenuItem *fileItem = [[NSMenuItem alloc] initWithTitle:@"File" action:nil keyEquivalent:@""];
  [fileItem setSubmenu:fileMenu];
  [mainMenu addItem:fileItem];
  [fileItem release];
  [fileMenu release];

  NSMenu *addMenu = [[NSMenu alloc] initWithTitle:@"Add"];
  item = CreateNativeMenuItem(@"Panel", @selector(addPanel:), @"");
  [addMenu addItem:item];
  [item release];
  item = CreateNativeMenuItem(@"Prop", @selector(addProp:), @"");
  [addMenu addItem:item];
  [item release];
  item = CreateNativeMenuItem(@"Light", @selector(addLight:), @"");
  [addMenu addItem:item];
  [item release];
  item = CreateNativeMenuItem(@"Camera", @selector(addCamera:), @"");
  [addMenu addItem:item];
  [item release];
  [addMenu addItem:[NSMenuItem separatorItem]];
  item = CreateNativeMenuItem(@"Prop from Selected Asset", @selector(addPropFromAsset:), @"");
  [addMenu addItem:item];
  [item release];
  NSMenuItem *addItem = [[NSMenuItem alloc] initWithTitle:@"Add" action:nil keyEquivalent:@""];
  [addItem setSubmenu:addMenu];
  [mainMenu addItem:addItem];
  [addItem release];
  [addMenu release];

  NSMenu *editMenu = [[NSMenu alloc] initWithTitle:@"Edit"];
  item = CreateNativeMenuItem(@"Undo", @selector(editUndo:), @"z");
  [editMenu addItem:item];
  [item release];
  item = CreateNativeMenuItem(@"Redo", @selector(editRedo:), @"Z");
  [editMenu addItem:item];
  [item release];
  [editMenu addItem:[NSMenuItem separatorItem]];
  item = CreateNativeMenuItem(@"Rename...", @selector(editRename:), @"");
  [editMenu addItem:item];
  [item release];
  item = CreateNativeMenuItem(@"Create Prefab", @selector(editCreatePrefab:), @"");
  [editMenu addItem:item];
  [item release];
  item = CreateNativeMenuItem(@"Duplicate", @selector(editDuplicate:), @"d");
  [editMenu addItem:item];
  [item release];
  item = CreateNativeMenuItem(@"Delete", @selector(editDelete:), @"\b");
  [editMenu addItem:item];
  [item release];
  NSMenuItem *editItem = [[NSMenuItem alloc] initWithTitle:@"Edit" action:nil keyEquivalent:@""];
  [editItem setSubmenu:editMenu];
  [mainMenu addItem:editItem];
  [editItem release];
  [editMenu release];

  NSMenu *viewMenu = [[NSMenu alloc] initWithTitle:@"View"];
  item = CreateNativeMenuItem(@"Viewport Nav", @selector(viewFlyMode:), @"");
  [viewMenu addItem:item];
  [item release];
  [viewMenu addItem:[NSMenuItem separatorItem]];
  item = CreateNativeMenuItem(@"Help", @selector(viewHelp:), @"?");
  [viewMenu addItem:item];
  [item release];
  item = CreateNativeMenuItem(@"Quick Open", @selector(viewQuickOpen:), @"p");
  [viewMenu addItem:item];
  [item release];
  item = CreateNativeMenuItem(@"Command Palette", @selector(viewCommandPalette:), @"P");
  [viewMenu addItem:item];
  [item release];
  [viewMenu addItem:[NSMenuItem separatorItem]];
  item = CreateNativeMenuItem(@"Reset Layout", @selector(viewResetLayout:), @"");
  [viewMenu addItem:item];
  [item release];
  NSMenuItem *viewItem = [[NSMenuItem alloc] initWithTitle:@"View" action:nil keyEquivalent:@""];
  [viewItem setSubmenu:viewMenu];
  [mainMenu addItem:viewItem];
  [viewItem release];
  [viewMenu release];

  NSMenu *buildMenu = [[NSMenu alloc] initWithTitle:@"Build"];
  item = CreateNativeMenuItem(@"Build & Release...", @selector(buildRelease:), @"b");
  [buildMenu addItem:item];
  [item release];
  NSMenuItem *buildItem = [[NSMenuItem alloc] initWithTitle:@"Build" action:nil keyEquivalent:@""];
  [buildItem setSubmenu:buildMenu];
  [mainMenu addItem:buildItem];
  [buildItem release];
  [buildMenu release];

  [NSApp setMainMenu:mainMenu];
  [mainMenu release];
}
