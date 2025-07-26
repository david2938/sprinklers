#include <interval_func.h>

void IntervalFunc::loop() {
    if ((now - prevMillis) >= interval) {
        func();
        prevMillis = now;
    }
}