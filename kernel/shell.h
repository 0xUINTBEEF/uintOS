#ifndef SHELL_H
#define SHELL_H

// Constants
#define MAX_COMMAND_LENGTH 256
#define MAX_ARGS 16
#define COMMAND_HISTORY_SIZE 10

// Function declarations
void shell_init(void);
void shell_run(void);
void shell_execute_command(const char *command);
void shell_display_prompt(void);
void shell_print(const char *str);
void shell_println(const char *str);

// Command functions
void cmd_help(int argc, char *argv[]);
void cmd_clear(int argc, char *argv[]);
void cmd_echo(int argc, char *argv[]);
void cmd_meminfo(int argc, char *argv[]);
void cmd_memstat(int argc, char *argv[]);
void cmd_memtest(int argc, char *argv[]);
void cmd_taskinfo(int argc, char *argv[]);
void cmd_reboot(int argc, char *argv[]);
void cmd_ls(int argc, char *argv[]);
void cmd_cat(int argc, char *argv[]);
void cmd_vgademo(int argc, char *argv[]);
void cmd_log(int argc, char *argv[]); // New log command
void cmd_usb(int argc, char *argv[]); // New USB devices command

#endif // SHELL_H