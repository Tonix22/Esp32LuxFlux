# Effects

The effects task receives typed synchronization events through a bounded
FreeRTOS queue. It renders blue entry/progress, an all-off replacement pause,
green success, and red failure above the generic LED API. After a successful
commit it plays a copied validated sequence. TCP reads never run in this task.
