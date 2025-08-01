/*
 * Copyright 2025 David Main
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

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
