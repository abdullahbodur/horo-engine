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
@end

extern "C" void SetupNativeMenuBarMac() {
  NSMenu *mainMenu = [[NSMenu alloc] init];

  NSMenu *fileMenu = [[NSMenu alloc] initWithTitle:@"File"];
  NSMenuItem *item = nil;
  item = [[NSMenuItem alloc] initWithTitle:@"New Scene" action:@selector(fileNew:) keyEquivalent:@"n"];
  [fileMenu addItem:item];
  [item release];
  item = [[NSMenuItem alloc] initWithTitle:@"Open Scene..." action:@selector(fileOpen:) keyEquivalent:@"o"];
  [fileMenu addItem:item];
  [item release];
  [fileMenu addItem:[NSMenuItem separatorItem]];
  item = [[NSMenuItem alloc] initWithTitle:@"Reset Layout" action:@selector(fileResetLayout:) keyEquivalent:@""];
  [fileMenu addItem:item];
  [item release];
  item = [[NSMenuItem alloc] initWithTitle:@"Settings..." action:@selector(fileSettings:) keyEquivalent:@","];
  [fileMenu addItem:item];
  [item release];
  [fileMenu addItem:[NSMenuItem separatorItem]];
  item = [[NSMenuItem alloc] initWithTitle:@"Close Editor" action:@selector(fileCloseEditor:) keyEquivalent:@"w"];
  [fileMenu addItem:item];
  [item release];
  NSMenuItem *fileItem = [[NSMenuItem alloc] initWithTitle:@"File" action:nil keyEquivalent:@""];
  [fileItem setSubmenu:fileMenu];
  [mainMenu addItem:fileItem];
  [fileItem release];
  [fileMenu release];

  NSMenu *addMenu = [[NSMenu alloc] initWithTitle:@"Add"];
  item = [[NSMenuItem alloc] initWithTitle:@"Panel" action:@selector(addPanel:) keyEquivalent:@""];
  [addMenu addItem:item];
  [item release];
  item = [[NSMenuItem alloc] initWithTitle:@"Prop" action:@selector(addProp:) keyEquivalent:@""];
  [addMenu addItem:item];
  [item release];
  item = [[NSMenuItem alloc] initWithTitle:@"Light" action:@selector(addLight:) keyEquivalent:@""];
  [addMenu addItem:item];
  [item release];
  item = [[NSMenuItem alloc] initWithTitle:@"Camera" action:@selector(addCamera:) keyEquivalent:@""];
  [addMenu addItem:item];
  [item release];
  [addMenu addItem:[NSMenuItem separatorItem]];
  item = [[NSMenuItem alloc] initWithTitle:@"Prop from Selected Asset" action:@selector(addPropFromAsset:) keyEquivalent:@""];
  [addMenu addItem:item];
  [item release];
  NSMenuItem *addItem = [[NSMenuItem alloc] initWithTitle:@"Add" action:nil keyEquivalent:@""];
  [addItem setSubmenu:addMenu];
  [mainMenu addItem:addItem];
  [addItem release];
  [addMenu release];

  NSMenu *editMenu = [[NSMenu alloc] initWithTitle:@"Edit"];
  item = [[NSMenuItem alloc] initWithTitle:@"Undo" action:@selector(editUndo:) keyEquivalent:@"z"];
  [editMenu addItem:item];
  [item release];
  item = [[NSMenuItem alloc] initWithTitle:@"Redo" action:@selector(editRedo:) keyEquivalent:@"Z"];
  [editMenu addItem:item];
  [item release];
  [editMenu addItem:[NSMenuItem separatorItem]];
  item = [[NSMenuItem alloc] initWithTitle:@"Rename..." action:@selector(editRename:) keyEquivalent:@""];
  [editMenu addItem:item];
  [item release];
  item = [[NSMenuItem alloc] initWithTitle:@"Create Prefab" action:@selector(editCreatePrefab:) keyEquivalent:@""];
  [editMenu addItem:item];
  [item release];
  item = [[NSMenuItem alloc] initWithTitle:@"Duplicate" action:@selector(editDuplicate:) keyEquivalent:@"d"];
  [editMenu addItem:item];
  [item release];
  item = [[NSMenuItem alloc] initWithTitle:@"Delete" action:@selector(editDelete:) keyEquivalent:@"\b"];
  [editMenu addItem:item];
  [item release];
  NSMenuItem *editItem = [[NSMenuItem alloc] initWithTitle:@"Edit" action:nil keyEquivalent:@""];
  [editItem setSubmenu:editMenu];
  [mainMenu addItem:editItem];
  [editItem release];
  [editMenu release];

  NSMenu *viewMenu = [[NSMenu alloc] initWithTitle:@"View"];
  item = [[NSMenuItem alloc] initWithTitle:@"Viewport Nav" action:@selector(viewFlyMode:) keyEquivalent:@""];
  [viewMenu addItem:item];
  [item release];
  [viewMenu addItem:[NSMenuItem separatorItem]];
  item = [[NSMenuItem alloc] initWithTitle:@"Help" action:@selector(viewHelp:) keyEquivalent:@"?"];
  [viewMenu addItem:item];
  [item release];
  item = [[NSMenuItem alloc] initWithTitle:@"Quick Open" action:@selector(viewQuickOpen:) keyEquivalent:@"p"];
  [viewMenu addItem:item];
  [item release];
  item = [[NSMenuItem alloc] initWithTitle:@"Command Palette" action:@selector(viewCommandPalette:) keyEquivalent:@"P"];
  [viewMenu addItem:item];
  [item release];
  [viewMenu addItem:[NSMenuItem separatorItem]];
  item = [[NSMenuItem alloc] initWithTitle:@"Reset Layout" action:@selector(viewResetLayout:) keyEquivalent:@""];
  [viewMenu addItem:item];
  [item release];
  NSMenuItem *viewItem = [[NSMenuItem alloc] initWithTitle:@"View" action:nil keyEquivalent:@""];
  [viewItem setSubmenu:viewMenu];
  [mainMenu addItem:viewItem];
  [viewItem release];
  [viewMenu release];

  [NSApp setMainMenu:mainMenu];
  [mainMenu release];
}
