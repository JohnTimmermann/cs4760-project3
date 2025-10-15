#include <iostream>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <sys/msg.h>
#include <string>
#include <unistd.h>
#include <signal.h>
#include "shared.h"
#include <fstream>
#include <sstream>

using namespace std;

struct PCB {
    int occupied;
    pid_t pid;
    int start_seconds;
    int start_nanos;
    int messagesSent;
};

const int MAX_PROCESSES = 20;

// Signal Handling Globals
int shared_memory_id;
int message_queue_id;
SimulatedClock* shared_clock = nullptr;
PCB* process_table_ptr = nullptr;
volatile sig_atomic_t terminate_flag = 0;
ofstream logfile;

void print_and_log(const string& message) {
    cout << message;
    if (logfile.is_open()) {
        logfile << message;
    }
}

void signal_handler(int signum) {
    ostringstream oss;
    oss << "\nOSS: Signal " << signum << " received. Initiating shutdown..." << endl;
    print_and_log(oss.str());
    terminate_flag = 1;
}

// Cleanup function for shared memory and child processes
void cleanup() {
    ostringstream oss;
    oss << "OSS: Parent process terminating. Cleaning up..." << endl;
    print_and_log(oss.str());

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
    if (message_queue_id > 0) msgctl(message_queue_id, IPC_RMID, NULL);
    if (logfile.is_open()) {
        logfile.close();
    }
    cout << "OSS: Cleanup complete." << endl;
}

int main(int argc, char* argv[]) {
    // Default values
    int proc = 5;
    int simul = 3;
    float time_limit = 4.5;
    float interval = 0.2;
    string log_filename = "logfile.txt";

    int opt;
    // Parse command-line options
    while ((opt = getopt(argc, argv, "hn:s:t:i:f:")) != -1) {
        switch (opt) {
            case 'h':
                cout << "Usage: ./oss [-n proc] [-s simul] [-t time_limit] [-i interval] [-f logfile]" << endl;
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
            case 'f':
                log_filename = optarg;
                break;
            default:
                cerr << "Usage: ./oss [-n proc] [-s simul] [-t time_limit] [-i interval] [-f logfile]" << endl;
                return 1;
        }
    }

    logfile.open(log_filename);
    if (!logfile.is_open()) {
        cerr << "Error opening log file: " << log_filename << endl;
        return 1;
    }

    // Print and log out the parameters passed
    {
        ostringstream oss;
        oss << "OSS Starting, PID:" << getpid() << " PPID:" << getppid() << endl;
        oss << "Called with:" << endl;
        oss << "-n " << proc << endl;
        oss << "-s " << simul << endl;
        oss << "-t " << time_limit << endl;
        oss << "-i " << interval << endl;
        print_and_log(oss.str());
    }

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

    message_queue_id = msgget(MSG_KEY, IPC_CREAT | 0666);
    if (message_queue_id < 0) {
        perror("msgget");
        msgctl(message_queue_id, IPC_RMID, NULL);
        return 1;
    }

    // Initialize the clock
    shared_clock->seconds = 0;
    shared_clock->nanoseconds = 0;
    {
        ostringstream oss;
        oss << "OSS: Initialized clock to " << shared_clock->seconds << "s and " << shared_clock->nanoseconds << "ns." << endl;
        print_and_log(oss.str());
    }

    // Initialize Process Table
    PCB process_table[MAX_PROCESSES];
    process_table_ptr = process_table; // For signal handler access
    for (int i = 0; i < MAX_PROCESSES; ++i) {
        process_table_ptr[i].occupied = 0; 
        process_table_ptr[i].messagesSent = 0;
    }

    // Loop Vars
    int total_launched = 0;
    int active_children = 0;
    int next_launch_seconds = 0;
    int next_launch_nanos = 0;
    long last_print_nanos = 0;
    int next_child_to_schedule = 0;
    int total_messages_sent = 0;
    srand(time(NULL));

    while ((total_launched < proc || active_children > 0) && !terminate_flag) {
        // Increment clock
        long increment_nanoseconds = (active_children > 0) ? (250000000 / active_children) : 10000000;
        shared_clock->nanoseconds += increment_nanoseconds;

        // Overflow logic
        if (shared_clock->nanoseconds >= 1000000000) {
            shared_clock->seconds++;
            shared_clock->nanoseconds -= 1000000000;
        }

        if (active_children > 0) {
            while (process_table_ptr[next_child_to_schedule].occupied == 0) {
                next_child_to_schedule = (next_child_to_schedule + 1) % MAX_PROCESSES;
            }

            pid_t child_pid = process_table_ptr[next_child_to_schedule].pid;

            message msg_to_child;
            msg_to_child.messageType = child_pid;
            msgsnd(message_queue_id, &msg_to_child, sizeof(msg_to_child.content), 0);
            {
                ostringstream oss;
                oss << "OSS: Sending message to worker in PCB slot " << next_child_to_schedule << " (PID: " << child_pid << ") at time " << shared_clock->seconds << ":" << shared_clock->nanoseconds << endl;
                print_and_log(oss.str());
            }

            // Wait for a reply
            message msg_from_child;
            msgrcv(message_queue_id, &msg_from_child, sizeof(msg_from_child.content), getpid(), 0);
            total_messages_sent++;
            {
                ostringstream oss;
                oss << "OSS: Receiving message from worker " << child_pid << " at time " << shared_clock->seconds << ":" << shared_clock->nanoseconds << endl;
                print_and_log(oss.str());
            }

            // Process the reply
            process_table_ptr[next_child_to_schedule].messagesSent++;
            if (msg_from_child.content == 0) {
                {
                    ostringstream oss;
                    oss << "OSS: Worker " << child_pid << " is planning to terminate." << endl;
                    print_and_log(oss.str());
                }
                waitpid(child_pid, NULL, 0); // Blocking wait to clean up child
                active_children--;
                process_table_ptr[next_child_to_schedule].occupied = 0; // Free the slot
            }

             // Advance to next child
            next_child_to_schedule = (next_child_to_schedule + 1) % MAX_PROCESSES;
        }

        // Print process table every 0.5 seconds
        long current_total_nanos = shared_clock->seconds * 1000000000L + shared_clock->nanoseconds;
        if (current_total_nanos - last_print_nanos >= 500000000) {
            last_print_nanos = current_total_nanos;
            ostringstream oss;
            oss << "\nOSS PID:" << getpid() << " SysClockS: " << shared_clock->seconds << " SysclockNano: " << shared_clock->nanoseconds << endl;
            oss << "Process Table:" << endl;
            oss << "Entry Occupied PID      StartS StartN  MessagesSent" << endl;
            for (int i = 0; i < MAX_PROCESSES; ++i) {
                oss << i << "\t" << process_table_ptr[i].occupied << "\t" << process_table_ptr[i].pid << "\t" << process_table_ptr[i].start_seconds << "\t" << process_table_ptr[i].start_nanos << "\t" << process_table_ptr[i].messagesSent << endl;
            }
            oss << endl;
            print_and_log(oss.str());
        }

        // Launch new child if possible
        bool can_launch = (total_launched < proc) && (active_children < simul);
        bool time_to_launch = (shared_clock->seconds > next_launch_seconds || (shared_clock->seconds == next_launch_seconds && shared_clock->nanoseconds >= next_launch_nanos));

        if (can_launch && time_to_launch) {
            int process_index = -1;

            // Find first empty slot in PCB
            for (int i = 0; i < MAX_PROCESSES; ++i) {
                if (process_table_ptr[i].occupied == 0) {
                    process_index = i;
                    break;
                }
            }

            // If we found an empty slot
            if (process_index != -1) {
                total_launched++;
                active_children++;
                pid_t child_pid = fork();

                // New child process
                if (child_pid == 0) {
                    int random_second = rand() % (int)time_limit + 1;
                    int random_nano = rand() % 1000000000;
                    string lifetime_seconds = to_string(random_second);
                    string lifetime_nanoseconds = to_string(random_nano);
                    execlp("./worker", "worker", lifetime_seconds.c_str(), lifetime_nanoseconds.c_str(), NULL);
                    perror("execlp");
                    exit(1);
                } else {
                    // Parent process
                    // Update the process table with childs info.
                    process_table_ptr[process_index].occupied = 1;
                    process_table_ptr[process_index].pid = child_pid;
                    process_table_ptr[process_index].start_seconds = shared_clock->seconds;
                    process_table_ptr[process_index].start_nanos = shared_clock->nanoseconds;
                    process_table_ptr[process_index].messagesSent = 0;

                    {
                        ostringstream oss;
                        oss << "OSS: Launched child " << child_pid << " into PCB slot " << process_index << " at " << shared_clock->seconds << "s " << shared_clock->nanoseconds << "ns." << endl;
                        print_and_log(oss.str());
                    }

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

    {
        ostringstream oss;
        oss << "\nOSS: Simulation finished." << endl;
        oss << total_launched << " workers were launched and terminated." << endl;
        oss << total_messages_sent << " total messages were sent from OSS." << endl;
        oss << "Workers ran for a combined time of " << shared_clock->seconds << " seconds and " << shared_clock->nanoseconds << " nanoseconds." << endl;
        print_and_log(oss.str());
    }

    cleanup();
    return 0;
}
