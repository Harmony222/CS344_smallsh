// Author: Harmony Scarlet
// Date: 10/20/20
// Description: 
//   smallsh is a shell program written in C with the following features:
//      1. Provide a prompt for running commands
//      2. Handle blank lines and comments, which are lines beginning with 
//         the # character
//      3. Provide expansion for the variable $$
//      4. Execute 3 commands exit, cd, and status via code built into the shell
//      5. Execute other commands by creating new processes using a function
//         from the exec family of functions
//      6. Support input and output redirection
//      7. Support running commands in foreground and background processes
//      8. Implement custom handlers for 2 signals, SIGINT and SIGTSTP


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

struct command_line {
    char *command;
    char **args;    // max of 512 arguments
    int args_count;
    char *input_file;
    char *output_file;
    bool run_in_background;
};

char *get_command_line();
char *variable_expansion(char *command_line_str);
struct command_line *parse_command_line(char *command_line_str);
void initialize_struct(struct command_line *command_line_parsed);
void handle_command_line(struct command_line *command_line_parsed, int *status,
                         int *background_procs, int *bg_proc_count);
void display_status(int *status);
void change_dir(char *envpath);
char *get_cwd();
void check_background_procs(int *background_procs, int *bg_proc_count, int *status);
void remove_val_at_index(int *arr, int *arr_length, int index);
void fork_child(struct command_line *command_line, int *status, 
                int *background_procs, int *bg_proc_count);
void input_redirect(struct command_line *command_line, int *status);
void output_redirect(struct command_line *command_line, int *status);
void ignore_SIGINT();
void restore_SIGINT();
void handle_SIGTSTP(int signo);
void signal_handling();
void ignore_SIGTSTP();
void kill_children(int *background_procs, int bg_proc_count);
void free_memory(struct command_line *command_line_parsed);
void print_command_line(struct command_line *command_line_parsed);

// Info on sig_atomic_t: 
// https://wiki.sei.cmu.edu/confluence/display/c/SIG31-C.+Do+not+access+shared+objects+in+signal+handlers
volatile sig_atomic_t foreground_only = 0; // global variable for handling SIGTSTP signal


/*******************************************************************************
Main() performs the following tasks:
- sets up signal handling
- gets command line from user
- processes command line (variable expansion and parse into command struct)
- calls function to handle command line
- checks for completion of background processes
- frees memory
*******************************************************************************/
int main() {
    ignore_SIGINT();    // parent and background processes ignore SIGINT 
    signal_handling();  // setup signal handler for SIGTSTP
    int status = 0;
    char *command_line_str = NULL;  // used to read command line from user
    char *command_line_expanded;    // points to string after variable expansion
    struct command_line *command_line_parsed;
    // printf("smallsh program PID = %d\n", getpid());
    // allocate memory for array to hold PIDs of background processes PIDs
    int *background_procs = calloc(100, sizeof(int));     
    int bg_proc_count = 0;    // length of background_procs array
    // outer do/while loop checks for exit command
    do {
        // inner do/while loop checks background processes and gets command line
        // from user until a non-empty/non-comment command is given
        do {
            // free previous command_line_str
            free(command_line_str);
            // check status of background processes
            check_background_procs(background_procs, &bg_proc_count, &status);
            command_line_str = get_command_line();
        } while (isspace(command_line_str[0]) | (command_line_str[0] == '#'));

        // if first command is not exit, perform variable expansion, parse the
        // command line, and then handle the command line
        if (strcmp("exit\n", command_line_str)) {
            command_line_expanded = variable_expansion(command_line_str);
            command_line_parsed = parse_command_line(command_line_expanded);
            // print_command_line(command_line_parsed);
            handle_command_line(command_line_parsed, &status, 
                                background_procs, &bg_proc_count);
            free(command_line_expanded);
            free_memory(command_line_parsed);
        }
    } while (strcmp("exit\n", command_line_str));
    // exit
    // when exit is run, shell must kill any other processes or jobs that the
    // shell has started before terminating
    kill_children(background_procs, bg_proc_count);
    free(background_procs);
    // free final command_line_str
    free(command_line_str);
    // printf("the process with PID %d is returning from main\n", getpid());
    return 0;
}

/******************************************************************************
get_command_line prompts user and gets command_line string:
- Display ": " prompt.
- Use getline() to read the command line string entered by the user.
- Return command line string.
******************************************************************************/
char *get_command_line() {
    char *buffer = NULL;  // used to read command line from user
    size_t len = 0;       // used for getline()
    ssize_t lread;                  
    printf(": ");
    fflush(stdout);
    lread = getline(&buffer, &len, stdin);
    if (lread == -1) {
        printf("error reading line\n");
    }
    return buffer;
}

/******************************************************************************
Expands any instance of "$$" in a command into the process ID of the smallsh
program. The expanded command has max length of 2048 characters per assignment
specs.
******************************************************************************/
char *variable_expansion(char *command_line_str) {
    // allocate memory for new string after variable expansion
    char *command_line_expanded = calloc(2048, sizeof(char));        
    char *var = "$$";       // variable to expand
    char *return_string;    // point to substring returned from strstr
    char *str_pointer = command_line_str;  // keep track of position in command_line_str
    char *pid = calloc(sizeof(char), 100); // used to convert PID to string
    // convert PID to string
    sprintf(pid, "%d", getpid());
    // printf("pid = %s; length of pid = %lu\n", pid, strlen(pid));
    // search for var in string pointed to by str_pointer
    return_string = strstr(str_pointer, var);
    // printf("substring = %s\n", return_string);
    // continue to search for var and replace with PID, move str_pointer down
    // the command_line string
    while (return_string != NULL) {
        // concatenate the string up until the var is found
        strncat(command_line_expanded, str_pointer, (strlen(str_pointer) - strlen(return_string)));
        // concatenate the PID (to replace var)
        strcat(command_line_expanded, pid);
        // advance pointer to position after found var
        str_pointer += strlen(str_pointer) - strlen(return_string) + strlen(var);
        // search for next var and assign substring to return_string
        return_string = strstr(str_pointer, var);
    }
    // add on the last of the string after final $$ is found (-1 to remove 
    // newline char)
    strncat(command_line_expanded, str_pointer, strlen(str_pointer) - 1);
    free(pid);
    return command_line_expanded;
}

/******************************************************************************
Parse the command line. Use strtok_r to get tokens, check for special symbols,
and store command in array. Save all command line data to command_line struct.
Does not do any error checking on the command line (per assignment specs).
Parameters: command_line string
Returns: command_line struct
******************************************************************************/
struct command_line *parse_command_line(char *command_line_str) {
    // allocate memory for parsed command line struct
    struct command_line *command_line_parsed = malloc(sizeof(struct command_line));
    // initialize the new command line struct to NULL/0 values
    initialize_struct(command_line_parsed);
    
    char *saveptr1;
    // First token is the command, add command to command_line struct
    char *token = strtok_r(command_line_str, " ", &saveptr1);
    command_line_parsed->command = calloc(strlen(token) + 1, sizeof(char));
    strcpy(command_line_parsed->command, token);
    // allocate memory for arg list (per assignment specs, max 512 arguments)
    char **args_list = malloc(512 * sizeof(*args_list));
    // allocate memory and copy in 1st token (command) to args_list
    args_list[0] = calloc(strlen(token) + 1, sizeof(char));
    strcpy(args_list[0], token);
    int args_count = 1;     // keep track of length of args_list
    // Get next token before entering while loop
    token = strtok_r(NULL, " ", &saveptr1);
    while (token) {
        // printf("token = %s, token length = %lu\n", token, strlen(token));
        if ((token[0] == '<') & (strlen(token) == 1)) {
            // if < found, get next token which will be input_file and copy
            // to command_line struct
            token = strtok_r(NULL, " ", &saveptr1);
            command_line_parsed->input_file = calloc(strlen(token) + 1, sizeof(char));
            strcpy(command_line_parsed->input_file, token);
        }
        else if ((token[0] == '>') & (strlen(token) == 1)) {
            // if > found, get next token which will be output_file and copy
            // to command_line struct
            token = strtok_r(NULL, " ", &saveptr1);
            command_line_parsed->output_file = calloc(strlen(token) + 1, sizeof(char));
            strcpy(command_line_parsed->output_file, token);
        }
        else {
            // if none of the above apply, add token to arg_list array and 
            // increment args_count
            args_list[args_count] = calloc(strlen(token) + 1, sizeof(char));
            strcpy(args_list[args_count], token);
            args_count++;
        }
        // get next token and repeat loop
        token = strtok_r(NULL, " ", &saveptr1);
    }
    // if there is at least two args, check the final args_list position for
    // & character, if found update command_line struct member and 'delete'
    // & from args_list
    if (args_count > 1) {
        if (!strcmp(args_list[args_count - 1], "&")) {
            command_line_parsed->run_in_background = true;
            free(args_list[args_count - 1]);
            args_count--;  
        }
    }
    // assign args list and count to command_line struct
    command_line_parsed->args = args_list;
    command_line_parsed->args_count = args_count;
        
    return command_line_parsed;
}

/******************************************************************************
Initialize a new command_line struct, this cleared several read memory warnings
The warnings were with the printf and free memory functions since the 
parse_command line function does not assign a value to all of the structure
members.
******************************************************************************/
void initialize_struct(struct command_line *command_line_parsed) {
    command_line_parsed->command = NULL;
    command_line_parsed->args = NULL;
    command_line_parsed->args_count = 0;
    command_line_parsed->input_file = NULL;
    command_line_parsed->output_file = NULL;
    command_line_parsed->run_in_background = 0;
}


/******************************************************************************
Handle the command from the comand line. 
Built in commands "cd" and "status" are passed to their respective functions.
All other commands are sent to fork_child function to process.
Before forking, add NULl to end of args list
******************************************************************************/
void handle_command_line(struct command_line *command_line, int *status,
                         int *background_procs, int *bg_proc_count) 
{
    if (!strcmp(command_line->command, "cd")) {
        // Handle "cd" command
        if (command_line->args_count == 1) {
            // change directory to Home environment variable if "cd" is
            // the only arg in args_list
            change_dir(getenv("HOME"));
        } else {
            // change directory to path specified after "cd" command
            change_dir(command_line->args[1]);
        }
    }
    else if (!strcmp(command_line->command, "status")) {
        // handle status command    
        display_status(status);
    }
    else {
        // add NULL to args list
        command_line->args[command_line->args_count] = NULL;
        command_line->args_count += 1;
        // print_command_line(command_line);
        fork_child(command_line, status, background_procs, bg_proc_count);
    }
}

/******************************************************************************
Displays either the exit status or the terminating signal of the last
ran foreground command. 
If status is 0 or 1, print exit status message.
If status is > 1, print terminating signal message.
******************************************************************************/
void display_status(int *status) {
    if ((*status == 0) || (*status == 1)) {
        printf("exit value %d\n", *status);
        fflush(stdout);
    } else {
        printf("terminated by signal %d\n", *status);
        fflush(stdout);
    }
}

/******************************************************************************
Change cwd to path specified.
Free memory allocated from getcwd call after changing directory.
******************************************************************************/
void change_dir(char *envpath) {
    int change_dir_num = chdir(envpath);
    // On chdir success, zero is returned.  On error, -1 is returned
    if (change_dir_num == -1) {
        printf("Error changing directories.\n");
        fflush(stdout);
    }
    char *cwd = get_cwd();
    // printf("cwd after change dir: %s\n", cwd);
    free(cwd);
}

/******************************************************************************
Get and return the current working directory.
https://man7.org/linux/man-pages/man3/getcwd.3.html
    "glibc's getcwd() allocates the buffer dynamically using malloc(3) if buf
     is NULL.  In this case, the allocated buffer has the length size unless
     size is zero, when buf is allocated as big as necessary.  The caller 
     should free(3) the returned buffer.
******************************************************************************/
char *get_cwd() {
    char *cwd;
    char *buffer = NULL;
    size_t len = 0;
    cwd = getcwd(buffer, len);
    return cwd;
}

/******************************************************************************
Iterate through background_procs array and use waitpid on each process PID. 
Display message if process is complete with process exit status. 
Remove process from array if it is complete.
Basic structure of WIFEXITED code modified from course exploration Process API
 - Monitoring Child Processes
******************************************************************************/
void check_background_procs(int *background_procs, int *bg_proc_count, int *status) {
    int i;
    int arr_length = *bg_proc_count;
    int pid_check;
    int child_status;
    for (i = 0; i < arr_length; i++) {
        // printf("background proc PID: %d\n", background_procs[i]);
        pid_check = waitpid(background_procs[i], &child_status, WNOHANG);
        // printf("child_status: %d, pid_check: %d\n", child_status, pid_check);
        // if waitpid returnes the child pid, the child process is complete and
        // can be removed from the background_procs list
        if (pid_check == background_procs[i]) {
            // remove PID from background_procs list
            remove_val_at_index(background_procs, bg_proc_count, i);
            // roll back i by one if a value was removed 
            i -= 1;
            // display background PID status and exit value or signal termination
            if(WIFEXITED(child_status)) {
                printf("background pid %d is done: exit value %d\n", pid_check, WEXITSTATUS(child_status));
                fflush(stdout);
            } else {
                printf("background pid %d is done: terminated by signal %d\n", pid_check, WTERMSIG(child_status));
                fflush(stdout);
            }
        }
    }
}

/******************************************************************************
Removes the value from the given array at the specified index.
Paramaters: ptr to array, ptr to array length, index of val to be removed
******************************************************************************/
void remove_val_at_index(int *arr, int *arr_length, int index) {
    int i;
    for (i = index; i < *arr_length - 1; i++) {
        arr[i] = arr[i + 1];
    }
    *arr_length -= 1;
}

/******************************************************************************
Fork a child process.
First add forked child to background_proc array if process to run in background
In child 
    - restore SIGINT for foreground processes, ignore SIGTSTP for both 
      foreground and background processes
    - call redirect input/output functions
    - use execvp to run command with args
In parent
    - print statement if running in background
    - wait for process if running in foreground only and then check exit 
      status of foreground process
Basic fork structure code modified from course exploration Executing a New 
Program.
Basic structure of WIFEXITED code modified from course exploration Monitoring 
Child Processes
******************************************************************************/
void fork_child(struct command_line *command_line, int *status, 
                int *background_procs, int *bg_proc_count) 
{
    int child_status;
    pid_t spawn_pid = fork();

    // If process to run in background and program is NOT in foreground 
    // only mode, add child PID to background_proc array
    if (command_line->run_in_background & !foreground_only) {
        background_procs[*bg_proc_count] = spawn_pid;
        *bg_proc_count += 1;
    }
    switch(spawn_pid) {
        case -1:
            perror("fork()\n");
            exit(1);
            break;
        case 0: ;
            // child process
            // printf("testing child process pid = %d\n", getpid());
            // If process is to run in foreground, restore SIGINT
            if (!command_line->run_in_background || !foreground_only) {
                restore_SIGINT();
            }
            // Child processes ignore SIGTSTP
            ignore_SIGTSTP();
            // Setup input and output redirection
            input_redirect(command_line, status);
            output_redirect(command_line, status);
            // printf("Child %d running %s command\n", getpid(), command_line->command);
            execvp(command_line->args[0], command_line->args);
            // perror and exit are only reached if execvp fails
            // perror("execvp");
            perror(command_line->args[0]);
            exit(1);
            break;
        default:
            // Parent process
            if (command_line->run_in_background & !foreground_only) {
                // run child in background, do not wait for child to terminate
                printf("background PID is %d\n", spawn_pid);
                fflush(stdout);
            } else {
                // run child in foreground, wait for child to terminate
                // printf("run proces in foreground child pid: %d\n", spawn_pid);
                spawn_pid = waitpid(spawn_pid, &child_status, 0);
                // printf("spawn_pid after waitpid: %d; child_status: %d\n", spawn_pid, child_status);
                // check exit status of foreground process
                if(WIFEXITED(child_status)) {
                    // printf("pid %d is done: exit value %d\n", spawn_pid, WEXITSTATUS(child_status));
                    *status = WEXITSTATUS(child_status);
                } else {
                    printf("terminated by signal %d\n", WTERMSIG(child_status));
                    fflush(stdout);
                    *status = WTERMSIG(child_status);
                }
            }
            break;
    }
}

/******************************************************************************
If input_file specified in command_line, open file and use dup2() for input
redirection.
If command is to run in background and a file is not specified, redirect input
to /dev/null per assignment description.
If error, exit with exit(1) to communicate to parent process that there was an
error.
Code for error handling modified from course exploration Processes and I/O.
******************************************************************************/
void input_redirect(struct command_line *command_line, int *status) {
    if (command_line->input_file) {
        // open input file
        int input_fd = open(command_line->input_file, O_RDONLY);
        if (input_fd == -1) {
            printf("cannot open %s for input\n", command_line->input_file);
            // perror("source open()");
            fflush(stdout);
            exit(1);
        }
        // redirect stdin to input file
        int result = dup2(input_fd, 0);
        if (result == -1) {
            printf("error redirecting stdin to input file\n");
            // perror("source dup2()");
            fflush(stdout);
            exit(1);
        }
    }
    if (command_line->run_in_background & !foreground_only
            & !command_line->input_file) 
    {
        int input_fd = open("/dev/null", O_RDONLY);
        if (input_fd == -1) {
            printf("cannot open /dev/null for input\n");
            // perror("source open()");
            fflush(stdout);
            exit(1);
        }
        int result = dup2(input_fd, 0);
        if (result == -1) {
            printf("error redirecting stdin to /dev/null\n");
            // perror("source dup2()");
            fflush(stdout);
            exit(1);
        }
    }
}

/******************************************************************************
If output_file specified in command_line, open file and use dup2() for output
redirection.
If command is to run in background and a file is not specified, redirect output
to /dev/null per assignment description.
If error, exit with exit(1) to communicate to parent process that there was an
error.
Code for error handling modified from exploration Processes and I/O.
******************************************************************************/
void output_redirect(struct command_line *command_line, int *status) {
    if (command_line->output_file) {
        // open output file
        int output_fd = open(command_line->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (output_fd == -1) {
            printf("cannot open %s for output\n", command_line->output_file);
            fflush(stdout);
            // perror("target open()");
            exit(1);
        }
        // redirect stdout to output file
        int result = dup2(output_fd, 1);
        if (result == -1) {
            printf("error redirecting stdout to output file\n");
            fflush(stdout);
            // perror("target dup2()");
            exit(1);
        }
    }
    if (command_line->run_in_background & !foreground_only
            & !command_line->output_file) 
    {
        int output_fd = open("/dev/null", O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (output_fd == -1) {
            printf("cannot open /dev/null for output\n");
            fflush(stdout);
            // perror("target open()");
            exit(1);
        }
        int result = dup2(output_fd, 1);
        if (result == -1) {
            printf("error redirecting stdout to /dev/null\n");
            fflush(stdout);
            // perror("target dup2()");
            exit(1);
        }
    }
}

/******************************************************************************
Sets SIG_IGN as the hander for SIGINT so that SIGINT is ignored.
Code to setup SIG_IGN adapted from course exploration example in Signal
Handling API.
******************************************************************************/
void ignore_SIGINT() {
    // Initialize ignore_action struct to be empty
    struct sigaction ignore_action = {{0}};
    // Assign signal handerl SIG_IGN to ignore_action struct
    ignore_action.sa_handler = SIG_IGN;
    // Reigster ignore_action as the signal handler for SIGINT
    sigaction(SIGINT, &ignore_action, NULL);
}

/******************************************************************************
Sets SIG_DFL as the hander for SIGINT so that SIGINT is no longer ignored.
******************************************************************************/
void restore_SIGINT() {
    // Initialize restore_action struct to be empty
    struct sigaction restore_action = {{0}};
    // Assign signal handerl SIG_DFL to restore_action struct
    restore_action.sa_handler = SIG_DFL;
    // Reigster restore_action as the signal handler for SIGINT
    sigaction(SIGINT, &restore_action, NULL);
}

/******************************************************************************
Function that handles SIGTSTP.
Parent process SIGTSTP handling: signals shell to display message and enter or
exit a state where subsequent commands can no longer be run in the background.
(The & operator is ignored in this state and commands are run as foreground
processes.)
Switches foreground-only mode on and off and
prints message to user about entering/exiting foreground-only mode.
Uses global variable to switch on/off.
******************************************************************************/
void handle_SIGTSTP(int signo) {
    // char *message = ("Caught SIGTSTP\n");
    // write(STDOUT_FILENO, message, 15);
    if (foreground_only) {
        char *message2 = "\nExiting foreground-only mode\n: ";
        write(STDOUT_FILENO, message2, 32);
        fflush(stdout);
        foreground_only = 0; // foreground only mode = off/false
    } else {
        char *message3 = "\nEntering foreground-only mode (& is now ignored)\n: ";
        write(STDOUT_FILENO, message3, 53);
        fflush(stdout);
        foreground_only = 1; // foreground only mode = on/true
    }
}

/******************************************************************************
Initialize SIGTSTP_action struct and assign struct members
Code adapted from course exploration Signal Handling API
******************************************************************************/
void signal_handling() {
    // Initialize SIGTSTP_action to be empty
    struct sigaction SIGTSTP_action = {{0}};
    // Register handle_SIGSTP as the signal handelr
    SIGTSTP_action.sa_handler = handle_SIGTSTP;
    // Block all catchable signals whiel handle_SIGTSTP is running
    sigfillset(&SIGTSTP_action.sa_mask);
    // use SA_RESTART flag to cause an automatic restart of the interuppted 
    // system call/library function after the signal handler is done (getline()
    // returns an error if its interuppted by a signal, use SA_RESTART to 
    // restart the getlien() process)
    SIGTSTP_action.sa_flags = SA_RESTART;
    // Install the signal handler
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);
}

/******************************************************************************
Child foreground and background processes ignore SIGTSTP.
Sets SIG_IGN as the hander for SIGTSTP so that SIGTSTP is ignored.
Code to setup SIG_IGN adapted from course exploration example in Signal
Handling API.
******************************************************************************/
void ignore_SIGTSTP() {
    // Initialize ignore_action struct to be empty
    struct sigaction ignore_action = {{0}};
    // Assign signal handerl SIG_IGN to ignore_action struct
    ignore_action.sa_handler = SIG_IGN;
    // Reigster ignore_action as the signal handler for SIGTSTP
    sigaction(SIGTSTP, &ignore_action, NULL);
}

/******************************************************************************
Iterate through all of the PIDs in the background processes array and
1. kill the process
2. call waitpid() on the PID to clear it, no zombies today please
******************************************************************************/
void kill_children(int *background_procs, int bg_proc_count) {
    int child_status;
    int pid_check;
    for (int i = 0; i < bg_proc_count; i++) {
        kill(background_procs[i], SIGKILL);
        pid_check = waitpid(background_procs[i], &child_status, WNOHANG);
        // printf("Killed child process %d\n", pid_check);
        // if(WIFEXITED(child_status)) {
        //     printf("background pid %d is done: exit value %d\n", pid_check, WEXITSTATUS(child_status));
        // } else {
        //     printf("background pid %d is done: terminated by signal %d\n", pid_check, WTERMSIG(child_status));
        // }
    }
}

/******************************************************************************
Frees memory allocated for command_line_parsed struct, frees each string in
the args array.
******************************************************************************/
void free_memory(struct command_line *command_line_parsed) {
    int i;
    free(command_line_parsed->command);
    free(command_line_parsed->input_file);
    free(command_line_parsed->output_file);
    for (i = 0; i < command_line_parsed->args_count; i++) {
        free(command_line_parsed->args[i]);
    }
    free(command_line_parsed->args);
    free(command_line_parsed);
}

/******************************************************************************
Prints the parsed command line (use for debugging purposes only)
******************************************************************************/
void print_command_line(struct command_line *command_line_parsed) {
    printf("command line command: %s -", command_line_parsed->command);
    printf(" input file: %s -", command_line_parsed->input_file);
    printf(" output file: %s -", command_line_parsed->output_file);
    printf(" run in background? %d -", command_line_parsed->run_in_background);
    printf(" args count: %d -", command_line_parsed->args_count);
    printf(" args: [");
    for (int i = 0; i < command_line_parsed->args_count; i++) {
        printf("%s, ", command_line_parsed->args[i]);
    }
    printf("]\n");
    fflush(stdout);
}