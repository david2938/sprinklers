struct IntervalFunc {
    unsigned long interval;
    unsigned long prevMillis;
    unsigned long& now;
    void (*func)();

    IntervalFunc(unsigned long interval, unsigned long& now, void (*func)()): 
        interval(interval), prevMillis(0), now(now), func(func)
        {};
    void loop();
};

typedef struct IntervalFunc interval_func_t;