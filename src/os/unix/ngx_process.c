
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_channel.h>


typedef struct {
    int     signo;
    char   *signame;
    char   *name;
    void  (*handler)(int signo);
} ngx_signal_t;



static void ngx_execute_proc(ngx_cycle_t *cycle, void *data);
static void ngx_signal_handler(int signo);
static void ngx_process_get_status(void);


int              ngx_argc;
char           **ngx_argv;//存放执行nginx时候所带的参数， 见ngx_save_argv
char           **ngx_os_argv;//指向nginx运行时候所带的参数，见ngx_save_argv

//当前操作的进程在ngx_processes数组中的下标
ngx_int_t        ngx_process_slot;
ngx_socket_t     ngx_channel;//存储所有子进程的数组  ngx_spawn_process中赋值  ngx_channel = ngx_processes[s].channel[1]
ngx_int_t        ngx_last_process;//ngx_processes数组中有意义的ngx_process_t元素中最大的下标
ngx_process_t    ngx_processes[NGX_MAX_PROCESSES];//存储所有子进程的数组  ngx_spawn_process中赋值


ngx_signal_t  signals[] = {
    { ngx_signal_value(NGX_RECONFIGURE_SIGNAL),
      "SIG" ngx_value(NGX_RECONFIGURE_SIGNAL),
      "reload",
            /* reload实际上是执行reload的nginx进程向原master+worker中的master进程发送reload信号，源master收到后，启动新的worker进程，同时向源worker
        进程发送quit信号，等他们处理完已有的数据信息后，退出，这样就只有新的worker进程运行。
     */
      ngx_signal_handler },

    { ngx_signal_value(NGX_REOPEN_SIGNAL),
      "SIG" ngx_value(NGX_REOPEN_SIGNAL),
      "reopen",
      ngx_signal_handler },

    { ngx_signal_value(NGX_NOACCEPT_SIGNAL),
      "SIG" ngx_value(NGX_NOACCEPT_SIGNAL),
      "",
      ngx_signal_handler },

    { ngx_signal_value(NGX_TERMINATE_SIGNAL),
      "SIG" ngx_value(NGX_TERMINATE_SIGNAL),
      "stop",
      ngx_signal_handler },

    { ngx_signal_value(NGX_SHUTDOWN_SIGNAL),
      "SIG" ngx_value(NGX_SHUTDOWN_SIGNAL),
      "quit",
      ngx_signal_handler },

    { ngx_signal_value(NGX_CHANGEBIN_SIGNAL),
      "SIG" ngx_value(NGX_CHANGEBIN_SIGNAL),
      "",
      ngx_signal_handler },

    { SIGALRM, "SIGALRM", "", ngx_signal_handler },

    { SIGINT, "SIGINT", "", ngx_signal_handler },

    { SIGIO, "SIGIO", "", ngx_signal_handler },

    { SIGCHLD, "SIGCHLD", "", ngx_signal_handler },

    { SIGSYS, "SIGSYS, SIG_IGN", "", SIG_IGN },

    { SIGPIPE, "SIGPIPE, SIG_IGN", "", SIG_IGN },

    { 0, NULL, "", NULL }
};

/*
master进程怎样启动一个子进程呢？其实很简单，fork系统调用即可以完成。ngx_spawn_process方法封装了fork系统调用，
并且会从ngx_processes数组中选择一个还未使用的ngx_process_t元素存储这个子进程的相关信息。如果所有1024个数纽元素中已经没有空
余的元素，也就是说，子进程个数超过了最大值1024，那么将会返回NGX_INVALID_PID。因此，ngx_processes数组中元素的初始化将在ngx_spawn_process方法中进行。
*/
//第一个参数是全局的配置，第二个参数是子进程需要执行的函数，第三个参数是proc的参数。第四个类型。  name是子进程的名称
ngx_pid_t
ngx_spawn_process(ngx_cycle_t *cycle, ngx_spawn_proc_pt proc, void *data,
    char *name, ngx_int_t respawn)
{
    u_long     on;
    ngx_pid_t  pid;
    ngx_int_t  s;
    // 如果respawn不小于0，则视为当前进程已经退出，需要重启
    if (respawn >= 0) {
        s = respawn;//替换进程ngx_processes[respawn],可安全重用该进程表项

    } else {
        for (s = 0; s < ngx_last_process; s++) {
            if (ngx_processes[s].pid == -1) {
                break;//先找到一个被回收的进程表象
            }
        }

        if (s == NGX_MAX_PROCESSES) {//最多只能创建1024个子进程
            ngx_log_error(NGX_LOG_ALERT, cycle->log, 0,
                          "no more than %d processes can be spawned",
                          NGX_MAX_PROCESSES);
            return NGX_INVALID_PID;
        }
    }


    if (respawn != NGX_PROCESS_DETACHED) {//不是分离的子进程      /* 不是热代码替换 */

        /* Solaris 9 still has no AF_LOCAL */
        /*
          这里相当于Master进程调用socketpair()为新的worker进程创建一对全双工的socket

          实际上socketpair 函数跟pipe 函数是类似的，也只能在同个主机上具有亲缘关系的进程间通信，但pipe 创建的匿名管道是半双工的，
          而socketpair 可以认为是创建一个全双工的管道。
          int socketpair(int domain, int type, int protocol, int sv[2]);
          这个方法可以创建一对关联的套接字sv[2]。下面依次介绍它的4个参数：参数d表示域，在Linux下通常取值为AF UNIX；type取值为SOCK。
          STREAM或者SOCK。DGRAM，它表示在套接字上使用的是TCP还是UDP; protocol必须传递0；sv[2]是一个含有两个元素的整型数组，实际上就
          是两个套接字。当socketpair返回0时，sv[2]这两个套接字创建成功，否则socketpair返回一1表示失败。
             当socketpair执行成功时，sv[2]这两个套接字具备下列关系：向sv[0]套接字写入数据，将可以从sv[l]套接字中读取到刚写入的数据；
          同样，向sv[l]套接字写入数据，也可以从sv[0]中读取到写入的数据。通常，在父、子进程通信前，会先调用socketpair方法创建这样一组
          套接字，在调用fork方法创建出子进程后，将会在父进程中关闭sv[l]套接字，仅使用sv[0]套接字用于向子进程发送数据以及接收子进程发
          送来的数据：而在子进程中则关闭sv[0]套接字，仅使用sv[l]套接字既可以接收父进程发来的数据，也可以向父进程发送数据。
          注意socketpair的协议族为AF_UNIX UNXI域
          */

        if (socketpair(AF_UNIX, SOCK_STREAM, 0, ngx_processes[s].channel) == -1)
        {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "socketpair() failed while spawning \"%s\"", name);
            return NGX_INVALID_PID;
        }

        ngx_log_debug2(NGX_LOG_DEBUG_CORE, cycle->log, 0,
                       "channel %d:%d",
                       ngx_processes[s].channel[0],
                       ngx_processes[s].channel[1]);
        /* 设置master的channel[0](即写端口)，channel[1](即读端口)均为非阻塞方式 */
        if (ngx_nonblocking(ngx_processes[s].channel[0]) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          ngx_nonblocking_n " failed while spawning \"%s\"",
                          name);
            ngx_close_channel(ngx_processes[s].channel, cycle->log);
            return NGX_INVALID_PID;
        }

        if (ngx_nonblocking(ngx_processes[s].channel[1]) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          ngx_nonblocking_n " failed while spawning \"%s\"",
                          name);
            ngx_close_channel(ngx_processes[s].channel, cycle->log);
            return NGX_INVALID_PID;
        }
        /*
            设置异步模式： 这里可以看下《网络编程卷一》的ioctl函数和fcntl函数 or 网上查询
          */
        on = 1;
        /*
          设置channel[0]的信号驱动异步I/O标志
          FIOASYNC：该状态标志决定是否收取针对socket的异步I/O信号（SIGIO）
          其与O_ASYNC文件状态标志等效，可通过fcntl的F_SETFL命令设置or清除
         */
        if (ioctl(ngx_processes[s].channel[0], FIOASYNC, &on) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "ioctl(FIOASYNC) failed while spawning \"%s\"", name);
            ngx_close_channel(ngx_processes[s].channel, cycle->log);
            return NGX_INVALID_PID;
        }
        /* F_SETOWN：用于指定接收SIGIO和SIGURG信号的socket属主（进程ID或进程组ID）
          * 这里意思是指定Master进程接收SIGIO和SIGURG信号
          * SIGIO信号必须是在socket设置为信号驱动异步I/O才能产生，即上一步操作
          * SIGURG信号是在新的带外数据到达socket时产生的
         */
        if (fcntl(ngx_processes[s].channel[0], F_SETOWN, ngx_pid) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "fcntl(F_SETOWN) failed while spawning \"%s\"", name);
            ngx_close_channel(ngx_processes[s].channel, cycle->log);
            return NGX_INVALID_PID;
        }
        /* FD_CLOEXEC：用来设置文件的close-on-exec状态标准
         *             在exec()调用后，close-on-exec标志为0的情况下，此文件不被关闭；非零则在exec()后被关闭
         *             默认close-on-exec状态为0，需要通过FD_CLOEXEC设置
         *     这里意思是当Master父进程执行了exec()调用后，关闭socket
         */
        if (fcntl(ngx_processes[s].channel[0], F_SETFD, FD_CLOEXEC) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "fcntl(FD_CLOEXEC) failed while spawning \"%s\"",
                           name);
            ngx_close_channel(ngx_processes[s].channel, cycle->log);
            return NGX_INVALID_PID;
        }
        /* 同上，这里意思是当Worker子进程执行了exec()调用后，关闭socket */
        if (fcntl(ngx_processes[s].channel[1], F_SETFD, FD_CLOEXEC) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "fcntl(FD_CLOEXEC) failed while spawning \"%s\"",
                           name);
            ngx_close_channel(ngx_processes[s].channel, cycle->log);
            return NGX_INVALID_PID;
        }
        /*
         设置即将创建子进程的channel ，这个在后面会用到，在后面创建的子进程的cycle循环执行函数中会用到，例如ngx_worker_process_init -> ngx_add_channel_event
         从而把子进程的channel[1]读端添加到epool中，用于读取父进程发送的ngx_channel_t信息
       */
        ngx_channel = ngx_processes[s].channel[1];

    } else {
        ngx_processes[s].channel[0] = -1;
        ngx_processes[s].channel[1] = -1;
    }

    ngx_process_slot = s;// 这一步将在ngx_pass_open_channel()中用到，就是设置下标，用于寻找本次创建的子进


    pid = fork();

    switch (pid) {

    case -1:
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "fork() failed while spawning \"%s\"", name);
        ngx_close_channel(ngx_processes[s].channel, cycle->log);
        return NGX_INVALID_PID;

    case 0:
        ngx_pid = ngx_getpid();// 设置子进程ID
            //printf(" .....slave......pid:%u, %u\n", pid, ngx_pid); slave......pid:0, 14127
        proc(cycle, data);// 调用proc回调函数，即ngx_worker_process_cycle。之后worker子进程从这里开始执行
        break;

    default:
        //printf(" ......master.....pid:%u, %u\n", pid, ngx_pid); master.....pid:14127, 14126
        break;
    }

    ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "start %s %P", name, pid);

    ngx_processes[s].pid = pid;
    ngx_processes[s].exited = 0;

    if (respawn >= 0) {
        return pid;
    }

    ngx_processes[s].proc = proc;
    ngx_processes[s].data = data;
    ngx_processes[s].name = name;
    ngx_processes[s].exiting = 0;

    switch (respawn) {/* 如果大于0,则说明是在重启子进程，因此下面的初始化不用再重复做 */

    case NGX_PROCESS_NORESPAWN:
        ngx_processes[s].respawn = 0;
        ngx_processes[s].just_spawn = 0;
        ngx_processes[s].detached = 0;
        break;

    case NGX_PROCESS_JUST_SPAWN:
        ngx_processes[s].respawn = 0;
        ngx_processes[s].just_spawn = 1;
        ngx_processes[s].detached = 0;
        break;

    case NGX_PROCESS_RESPAWN:
        ngx_processes[s].respawn = 1;
        ngx_processes[s].just_spawn = 0;
        ngx_processes[s].detached = 0;
        break;

    case NGX_PROCESS_JUST_RESPAWN:
        ngx_processes[s].respawn = 1;
        ngx_processes[s].just_spawn = 1;
        ngx_processes[s].detached = 0;
        break;

    case NGX_PROCESS_DETACHED:
        ngx_processes[s].respawn = 0;
        ngx_processes[s].just_spawn = 0;
        ngx_processes[s].detached = 1;
        break;
    }

    if (s == ngx_last_process) {
        ngx_last_process++;
    }

    return pid;
}


ngx_pid_t
ngx_execute(ngx_cycle_t *cycle, ngx_exec_ctx_t *ctx)
{
    return ngx_spawn_process(cycle, ngx_execute_proc, ctx, ctx->name,
                             NGX_PROCESS_DETACHED);
}


static void
ngx_execute_proc(ngx_cycle_t *cycle, void *data)
{
    /*
   execve()用来执行参数filename字符串所代表的文件路径，第二个参数是利用指针数组来传递给执行文件，并且需
   要以空指针(NULL)结束，最后一个参数则为传递给执行文件的新环境变量数组。
   */
    ngx_exec_ctx_t  *ctx = data;

    if (execve(ctx->path, ctx->argv, ctx->envp) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "execve() failed while executing %s \"%s\"",
                      ctx->name, ctx->path);
    }

    exit(1);
}


ngx_int_t
ngx_init_signals(ngx_log_t *log)
{
    ngx_signal_t      *sig;
    struct sigaction   sa;

    for (sig = signals; sig->signo != 0; sig++) {
        ngx_memzero(&sa, sizeof(struct sigaction));
        sa.sa_handler = sig->handler;
        sigemptyset(&sa.sa_mask);
        if (sigaction(sig->signo, &sa, NULL) == -1) {
            ngx_log_error(NGX_LOG_EMERG, log, ngx_errno,
                          "sigaction(%s) failed", sig->signame);
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}


void
ngx_signal_handler(int signo)
{
    char            *action;
    ngx_int_t        ignore;
    ngx_err_t        err;
    ngx_signal_t    *sig;

    ignore = 0;

    err = ngx_errno;

    for (sig = signals; sig->signo != 0; sig++) {
        if (sig->signo == signo) {
            break;
        }
    }

    ngx_time_sigsafe_update();

    action = "";

    switch (ngx_process) {

    case NGX_PROCESS_MASTER:
    case NGX_PROCESS_SINGLE:
        switch (signo) {
            //当接收到QUIT信号时，ngx_quit标志位会设为1，这是在告诉worker进程需要优雅地关闭进程
        case ngx_signal_value(NGX_SHUTDOWN_SIGNAL):
            ngx_quit = 1;
            action = ", shutting down";
            break;
        //当接收到TERM信号时，ngx_terminate标志位会设为1，这是在告诉worker进程需要强制关闭进程
        case ngx_signal_value(NGX_TERMINATE_SIGNAL):
        case SIGINT:
            ngx_terminate = 1;
            action = ", exiting";
            break;

        case ngx_signal_value(NGX_NOACCEPT_SIGNAL):
            if (ngx_daemonized) {
                ngx_noaccept = 1;
                action = ", stop accepting connections";
            }
            break;

        case ngx_signal_value(NGX_RECONFIGURE_SIGNAL):
            ngx_reconfigure = 1;
            action = ", reconfiguring";
            break;

        case ngx_signal_value(NGX_REOPEN_SIGNAL):
            ngx_reopen = 1;
            action = ", reopening logs";
            break;

        case ngx_signal_value(NGX_CHANGEBIN_SIGNAL):
            if (getppid() > 1 || ngx_new_binary > 0) {
                //nginx热升级通过发送该信号,这里必须保证父进程大于1，父进程小于等于1的话，说明已经由就master启动了本master，则就不能热升级
                //所以如果通过crt登录启动nginx的话，可以看到其PPID大于1,所以不能热升级
                /*
                 * Ignore the signal in the new binary if its parent is
                 * not the init process, i.e. the old binary's process
                 * is still running.  Or ignore the signal in the old binary's
                 * process if the new binary's process is already running.
                 */

                action = ", ignoring";
                ignore = 1;
                break;
            }

            ngx_change_binary = 1;
            action = ", changing binary";
            break;

        case SIGALRM:
            ngx_sigalrm = 1;//子进程会重新设置定时器信号，见ngx_timer_signal_handler
            break;

        case SIGIO:
            ngx_sigio = 1;
            break;

        case SIGCHLD:
            ngx_reap = 1;//子进程终止, 这时候内核同时向父进程发送个sigchld信号.等待父进程waitpid回收，避免僵死进程
            break;
        }

        break;

    case NGX_PROCESS_WORKER:
    case NGX_PROCESS_HELPER:
        switch (signo) {

        case ngx_signal_value(NGX_NOACCEPT_SIGNAL):
            if (!ngx_daemonized) {
                break;
            }
            ngx_debug_quit = 1;
        case ngx_signal_value(NGX_SHUTDOWN_SIGNAL):
            ngx_quit = 1;
            action = ", shutting down";
            break;

        case ngx_signal_value(NGX_TERMINATE_SIGNAL):
        case SIGINT:
            ngx_terminate = 1;
            action = ", exiting";
            break;

        case ngx_signal_value(NGX_REOPEN_SIGNAL):
            ngx_reopen = 1;
            action = ", reopening logs";
            break;

        case ngx_signal_value(NGX_RECONFIGURE_SIGNAL):
        case ngx_signal_value(NGX_CHANGEBIN_SIGNAL):
        case SIGIO:
            action = ", ignoring";
            break;
        }

        break;
    }

    ngx_log_error(NGX_LOG_NOTICE, ngx_cycle->log, 0,
                  "signal %d (%s) received%s", signo, sig->signame, action);

    if (ignore) {
        ngx_log_error(NGX_LOG_CRIT, ngx_cycle->log, 0,
                      "the changing binary signal is ignored: "
                      "you should shutdown or terminate "
                      "before either old or new binary's process");
    }

    if (signo == SIGCHLD) {
        ngx_process_get_status();
    }

    ngx_set_errno(err);
}

/**
 *  在Linux进程的状态中，僵尸进程是非常特殊的一种，它已经放弃了几乎所有的空间，没有任何可执行代码，也不能被调度，仅仅在进程的列表中保留一个位置，记载该进程的退出状态等信息供其他进程收集。
 *  除此之外，僵尸进程不再占有任何内存空间。
　*　 它需要他的父进程来为他收尸，如果他的父进程没有安装SIGCHLD信息处理函数调用wait或waitpid等待子进程的结束，又没有显示忽略该信息，那么它就一直保持僵尸状态
 */
static void
ngx_process_get_status(void)
{
    int              status;
    char            *process;
    ngx_pid_t        pid;
    ngx_err_t        err;
    ngx_int_t        i;
    ngx_uint_t       one;

    one = 0;

    for ( ;; ) {
        //等待任何一个子进程退出，没有任何限制
        /**
         * 1 为啥这里要用while( (pid = waitpid(-1,&stat,WNOHANG)) > 0)的方式

            这是因为SIGRTMIN以前的也就是从SIGHUP到SIGSYS的1-31个信号都是不可靠的。这和UNIX系统的早期实现有关系，出于历史原因这些老的信号现在并没有被修改成可排队的。

            不可靠信号的意思是信号并不会排队，如果进程处理速度很低，而同时发生了多个信号，就有可能只能接收到一个信号。

            这是因为在内核里用一个位来表示一个不可靠信号的触发，所以它无法保存多个同一类型不可靠信号的触发

            比如一个进程创建了100个子进程，然后在外部用kill同时把所有的子进程都杀掉，那么父进程并不能收到100个SIGCHLD信号。

            这里有篇文章有个实现信号丢失的情况的一个例子

            Linux可靠信号和不可靠信号

            所以这里要循环执行无阻塞的waitpid判断返回值来处理可能出现的多个SIGCHLD信号发生
         *
         */
        pid = waitpid(-1, &status, WNOHANG);

        if (pid == 0) {
            return;
        }

        if (pid == -1) {
            err = ngx_errno;

            if (err == NGX_EINTR) {
                continue;//如果waitpid调用被其他系统调用打断了，那么继续调用
            }

            if (err == NGX_ECHILD && one) {
                return;
            }

#if (NGX_SOLARIS || NGX_FREEBSD)

            /*
             * Solaris always calls the signal handler for each exited process
             * despite waitpid() may be already called for this process.
             *
             * When several processes exit at the same time FreeBSD may
             * erroneously call the signal handler for exited process
             * despite waitpid() may be already called for this process.
             */

            if (err == NGX_ECHILD) {
                ngx_log_error(NGX_LOG_INFO, ngx_cycle->log, err,
                              "waitpid() failed");
                return;
            }

#endif

            ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, err,
                          "waitpid() failed");
            return;
        }


        if (ngx_accept_mutex_ptr) {

            /*
             * unlock the accept mutex if the abnormally exited process
             * held it
             */

            ngx_atomic_cmp_set(ngx_accept_mutex_ptr, pid, 0);
        }


        one = 1;
        process = "unknown process";

        for (i = 0; i < ngx_last_process; i++) {
            if (ngx_processes[i].pid == pid) {
                ngx_processes[i].status = status;
                ngx_processes[i].exited = 1;
                process = ngx_processes[i].name;
                break;
            }
        }

        if (WTERMSIG(status)) {
#ifdef WCOREDUMP
            ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, 0,
                          "%s %P exited on signal %d%s",
                          process, pid, WTERMSIG(status),
                          WCOREDUMP(status) ? " (core dumped)" : "");
#else
            ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, 0,
                          "%s %P exited on signal %d",
                          process, pid, WTERMSIG(status));
#endif

        } else {
            ngx_log_error(NGX_LOG_NOTICE, ngx_cycle->log, 0,
                          "%s %P exited with code %d",
                          process, pid, WEXITSTATUS(status));
        }

        if (WEXITSTATUS(status) == 2 && ngx_processes[i].respawn) {
            ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, 0,
                          "%s %P exited with fatal code %d "
                          "and cannot be respawned",
                          process, pid, WEXITSTATUS(status));
            ngx_processes[i].respawn = 0;
        }
    }
}


void
ngx_debug_point(void)
{
    ngx_core_conf_t  *ccf;

    ccf = (ngx_core_conf_t *) ngx_get_conf(ngx_cycle->conf_ctx,
                                           ngx_core_module);

    switch (ccf->debug_points) {

    case NGX_DEBUG_POINTS_STOP:
        raise(SIGSTOP);
        break;

    case NGX_DEBUG_POINTS_ABORT:
        ngx_abort();
    }
}


ngx_int_t
ngx_os_signal_process(ngx_cycle_t *cycle, char *name, ngx_int_t pid)
{
    ngx_signal_t  *sig;

    for (sig = signals; sig->signo != 0; sig++) {
        if (ngx_strcmp(name, sig->name) == 0) {
            if (kill(pid, sig->signo) != -1) {
                return 0;
            }

            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "kill(%P, %d) failed", pid, sig->signo);
        }
    }

    return 1;
}
