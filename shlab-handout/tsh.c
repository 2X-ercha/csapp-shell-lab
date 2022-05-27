/*
 * tsh - A tiny shell program with job control
 *
 * <Put your name and login ID here>
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

/*
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {              /* The job struct */
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */
/* End global variables */


/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv);
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs);
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid);
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid);
int pid2jid(pid_t pid);
void listjobs(struct job_t *jobs);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

/*
 * main - The shell's main routine
 */
int main(int argc, char **argv)
{
    char c;
    char cmdline[MAXLINE];
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
        case 'h':             /* print help message */
            usage();
	    break;
        case 'v':             /* emit additional diagnostic info */
            verbose = 1;
	    break;
        case 'p':             /* don't print a prompt */
            emit_prompt = 0;  /* handy for automatic testing */
	    break;
	default:
            usage();
	}
    }

    /* Install the signal handlers */

    /* These are the ones you will need to implement */
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler);

    /* Initialize the job list */
    initjobs(jobs);

    /* Execute the shell's read/eval loop */
    while (1) {

	/* Read command line */
	if (emit_prompt) {
	    printf("%s", prompt);
	    fflush(stdout);
	}
	if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
	    app_error("fgets error");
	if (feof(stdin)) { /* End of file (ctrl-d) */
	    fflush(stdout);
	    exit(0);
	}

	/* Evaluate the command line */
	eval(cmdline);
	fflush(stdout);
	fflush(stdout);
    }

    exit(0); /* control never reaches here */
}

/*
 * eval - Evaluate the command line that the user has just typed in
 *
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.
*/
/*
trace01.txt – 在 EOF 处正确地停止。
  eval 函数是根据输入命令行进行对应操作的函数。在 CSAPP 2e 8.4.6 节（中文版 P502 / 英文版 P733），有 eval 函数的大致框架。
  trace01 只需要正确响应 EOF 就可以了。在课本的程序框架中，已经使用 parseline 函数将命令行解析为了参数，并判断了第一个参数是否为 NULL。

  显然，第一个参数是 NULL，后面当然更是 NULL，意味着没有输入。也就是说，我们什么额外的工作都不用干……

trace03.txt – 运行一个前台任务。
trace04.txt – 运行一个后台任务。
  在课本的 eval 框架中，首先使用 builtin_cmd 判断并处理内置函数，然后根据 bg 值（由 parseline 得到）判断是否在后台运行。
  这其中会先 fork 出一个子进程，然后使用 execve 执行目标程序。
  请注意，只有子进程会在运行失败时 exit。根据是否是前台任务，判断是直接打印执行详情还是等待前台进程结束。

  所以还是啥都不用干。。。(job不一致，trace05解决)

trace05.txt – 处理 jobs 内置命令。
运行 jobs 都不会输出任何东西。毕竟在我们已有的代码里，既没有在任务开始时将其添加到任务列表，也没有在结束时将它移出。
要做到将任务添加到任务列表，需要在 fork 后调用 addjob
如果当前任务需要在前台运行，需要等待前台任务（如果有）结束，但 tsh 有一个 waitfg 函数，看起来他们不想让我们把等待工作放到 eval 中。
另外还要处理 eval 中的信号问题。Writeup 中提到
  > 在 eval 中，父进程在 fork 子进程之前，必须使用 sigprocmask 函数来阻断 SIGCHLD 信号，然后在使用 addjob 将子进程加入任务列表之后，再调用 sigprocmask 恢复 SIGCHLD 信号。因为子进程继承了父进程的中断向量，所以子进程必须在它执行新程序之前将 SIGCHILD 恢复。
  > 父进程这样将 SIGCHLD 信号阻断，是为了避免子进程被 SIGCHLD 处理程序回收（然后被从任务列表中移除），之后父进程调用 addjob 时的竞态条件。
*/
void eval(char *cmdline)
{
    char *argv[MAXARGS];
    char buf[MAXLINE]; // command will be parsed and modified?
    int bg;            // whether it runs in background
    pid_t pid;

    // trace05 add
    sigset_t mask;
    sigemptyset(&mask);

    // preprocess cmd line
    strcpy(buf, cmdline);
    bg = parseline(buf, argv); // convert the command into argv
    if (argv[0] == NULL)       // the line is empty, return
    {
        return;
    }
    // run external command
    if (!builtin_cmd(argv))
    {
        // trace05 add
        sigaddset(&mask, SIGCHLD);
        sigprocmask(SIG_BLOCK, &mask, NULL); // 阻断 SIGCHLD 信号

        if ((pid = fork()) == 0) // this is child
        {
            // trace05 add
            sigprocmask(SIG_UNBLOCK, &mask, NULL); // 恢复信号

            if (execve(argv[0], argv, environ) < 0) // execute command failed
            {
                printf("%s: Command not found\n", argv[0]);
                exit(0); // here only child exited
            }
        }
        addjob(jobs, pid, bg ? BG : FG, cmdline); // add the job to job list.

        // trace05 add
        sigprocmask(SIG_UNBLOCK, &mask, NULL); // 父进程 addjob 完毕后也要恢复

        // 代码的 addjob 中第三个参数 state 有三个取值，FG=1、BG=2、ST=3。虽然直接使用 bg+1 也是可行的方案，但这样使用三元运算符会更优雅更容易理解。
        if (!bg) // run the process in foreground:
        // wait for foreground job to terminate
        {
            waitfg(pid); // the waiting stuff should be done in `waitfg`
            /* trace05 修改
            int status;
            if (waitpid(pid, &status, 0) < 0) // if return -1, then waiting failed
            {
                unix_error("waitfg: waitpid error");
            }
            */
        }
        else
        {
            printf("[%d] (%d) %s", pid2jid(pid), pid, cmdline);
        }
    }
    return;
}

/*
 * parseline - Parse the command line and build the argv array.
 *
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.
 */
int parseline(const char *cmdline, char **argv)
{
    static char array[MAXLINE]; /* holds local copy of command line */
    char *buf = array;          /* ptr that traverses command line */
    char *delim;                /* points to first space delimiter */
    int argc;                   /* number of args */
    int bg;                     /* background job? */

    strcpy(buf, cmdline);
    buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */
    while (*buf && (*buf == ' ')) /* ignore leading spaces */
        buf++;

    /* Build the argv list */
    argc = 0;
    if (*buf == '\'') {
	      buf++;
	      delim = strchr(buf, '\'');
    }
    else
    {
	      delim = strchr(buf, ' ');
    }

    while (delim)
    {
	      argv[argc++] = buf;
	      *delim = '\0';
	      buf = delim + 1;
    	  while (*buf && (*buf == ' ')) /* ignore spaces */
    	       buf++;
    	  if (*buf == '\'') {
    	       buf++;
    	       delim = strchr(buf, '\'');
    	  }
        else {
    	       delim = strchr(buf, ' ');
    	  }
    }
    argv[argc] = NULL;

    if (argc == 0)  /* ignore blank line */
        return 1;

    /* should the job run in the background? */
    if ((bg = (*argv[argc-1] == '&')) != 0) {
        argv[--argc] = NULL;
    }
    return bg;
}

/*
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.
 */
/*
trace02.txt – 处理内置的 quit 命令。
在 tsh 中，内置的命令实在 builtin_cmd 函数中处理的。只需要在其中判断一下第一个参数是否为 quit，如果是的话退出即可。

trace05.txt – 处理 jobs 内置命令。
将处理 jobs 命令的地方做好。tsh 已经为我们实现了 listjobs 函数，可以直接放到 builtin_cmd 中。
注意 return 1 用来告诉 eval 已经找到了一个内置命令，否则会提示 “command not found”。
*/
int builtin_cmd(char **argv)
{
    if (strcmp(argv[0], "quit") == 0) // process quit command
      exit(0);

    if (strcmp(argv[0], "jobs") == 0) // t5: process jobs command
    {
        listjobs(jobs);
        return 1; // this IS a builtin command, return 1 to notify
    }

    return 0;
}

/*
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv)
{
    return;
}

/*
 * waitfg - Block until process pid is no longer the foreground process
 */
/*
trace05.txt – 处理 jobs 内置命令。
Writeup 提示
  > 实验有一个棘手的部分，是决定 waitfg 和 sigchld 处理函数之间的工作分配。我们推荐以下方法：
  > – 在 waitfg 中，用一个死循环包裹 sleep 函数。
  > – 在 sigchild_handler 中，调用且仅调用一次 waitpid。
*/
void waitfg(pid_t pid)
{
    while (pid == fgpid(jobs))
    {
        sleep(0);
    }
    return;
}

/*****************
 * Signal handlers
 *****************/

/*
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.
 */
/*
trace05.txt – 处理 jobs 内置命令。
涉及到对 waitpid 函数的更深入理解，在 CSAPP 8.4.3 节（中文版 P496，英文版 P724），提到了这样一段话，介绍了 waitpid 函数的默认行为，以及退出状态的检查方法
  > （更改默认行为）WNOHANG|WUNTRACED：立即返回。如果等待集里面没有子进程已经终止，那么返回 0；否则，返回其中一个已终止子进程的 PID。
  > （检查回收子进程的返回状态）WIFEXITED(status)：如果子进程正常退出，即通过调用 exit 或者 return，则返回真。
Writeup 中也提到 WNOHANG|WUNTRACED 或许会有用。
前一个更改默认行为的部分对应了 waitpid 的第三个参数，后一个检查用于确定是否有个子进程真的退出了（而非没有子进程终止，waitpid 返回了 0）
*/
void sigchld_handler(int sig)
{
    pid_t pid;
    int status;
    while((pid=waitpid(-1,&status,WNOHANG|WUNTRACED))>0) // check if a child has become zombie, without wait
    {
        if(WIFEXITED(status))
        {
            deletejob(jobs,pid); // remove pid from job list
        }
    }
    return;
}

/*
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.
 */
void sigint_handler(int sig)
{
    return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.
 */
void sigtstp_handler(int sig)
{
    return;
}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs)
{
    int i, max=0;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid > max)
	    max = jobs[i].jid;
    return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline)
{
    int i;

    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == 0) {
	    jobs[i].pid = pid;
	    jobs[i].state = state;
	    jobs[i].jid = nextjid++;
	    if (nextjid > MAXJOBS)
		nextjid = 1;
	    strcpy(jobs[i].cmdline, cmdline);
  	    if(verbose){
	        printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
            }
            return 1;
	}
    }
    printf("Tried to create too many jobs\n");
    return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid)
{
    int i;

    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == pid) {
	    clearjob(&jobs[i]);
	    nextjid = maxjid(jobs)+1;
	    return 1;
	}
    }
    return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].state == FG)
	    return jobs[i].pid;
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid)
	    return &jobs[i];
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid)
{
    int i;

    if (jid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid == jid)
	    return &jobs[i];
    return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid)
{
    int i;

    if (pid < 1)
	return 0;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid) {
            return jobs[i].jid;
        }
    return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs)
{
    int i;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid != 0) {
	    printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
	    switch (jobs[i].state) {
		case BG:
		    printf("Running ");
		    break;
		case FG:
		    printf("Foreground ");
		    break;
		case ST:
		    printf("Stopped ");
		    break;
	    default:
		    printf("listjobs: Internal error: job[%d].state=%d ",
			   i, jobs[i].state);
	    }
	    printf("%s", jobs[i].cmdline);
	}
    }
}
/******************************
 * end job list helper routines
 ******************************/


/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void usage(void)
{
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg)
{
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg)
{
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler)
{
    struct sigaction action, old_action;

    action.sa_handler = handler;
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0)
	unix_error("Signal error");
    return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig)
{
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}
