#ifndef SHARED_H
#define SHARED_H

// Define a unique key for the shared memory segment.
#define SHM_KEY 0x1234

struct SimulatedClock {
    int seconds;
    int nanoseconds;
};

#endif
