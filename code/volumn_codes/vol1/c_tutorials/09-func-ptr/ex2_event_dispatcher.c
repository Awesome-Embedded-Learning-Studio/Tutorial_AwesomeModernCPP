#include <stdio.h>
#include <stddef.h>

#define kMaxHandlers 8
#define kMaxEvents 4

typedef void (*EventHandler)(int event_data);

typedef struct {
    EventHandler handlers[kMaxHandlers];
    int count;
} EventSlot;

static EventSlot g_events[kMaxEvents];

void event_register(int event_id, EventHandler handler)
{
    if (event_id < 0 || event_id >= kMaxEvents) return;
    EventSlot* slot = &g_events[event_id];
    if (slot->count < kMaxHandlers) {
        slot->handlers[slot->count++] = handler;
    }
}

void event_emit(int event_id, int data)
{
    if (event_id < 0 || event_id >= kMaxEvents) return;
    EventSlot* slot = &g_events[event_id];
    for (int i = 0; i < slot->count; i++) {
        slot->handlers[i](data);
    }
}

// Sample handlers
static void on_sensor_read(int data)
{
    printf("  [sensor_logger] Reading: %d\n", data);
}

static void on_sensor_alert(int data)
{
    if (data > 100) {
        printf("  [ALERT] Sensor value %d exceeds threshold!\n", data);
    }
}

static void on_any_event(int data)
{
    printf("  [monitor] Event fired with data=%d\n", data);
}

int main(void)
{
    event_register(0, on_sensor_read);
    event_register(0, on_sensor_alert);
    event_register(0, on_any_event);

    printf("Emit event 0, data=42:\n");
    event_emit(0, 42);

    printf("\nEmit event 0, data=150:\n");
    event_emit(0, 150);

    return 0;
}
