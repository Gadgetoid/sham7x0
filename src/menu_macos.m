#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>

#include "menu.h"
#include "serial.h"
#include "touch.h"

#define MENU_QUEUE 32

static int queue[MENU_QUEUE];
static int queued = 0;
static NSMenuItem *items[MENU_COUNT];

static char serial_devices[MENU_SERIAL_DEVICE_END - MENU_SERIAL_DEVICE_FIRST][64];
static int serial_device_count = 0;
static char serial_current[512] = "";
static NSMenu *firmware_menu = nil;

@interface PocketMenuTarget : NSObject <NSMenuDelegate>
@end

static PocketMenuTarget *target = nil;
static void rebuild_serial_menu(NSMenu *menu);

@implementation PocketMenuTarget
- (void)fire:(NSMenuItem *)item {
    if (queued < MENU_QUEUE) queue[queued++] = (int)item.tag;
}
- (void)menuNeedsUpdate:(NSMenu *)menu {
    rebuild_serial_menu(menu);
}
@end
static NSMenuItem *holders[4];
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


static void add_serial_item(NSMenu *menu, int tag, NSString *title, bool checked) {
    NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:title action:@selector(fire:) keyEquivalent:@""];
    item.target = target;
    item.tag = tag;
    item.state = checked ? NSControlStateValueOn : NSControlStateValueOff;
    [menu addItem:item];
}

static void rebuild_serial_menu(NSMenu *menu) {
    [menu removeAllItems];
    serial_device_count = serial_list_devices(serial_devices, MENU_SERIAL_DEVICE_END - MENU_SERIAL_DEVICE_FIRST);
    bool on_device = false;
    for (int i = 0; i < serial_device_count; i++) on_device |= strcmp(serial_current, serial_devices[i]) == 0;
    add_serial_item(menu, MENU_SERIAL_OFF, @"Off", serial_current[0] == 0);
    NSString *virtual_title = @"Virtual Port (pty)";
    if (serial_current[0] && !on_device && strcmp(serial_current, "pty") != 0) virtual_title = [NSString stringWithFormat:@"Virtual Port at %s", serial_current];
    add_serial_item(menu, MENU_SERIAL_PTY, virtual_title, serial_current[0] && !on_device);
    if (serial_device_count) [menu addItem:[NSMenuItem separatorItem]];
    for (int i = 0; i < serial_device_count; i++) {
        const char *name = serial_devices[i] + strlen("/dev/cu.");
        add_serial_item(menu, MENU_SERIAL_DEVICE_FIRST + i, [NSString stringWithUTF8String:name], strcmp(serial_current, serial_devices[i]) == 0);
    }
}

void menu_set_serial(const char *current) {
    snprintf(serial_current, sizeof serial_current, "%s", current ? current : "");
}

const char *menu_serial_device(int item) {
    int index = item - MENU_SERIAL_DEVICE_FIRST;
    return index >= 0 && index < serial_device_count ? serial_devices[index] : NULL;
}

void menu_set_firmware(const char *const *titles, int count, int current) {
    if (!firmware_menu) return;
    [firmware_menu removeAllItems];
    for (int i = 0; i < count && i < MENU_FIRMWARE_END - MENU_FIRMWARE_FIRST; i++) {
        add_serial_item(firmware_menu, MENU_FIRMWARE_FIRST + i, [NSString stringWithUTF8String:titles[i]], i == current);
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
    add_item(run, MENU_INTERRUPT, @"Power", @"c", NSEventModifierFlagControl);
    [run addItem:[NSMenuItem separatorItem]];
    add_item(run, MENU_RELOAD, @"Reset", @"r", NSEventModifierFlagCommand);
    add_item(run, MENU_INITIALIZE, @"Initialize Memory", @"", 0);
    add_item(run, MENU_TEST_MODE, @"Factory Test Mode", @"", 0);

    NSMenu *install = add_menu(@"Install");
    add_item(install, MENU_INSTALL_WZD, @"Install .wzd…", @"i", NSEventModifierFlagCommand);
    add_item(install, MENU_APP_BROWSER, @"App Browser", @"i", NSEventModifierFlagCommand | NSEventModifierFlagShift);

    NSMenu *view = add_menu(@"View");
    NSString *layouts[] = { @"Screen Only", @"Screen & Frame", @"Screen & Buttons", @"Screen & Keyboard" };
    for (int i = 0; i < MENU_LAYOUT_END - MENU_LAYOUT_FIRST; i++) add_item(view, MENU_LAYOUT_FIRST + i, layouts[i], @"", 0);
    add_item(view, MENU_LAYOUT_NEXT, @"Next Layout", @"k", NSEventModifierFlagCommand);
    [view addItem:[NSMenuItem separatorItem]];
    add_item(view, MENU_BORDERLESS, @"Borderless", @"b", NSEventModifierFlagCommand | NSEventModifierFlagShift);
    add_item(view, MENU_COMPACT, @"Compact", @"", 0);
#ifdef SHAM_TOUCHSCREEN
    add_item(view, MENU_TOUCHSCREEN, @"Touchscreen Mode", @"t", NSEventModifierFlagCommand | NSEventModifierFlagShift);
#endif
    [view addItem:[NSMenuItem separatorItem]];
    add_item(view, MENU_SHOW_CONSOLE, @"Show Console", @"j", NSEventModifierFlagCommand);
    add_item(view, MENU_FOCUS_CONSOLE, @"Focus Console", @"l", NSEventModifierFlagCommand);
    [view addItem:[NSMenuItem separatorItem]];
    NSMenu *realism = submenu(view, @"Realism");
    add_item(realism, MENU_DEAD_COLUMNS, @"Dead Columns", @"d", NSEventModifierFlagCommand);
    add_item(realism, MENU_SCRATCHES, @"Scratches", @"", 0);
    add_item(realism, MENU_WEAR, @"Wear", @"", 0);
    add_item(realism, MENU_BACKLIGHT_TIMEOUT, @"Backlight Timeout", @"", 0);

    NSMenu *emulation = add_menu(@"Emulation");
    add_item(emulation, MENU_BACKLIGHT, @"Backlight", @"b", NSEventModifierFlagCommand);
    add_item(emulation, MENU_SOUND, @"Sound", @"", 0);
    firmware_menu = submenu(emulation, @"Firmware");
    NSMenu *serial = submenu(emulation, @"Serial Port");
    serial.delegate = target;
    rebuild_serial_menu(serial);
    [emulation addItem:[NSMenuItem separatorItem]];
    NSMenu *rate = submenu(emulation, @"Frame Rate");
    NSString *rates[] = { @"Unlimited", @"60 fps", @"30 fps", @"20 fps", @"15 fps", @"10 fps" };
    for (int i = 0; i < MENU_FPS_END - MENU_FPS_FIRST; i++) add_item(rate, MENU_FPS_FIRST + i, rates[i], @"", 0);
    NSMenu *response = submenu(emulation, @"Response Time");
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

#ifdef SHAM_TOUCHSCREEN
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
#endif

void window_set_transparent(void *handle, void *layer_handle, bool transparent) {
    NSWindow *window = (__bridge NSWindow *)handle;
    CALayer *layer = (__bridge CALayer *)layer_handle;
    if (!window) return;
    window.opaque = !transparent;
    window.backgroundColor = transparent ? NSColor.clearColor : NSColor.windowBackgroundColor;
    window.hasShadow = !transparent;
    layer.opaque = !transparent;
}

void window_set_aspect(void *handle, float width, float height) {
    NSWindow *window = (__bridge NSWindow *)handle;
    if (!window) return;
    if (width > 0 && height > 0) window.contentAspectRatio = NSMakeSize(width, height);
    else window.contentResizeIncrements = NSMakeSize(1, 1);
}
