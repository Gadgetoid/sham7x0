#import <Cocoa/Cocoa.h>

#include "menu.h"
#include "touch.h"

#define MENU_QUEUE 32

static int queue[MENU_QUEUE];
static int queued = 0;
static NSMenuItem *items[MENU_COUNT];

@interface PocketMenuTarget : NSObject
@end

@implementation PocketMenuTarget
- (void)fire:(NSMenuItem *)item {
    if (queued < MENU_QUEUE) queue[queued++] = (int)item.tag;
}
@end

static PocketMenuTarget *target = nil;
static NSMenuItem *holders[2];
static int holder_count = 0;

static void add_item(NSMenu *menu, int tag, NSString *title, NSString *key, NSEventModifierFlags modifiers) {
    NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:title action:@selector(fire:) keyEquivalent:key];
    item.keyEquivalentModifierMask = modifiers;
    item.target = target;
    item.tag = tag;
    [menu addItem:item];
    items[tag] = item;
}

static NSMenu *add_menu(NSString *title) {
    NSMenu *menu = [[NSMenu alloc] initWithTitle:title];
    NSMenuItem *holder = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
    holder.submenu = menu;
    holders[holder_count++] = holder;
    return menu;
}

static void attach_menus(void) {
    NSMenu *bar = [NSApp mainMenu];
    if (!bar) {
        bar = [[NSMenu alloc] init];
        [NSApp setMainMenu:bar];
    }
    for (int i = 0; i < holder_count; i++) {
        if (holders[i].menu == bar) continue;
        if (holders[i].menu) [holders[i].menu removeItem:holders[i]];
        NSInteger window_menu = [bar indexOfItemWithTitle:@"Window"];
        NSInteger position = window_menu >= 0 ? window_menu : bar.numberOfItems;
        [bar insertItem:holders[i] atIndex:position];
    }
}


static NSMenu *submenu(NSMenu *parent, NSString *title) {
    NSMenu *menu = [[NSMenu alloc] initWithTitle:title];
    NSMenuItem *holder = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
    holder.submenu = menu;
    [parent addItem:holder];
    return menu;
}

void menu_install(void) {
    target = [[PocketMenuTarget alloc] init];

    NSMenu *run = add_menu(@"Run");
    add_item(run, MENU_RELOAD, @"Reset", @"r", NSEventModifierFlagCommand);
    add_item(run, MENU_INTERRUPT, @"Press ON (Ctrl-C)", @"", 0);
    add_item(run, MENU_INITIALIZE, @"Initialize Memory", @"", 0);
    add_item(run, MENU_TEST_MODE, @"Factory Test Mode", @"", 0);
    add_item(run, MENU_INSTALL_WZD, @"Install .wzd…", @"i", NSEventModifierFlagCommand);
    [run addItem:[NSMenuItem separatorItem]];
    add_item(run, MENU_SHOW_CONSOLE, @"Show Console", @"j", NSEventModifierFlagCommand);
    add_item(run, MENU_FOCUS_CONSOLE, @"Focus Console", @"l", NSEventModifierFlagCommand);

    NSMenu *system = add_menu(@"System");
    NSMenu *layout = submenu(system, @"Layout");
    NSString *layouts[] = { @"Screen Only", @"Screen & Frame", @"Screen & Buttons", @"Screen & Keyboard" };
    for (int i = 0; i < MENU_LAYOUT_END - MENU_LAYOUT_FIRST; i++) add_item(layout, MENU_LAYOUT_FIRST + i, layouts[i], @"", 0);
    [layout addItem:[NSMenuItem separatorItem]];
    add_item(layout, MENU_LAYOUT_NEXT, @"Next Layout", @"k", NSEventModifierFlagCommand);
    [layout addItem:[NSMenuItem separatorItem]];
    add_item(layout, MENU_TOUCHSCREEN, @"Touchscreen Mode", @"t", NSEventModifierFlagCommand | NSEventModifierFlagShift);
    [system addItem:[NSMenuItem separatorItem]];
    NSMenu *realism = submenu(system, @"Realism");
    add_item(realism, MENU_DEAD_COLUMNS, @"Dead Columns", @"d", NSEventModifierFlagCommand);
    add_item(realism, MENU_SCRATCHES, @"Scratches", @"", 0);
    add_item(realism, MENU_WEAR, @"Wear", @"", 0);
    [system addItem:[NSMenuItem separatorItem]];
    add_item(system, MENU_BACKLIGHT, @"Backlight", @"b", NSEventModifierFlagCommand);
    add_item(system, MENU_SOUND, @"Sound", @"", 0);
    [system addItem:[NSMenuItem separatorItem]];
    NSMenu *rate = submenu(system, @"Frame Rate");
    NSString *rates[] = { @"Unlimited", @"60 fps", @"30 fps", @"20 fps", @"15 fps", @"10 fps" };
    for (int i = 0; i < MENU_FPS_END - MENU_FPS_FIRST; i++) add_item(rate, MENU_FPS_FIRST + i, rates[i], @"", 0);
    NSMenu *response = submenu(system, @"Response Time");
    NSString *responses[] = { @"Instant", @"Fast", @"Normal", @"Slow", @"Very Slow" };
    for (int i = 0; i < MENU_RESPONSE_END - MENU_RESPONSE_FIRST; i++) add_item(response, MENU_RESPONSE_FIRST + i, responses[i], @"", 0);
    attach_menus();
}

void menu_ensure(void) {
    if (holder_count) attach_menus();
}

void menu_perform(int item) {
    if (item < 0 || item >= MENU_COUNT || !items[item]) return;
    NSMenu *menu = items[item].menu;
    [menu performActionForItemAtIndex:[menu indexOfItem:items[item]]];
}

int menu_poll(void) {
    if (queued == 0) return -1;
    int item = queue[0];
    for (int i = 1; i < queued; i++) queue[i - 1] = queue[i];
    queued--;
    return item;
}

void menu_set_checked(int item, bool checked) {
    if (item < 0 || item >= MENU_COUNT || !items[item]) return;
    NSControlStateValue state = checked ? NSControlStateValueOn : NSControlStateValueOff;
    if (items[item].state != state) items[item].state = state;
}

bool window_cover_display(void *handle, bool cover) {
    NSWindow *window = (__bridge NSWindow *)handle;
    if (!window) return false;
    if (cover) {
        NSApp.presentationOptions = NSApplicationPresentationAutoHideMenuBar | NSApplicationPresentationAutoHideDock;
        window.level = NSMainMenuWindowLevel + 1;
        window.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorStationary |
                                    NSWindowCollectionBehaviorFullScreenNone;
    } else {
        NSApp.presentationOptions = NSApplicationPresentationDefault;
        window.level = NSNormalWindowLevel;
        window.collectionBehavior = NSWindowCollectionBehaviorDefault;
    }
    return true;
}
