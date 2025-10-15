#include <iostream>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <unistd.h>
#include "shared.h"

using namespace std;

int main(int argc, char* argv[]) {
    cout << "Worker starting, PID:" << getpid() << " PPID:" << getppid() << endl;

    if (argc != 3) {
        cerr << "Usage: ./worker <seconds> <nanoseconds>" << endl;
        return 1;
    }

    int max_seconds = stoi(argv[1]);
    int max_nanos = stoi(argv[2]);
    cout << "Called with: " << endl;
    cout << "Interval: " << max_seconds << " seconds, " << max_nanos << " nanoseconds" << endl;

    int shared_memory_id = shmget(SHM_KEY, sizeof(SimulatedClock), 0666);

    if (shared_memory_id < 0) {
        perror("worker: shmget");
        return 1;
    }

    int message_queue_id = msgget(MSG_KEY, 0666);
    if (message_queue_id < 0) {
        perror("worker: msgget");
        return 1;
    }

    // Attach the segment
    SimulatedClock* shared_clock = (SimulatedClock*)shmat(shared_memory_id, NULL, 0);
    if (shared_clock == (void*)-1) {
        perror("worker: shmat");
        return 1;
    }

    // Calculate Termination Time
    int term_seconds = shared_clock->seconds + max_seconds;
    int term_nanos = shared_clock->nanoseconds + max_nanos;

    // Nanosecond to second
    if (term_nanos >= 1000000000) {
        term_seconds++;
        term_nanos -= 1000000000;
    }

    // Start up message
    cout << "WORKER PID:" << getpid() << " PPID:" << getppid() << endl;
    cout << " SysClockS: " << shared_clock->seconds << " SysclockNano: " << shared_clock->nanoseconds << " TermTimeS: " << term_seconds << " TermTimeNano: " << term_nanos << endl;
    cout << " --Just Starting" << endl;

    int previous_seconds = shared_clock->seconds;

    // Check if termination time has been reached
    message msg;
    bool should_terminate = false;

    do {
        // Block while waiting for a message from oss
        if (msgrcv(message_queue_id, &msg, sizeof(msg.content), getpid(), 0) == -1) {
            perror("worker: msgrcv");
            exit(1);
        }

        // Check termination time
        if (shared_clock->seconds > term_seconds || (shared_clock->seconds == term_seconds && shared_clock->nanoseconds >= term_nanos)) {
            should_terminate = true;
            msg.content = 0; // terminate
        } else {
            msg.content = 1; // running
        }

        // Send message back to oss
        msg.messageType = getppid(); // Parent
        if (msgsnd(message_queue_id, &msg, sizeof(msg.content), 0) == -1) {
            perror("worker: msgsnd");
            exit(1);
        }

    } while (!should_terminate);

    // Termination message
    cout << "WORKER PID:" << getpid() << " PPID:" << getppid() << endl;
    cout << " SysClocks: " << shared_clock->seconds << " SysclockNano: " << shared_clock->nanoseconds << " TermTimeS: " << term_seconds << " TermTimeNano: " << term_nanos << endl;
    cout << " --Terminating" << endl;


    // Detach from shared memory
    shmdt(shared_clock);
    return 0;
}


