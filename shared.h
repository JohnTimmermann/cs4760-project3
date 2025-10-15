#ifndef SHARED_H
#define SHARED_H

// Define a unique key for the shared memory segment.
#define SHM_KEY 0x1234
#define MSG_KEY 0x5678

struct message {
    long messageType;
    int content; // 1 for running, 0 for terminating
};

struct SimulatedClock {
    int seconds;
    int nanoseconds;
};

#endif
