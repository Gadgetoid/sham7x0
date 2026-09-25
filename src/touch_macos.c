#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "touch.h"

#define TETRA_VID 0x2575
#define TETRA_PID 0xfe13
#define FINGER_STRIDE 5
#define MAX_FINGERS 10
#define SCAN_TIME_BYTES 2
#define CONTACT_COUNT_OFFSET (MAX_FINGERS * FINGER_STRIDE + SCAN_TIME_BYTES)
#define LOGICAL_MAX 0x7fff
#define QUEUE_SIZE 256
#define NO_CONTACT -1

static touch_event_t queue[QUEUE_SIZE];
static int head = 0, tail = 0;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t thread;
static CFRunLoopRef runloop = NULL;
static atomic_bool attached = false;
static int panel_width = 1280, panel_height = 800;
static int primary = NO_CONTACT;
static float last_x = 0, last_y = 0;
static uint8_t report_buffer[128];

static void push(uint8_t kind, float x, float y) {
    pthread_mutex_lock(&lock);
    int next = (head + 1) % QUEUE_SIZE;
    if (next != tail) {
        queue[head] = (touch_event_t){ kind, x, y };
        head = next;
    }
    pthread_mutex_unlock(&lock);
}

static void release_primary(void) {
    if (primary != NO_CONTACT) push(TOUCH_UP, last_x, last_y);
    primary = NO_CONTACT;
}

static void on_report(void *context, IOReturn result, void *sender, IOHIDReportType type, uint32_t report_id,
                      uint8_t *data, CFIndex length) {
    (void)context; (void)sender; (void)type;
    if (result != kIOReturnSuccess || length <= 0 || report_id != 1) return;
    CFIndex offset = data[0] == report_id ? 1 : 0;
    if (length - offset < CONTACT_COUNT_OFFSET + 1) return;
    int count = data[offset + CONTACT_COUNT_OFFSET];
    if (count > MAX_FINGERS) count = MAX_FINGERS;
    bool primary_seen = false;
    for (int i = 0; i < count; i++) {
        const uint8_t *finger = data + offset + i * FINGER_STRIDE;
        bool tip = finger[0] & 0x01;
        int contact = (finger[0] >> 3) & 0x1f;
        float x = (float)(finger[1] | (finger[2] << 8)) * (panel_width - 1) / LOGICAL_MAX;
        float y = (float)(finger[3] | (finger[4] << 8)) * (panel_height - 1) / LOGICAL_MAX;
        if (primary == NO_CONTACT && tip) {
            primary = contact;
            last_x = x;
            last_y = y;
            push(TOUCH_DOWN, x, y);
            primary_seen = true;
        } else if (contact == primary) {
            primary_seen = true;
            if (tip) {
                last_x = x;
                last_y = y;
                push(TOUCH_MOVE, x, y);
            } else {
                last_x = x;
                last_y = y;
                release_primary();
            }
        }
    }
    if (!primary_seen) release_primary();
}

static CFMutableDictionaryRef matching(int vendor, int product) {
    CFMutableDictionaryRef dictionary = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
                                                                  &kCFTypeDictionaryValueCallBacks);
    CFNumberRef vendor_number = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &vendor);
    CFNumberRef product_number = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &product);
    CFDictionarySetValue(dictionary, CFSTR(kIOHIDVendorIDKey), vendor_number);
    CFDictionarySetValue(dictionary, CFSTR(kIOHIDProductIDKey), product_number);
    CFRelease(vendor_number);
    CFRelease(product_number);
    return dictionary;
}

static void on_matched(void *context, IOReturn result, void *sender, IOHIDDeviceRef device) {
    (void)context; (void)result; (void)sender;
    IOReturn opened = IOHIDDeviceOpen(device, kIOHIDOptionsTypeSeizeDevice);
    if (opened != kIOReturnSuccess) {
        fprintf(stderr, "touch: could not open the panel (0x%x). Grant Input Monitoring to this app or terminal.\n", opened);
        return;
    }
    IOHIDDeviceRegisterInputReportCallback(device, report_buffer, sizeof report_buffer, on_report, NULL);
    IOHIDDeviceScheduleWithRunLoop(device, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    atomic_store(&attached, true);
    printf("touch: panel attached (%04x:%04x)\n", TETRA_VID, TETRA_PID);
}

static void on_removed(void *context, IOReturn result, void *sender, IOHIDDeviceRef device) {
    (void)context; (void)result; (void)sender;
    IOHIDDeviceUnscheduleFromRunLoop(device, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    IOHIDDeviceRegisterInputReportCallback(device, report_buffer, sizeof report_buffer, NULL, NULL);
    IOHIDDeviceClose(device, kIOHIDOptionsTypeSeizeDevice);
    release_primary();
    atomic_store(&attached, false);
    printf("touch: panel detached\n");
}

static void *touch_main(void *argument) {
    (void)argument;
    IOHIDManagerRef manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    CFMutableDictionaryRef dictionary = matching(TETRA_VID, TETRA_PID);
    IOHIDManagerSetDeviceMatching(manager, dictionary);
    CFRelease(dictionary);
    IOHIDManagerRegisterDeviceMatchingCallback(manager, on_matched, NULL);
    IOHIDManagerRegisterDeviceRemovalCallback(manager, on_removed, NULL);
    IOHIDManagerScheduleWithRunLoop(manager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    if (IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone) != kIOReturnSuccess) {
        fprintf(stderr, "touch: IOHIDManagerOpen failed\n");
        return NULL;
    }
    runloop = CFRunLoopGetCurrent();
    CFRunLoopRun();
    return NULL;
}

bool touch_start(void) {
    if (runloop) return true;
    return pthread_create(&thread, NULL, touch_main, NULL) == 0;
}

void touch_set_panel(int width, int height) {
    panel_width = width;
    panel_height = height;
}

bool touch_pop(touch_event_t *event) {
    pthread_mutex_lock(&lock);
    bool got = tail != head;
    if (got) {
        *event = queue[tail];
        tail = (tail + 1) % QUEUE_SIZE;
    }
    pthread_mutex_unlock(&lock);
    return got;
}

bool touch_attached(void) {
    return atomic_load(&attached);
}

void touch_stop(void) {
    if (!runloop) return;
    CFRunLoopStop(runloop);
    pthread_join(thread, NULL);
    runloop = NULL;
}
