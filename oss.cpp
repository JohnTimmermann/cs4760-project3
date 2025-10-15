#include <iostream>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <string> 
#include <unistd.h>
#include <signal.h>
#include "shared.h"

using namespace std;

struct PCB {
    int occupied;
    pid_t pid;
    int start_seconds;
    int start_nanos; 
};

const int MAX_PROCESSES = 20;

// Signal Handling Globals
int shared_memory_id;
SimulatedClock* shared_clock = nullptr;
PCB* process_table_ptr = nullptr;
volatile sig_atomic_t terminate_flag = 0;

void signal_handler(int signum) {
    cout << "\nOSS: Signal " << signum << " received. Initiating shutdown..." << endl;
    terminate_flag = 1;
}

// Cleanup function for shared memory and child processes
void cleanup() {
    cout << "OSS: Parent process terminating. Cleaning up..." << endl;
    // Kill any remaining children
    if (process_table_ptr != nullptr) {
        for (int i = 0; i < MAX_PROCESSES; i++) {
            if (process_table_ptr[i].occupied == 1) {
                kill(process_table_ptr[i].pid, SIGTERM);
            }
        }
    }
    // Clean up shared memory
    if (shared_clock != nullptr) shmdt(shared_clock);
    if (shared_memory_id > 0) shmctl(shared_memory_id, IPC_RMID, NULL);
    cout << "OSS: Cleanup complete." << endl;
}

int main(int argc, char* argv[]) {

    // Default values for command-line options
    int proc = 5;
    int simul = 3;
    float time_limit = 4.5;
    float interval = 0.2;

    int opt;
    // Parse command-line options
    while ((opt = getopt(argc, argv, "hn:s:t:i:")) != -1) {
        switch (opt) {
            case 'h':
                cout << "Usage: ./oss [-n proc] [-s simul] [-t time_limit] [-i interval]" << endl;
                return 0;
            case 'n':
                proc = atoi(optarg);
                break;
            case 's':
                simul = atoi(optarg);
                break;
            case 't':
                time_limit = atof(optarg);
                break;
            case 'i':
                interval = atof(optarg);
                break;
            default:
                cerr << "Usage: ./oss [-n proc] [-s simul] [-t time_limit] [-i interval]" << endl;
                return 1;
        }
    }

    // Print out the parameters passed
    cout << "OSS Starting, PID:" << getpid() << " PPID:" << getppid() << endl;
    cout << "Called with:" << endl;
    cout << "-n " << proc << endl;
    cout << "-s " << simul << endl;
    cout << "-t " << time_limit << endl;
    cout << "-i " << interval << endl;

    // Register Signal Handlers
    signal(SIGINT, signal_handler);  // Ctrl-C
    signal(SIGALRM, signal_handler); // Timeout
    alarm(60); // 60 second timer


    // Create shared memory segment
    shared_memory_id = shmget(SHM_KEY, sizeof(SimulatedClock), IPC_CREAT | 0666);
    if (shared_memory_id < 0) {
        perror("shmget");
        return 1;
    }

    // Attach shared memory segment
    shared_clock = (SimulatedClock*)shmat(shared_memory_id, NULL, 0);
    if (shared_clock == (void*)-1) {
        perror("shmat");
        return 1;
    }

    // Initialize the clock
    shared_clock->seconds = 0;
    shared_clock->nanoseconds = 0;
    cout << "OSS: Initialized clock to " << shared_clock->seconds << "s and " << shared_clock->nanoseconds << "ns." << endl;

    // Initialize Process Table
    PCB process_table[MAX_PROCESSES];
    process_table_ptr = process_table; // For signal handler access
    for (int i = 0; i < MAX_PROCESSES; ++i) {
        process_table_ptr[i].occupied = 0;
    }

    // Loop Vars
    int total_launched = 0;
    int active_children = 0;
    int next_launch_seconds = 0;
    int next_launch_nanos = 0;
    long last_print_nanos = 0;

    srand(time(NULL));

    while ((total_launched < proc || active_children > 0 ) && !terminate_flag) {
        // Increment clock
        shared_clock->nanoseconds += 10000000;
        if (shared_clock->nanoseconds >= 1000000000) {
            shared_clock->seconds++;
            shared_clock->nanoseconds -= 1000000000;
        }

        // Print process table every 0.5 seconds
        long current_total_nanos = shared_clock->seconds * 1000000000L + shared_clock->nanoseconds;
        if (current_total_nanos - last_print_nanos >= 500000000) { // 0.5 seconds
            last_print_nanos = current_total_nanos;
            cout << "\nOSS PID:" << getpid() << " SysClockS: " << shared_clock->seconds << " SysclockNano: " << shared_clock->nanoseconds << endl;
            cout << "Process Table:" << endl;
            cout << "Entry Occupied PID      StartS StartN" << endl;
            for (int i = 0; i < MAX_PROCESSES; ++i) {
                cout << i << "\t" << process_table_ptr[i].occupied << "\t" << process_table_ptr[i].pid << "\t" << process_table_ptr[i].start_seconds << "\t" << process_table_ptr[i].start_nanos << endl;
            }
            cout << endl;
        }

        // Check child terminations
        int status;
        pid_t terminated_pid;
        while ((terminated_pid = waitpid(-1, &status, WNOHANG)) > 0) {
            active_children--;
            for (int i = 0; i < MAX_PROCESSES; ++i) {
                if (process_table_ptr[i].pid == terminated_pid) {
                    process_table_ptr[i].occupied = 0; // Free up slot
                    cout << "Process with PID " << terminated_pid << " has terminated." << endl;
                    break;
                }
            }
        }

        // Launch new child if possible
        bool can_launch = (total_launched < proc) && (active_children < simul);
        bool time_to_launch = (shared_clock->seconds > next_launch_seconds || (shared_clock->seconds == next_launch_seconds && shared_clock->nanoseconds >= next_launch_nanos));

        if (can_launch && time_to_launch) {
            int process_index = -1;
            for (int i = 0; i < MAX_PROCESSES; ++i) {
                if (process_table_ptr[i].occupied == 0) {
                    process_index = i;
                    break;
                }
            }

            if (process_index != -1) {
                total_launched++;
                active_children++;
                
                pid_t child_pid = fork();

                // Child process
                if (child_pid == 0) { 
                    int random_second = rand() % (int)time_limit + 1;
                    int random_nano = rand() % 1000000000;
                    
                    string lifetime_seconds = to_string(random_second);
                    string lifetime_nanoseconds = to_string(random_nano);

                    execlp("./worker", "worker", lifetime_seconds.c_str(), lifetime_nanoseconds.c_str(), NULL);
                    perror("execlp"); exit(1);
                } else { 
                    // Parent process
                    process_table_ptr[process_index].occupied = 1;
                    process_table_ptr[process_index].pid = child_pid;
                    process_table_ptr[process_index].start_seconds = shared_clock->seconds;
                    process_table_ptr[process_index].start_nanos = shared_clock->nanoseconds;

                    cout << "OSS: Launched child " << child_pid << " into PCB slot " << process_index << " at " << shared_clock->seconds << "s " << shared_clock->nanoseconds << "ns." << endl;

                    // Calculate next launch time
                    long interval_nanos = interval * 1000000000;
                    next_launch_nanos = shared_clock->nanoseconds + interval_nanos;
                    next_launch_seconds = shared_clock->seconds;
                    if (next_launch_nanos >= 1000000000) {
                        next_launch_seconds++;
                        next_launch_nanos -= 1000000000;
                    }
                }
            }
        }
    }

    cout << "\nOSS: Simulation finished." << endl;
    cout << total_launched << " workers were launched and terminated." << endl;
    cout << "Workers ran for a combined time of " << shared_clock->seconds << " seconds and " << shared_clock->nanoseconds << " nanoseconds." << endl;

    cleanup();

    return 0;
}


