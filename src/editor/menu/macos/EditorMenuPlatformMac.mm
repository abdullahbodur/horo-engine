#import <AppKit/AppKit.h>

#include "editor/menu/EditorMenuPlatform.h"

#include "Horo/Editor/Localization/ILocalizationService.h"

#include <optional>

namespace
{
std::optional<Horo::Editor::EditorMenuAction> g_pendingAction;

NSString *ToNSString(const std::string_view value)
{
    return [[NSString alloc] initWithBytes:value.data() length:value.size() encoding:NSUTF8StringEncoding];
}

NSString *LocalizedTitle(const Horo::Editor::ILocalizationService &localization, const std::string_view key)
{
    return ToNSString(localization.Get("editor", key));
}
} // namespace

@interface HoroEditorMenuTarget : NSObject
+ (void)performEditorMenuAction:(id)sender;
@end

@implementation HoroEditorMenuTarget
+ (void)performEditorMenuAction:(id)sender
{
    const NSInteger tag = [(NSMenuItem *)sender tag];
    g_pendingAction = static_cast<Horo::Editor::EditorMenuAction>(tag);
}
@end

namespace Horo::Editor
{
namespace
{
void AppendModelItem(NSMenu *menu, const EditorMenuItem &modelItem, const ILocalizationService &localization)
{
    if (modelItem.kind == EditorMenuItemKind::Separator)
    {
        [menu addItem:[NSMenuItem separatorItem]];
        return;
    }

    NSString *title = LocalizedTitle(localization, modelItem.labelKey);
    NSString *keyEquivalent = ToNSString(modelItem.macKeyEquivalent);
    const bool actionable = modelItem.kind == EditorMenuItemKind::Command &&
                            modelItem.action != EditorMenuAction::None && modelItem.enabledByDefault;
    SEL selector = actionable ? @selector(performEditorMenuAction:) : nil;
    NSMenuItem *nativeItem = [[NSMenuItem alloc] initWithTitle:title action:selector keyEquivalent:keyEquivalent];
    [title release];
    [keyEquivalent release];

    if (modelItem.kind == EditorMenuItemKind::Submenu)
    {
        NSString *submenuTitle = LocalizedTitle(localization, modelItem.labelKey);
        NSMenu *submenu = [[NSMenu alloc] initWithTitle:submenuTitle];
        [submenuTitle release];
        [submenu setAutoenablesItems:NO];
        for (const EditorMenuItem &child : modelItem.children)
        {
            AppendModelItem(submenu, child, localization);
        }
        [nativeItem setSubmenu:submenu];
        [submenu release];
        [nativeItem setEnabled:YES];
    }
    else
    {
        [nativeItem setTarget:actionable ? [HoroEditorMenuTarget class] : nil];
        [nativeItem setTag:static_cast<NSInteger>(modelItem.action)];
        [nativeItem setEnabled:actionable];
    }

    [menu addItem:nativeItem];
    [nativeItem release];
}

void AppendApplicationMenu(NSMenu *mainMenu)
{
    NSMenuItem *appRoot = [[NSMenuItem alloc] initWithTitle:@"Horo Editor" action:nil keyEquivalent:@""];
    NSMenu *appMenu = [[NSMenu alloc] initWithTitle:@"Horo Editor"];
    [appMenu setAutoenablesItems:NO];

    NSMenuItem *about = [[NSMenuItem alloc] initWithTitle:@"About Horo Editor" action:nil keyEquivalent:@""];
    [about setEnabled:NO];
    [appMenu addItem:about];
    [about release];
    [appMenu addItem:[NSMenuItem separatorItem]];

    NSMenuItem *services = [[NSMenuItem alloc] initWithTitle:@"Services" action:nil keyEquivalent:@""];
    NSMenu *servicesMenu = [[NSMenu alloc] initWithTitle:@"Services"];
    [services setSubmenu:servicesMenu];
    [NSApp setServicesMenu:servicesMenu];
    [appMenu addItem:services];
    [servicesMenu release];
    [services release];
    [appMenu addItem:[NSMenuItem separatorItem]];

    NSMenuItem *hide = [[NSMenuItem alloc] initWithTitle:@"Hide Horo Editor"
                                                  action:@selector(hide:)
                                           keyEquivalent:@"h"];
    [appMenu addItem:hide];
    [hide release];
    NSMenuItem *hideOthers = [[NSMenuItem alloc] initWithTitle:@"Hide Others"
                                                        action:@selector(hideOtherApplications:)
                                                 keyEquivalent:@"h"];
    [hideOthers setKeyEquivalentModifierMask:NSEventModifierFlagCommand | NSEventModifierFlagOption];
    [appMenu addItem:hideOthers];
    [hideOthers release];
    NSMenuItem *showAll = [[NSMenuItem alloc] initWithTitle:@"Show All"
                                                     action:@selector(unhideAllApplications:)
                                              keyEquivalent:@""];
    [appMenu addItem:showAll];
    [showAll release];
    [appMenu addItem:[NSMenuItem separatorItem]];

    NSMenuItem *quit = [[NSMenuItem alloc] initWithTitle:@"Quit Horo Editor"
                                                  action:@selector(performEditorMenuAction:)
                                           keyEquivalent:@"q"];
    [quit setTarget:[HoroEditorMenuTarget class]];
    [quit setTag:static_cast<NSInteger>(EditorMenuAction::ExitApplication)];
    [appMenu addItem:quit];
    [quit release];

    [appRoot setSubmenu:appMenu];
    [mainMenu addItem:appRoot];
    [appMenu release];
    [appRoot release];
}
} // namespace

/** @copydoc UsesNativeEditorMenuBar */
bool UsesNativeEditorMenuBar() noexcept
{
    return true;
}

/** @copydoc InstallNativeEditorMenuBar */
void InstallNativeEditorMenuBar(const EditorMenuModel &model, const ILocalizationService &localization)
{
    [NSApplication sharedApplication];
    NSMenu *mainMenu = [[NSMenu alloc] initWithTitle:@"Horo Editor"];
    [mainMenu setAutoenablesItems:NO];
    AppendApplicationMenu(mainMenu);
    for (const EditorMenuItem &menu : model.menus)
    {
        AppendModelItem(mainMenu, menu, localization);
    }
    [NSApp setMainMenu:mainMenu];
    [mainMenu release];
}

/** @copydoc PollNativeEditorMenuAction */
std::optional<EditorMenuAction> PollNativeEditorMenuAction() noexcept
{
    std::optional<EditorMenuAction> action = g_pendingAction;
    g_pendingAction.reset();
    return action;
}
} // namespace Horo::Editor
