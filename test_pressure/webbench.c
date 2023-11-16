/*
 * (C) Radim Kolar 1997-2004
 * This is free software, see GNU Public License version 2 for
 * details.
 *
 * Simple forking WWW Server benchmark:
 *
 * Usage:
 *   webbench --help
 *
 * Return codes:
 *    0 - sucess
 *    1 - benchmark failed (server is not on-line)
 *    2 - bad param
 *    3 - internal error, fork failed
 * 
 */ 
#include"socket.c"
#include<unistd.h>
#include<sys/param.h>
#include<rpc/types.h>
#include<getopt.h>
#include<strings.h>
#include<time.h>
#include<signal.h>

/*values*/ 
volatile int timerexpired = 0; //判断测试时长是否已经达到设定时间
/*volatile类型修饰符，作为指令关键字，确保本指令不会因为编译器优化而省略，且每次要求重新读值*/
int speed = 0;
int failed = 0;
int bytes = 0;
/*global*/
int http10=1; /* 0 - http/0.9, 1 - http/1.0, 2 - http/1.1 */
/* Allow: GET, HEAD, OPTIONS, TRACE */
#define METHOD_GET 0
#define METHOD_HEAD 1
#define METHOD_OPTIONS 2
#define METHOD_TRACE 3
#define METHOD_VERSION "1.5"
int method = METHOD_GET;
int clients = 1;
int force = 0;
int force_reload = 0;
int proxyport = 80; //代理端口
char *proxyhost = NULL; //代理主机
int benchtime = 30;
/*internal */
int mypipe[2];
char host[MAXHOSTNAMELEN];
#define REQUEST_SIZE 2048;
char request[REQUEST_SIZE];

//构造长选项和短选项的对应
static const struct option long_options[]={
    {"force",no_argument,&force,1},
    {"reload",no_argument,&force_reload,1},
    {"time",required_argument,NULL,'t'},
    {"help",no_argument,'?'},
    {"http09",no_argument,NULL,'9'},
    {"http10",no_argument,NULL,'1'},
    {"http11",no_argument,NULL,'2'},
    {"get",no_argument,&method,METHOD_GET},
    {"head",no_argument,&method,METHOD_HEAD},
    {"options",no_argument,&method,METHOD_OPTIONS},
    {"trace",no_argument,&method,METHOD_TRACE},
    {"version",no_argument,&method,METHOD_VERSION},
    {"proxy",required_argument,NULL,'p'},
    {"clients",required_argument,NULL,'c'},
    {NULL,0,NULL,0}
};
/* prototypes */
static void benchcore(const char* host, const int port, const char *request);
static int bench(void);
static void build_request(const char *url);

static void alarm_handler(int signal) {
    timerexpired = 1;
}

//用法和各参数的详细意义
static void usage(void) {
    fprintf(stderr,
    "webbench [option]... URL\n"
	"  -f|--force               Don't wait for reply from server.\n"
	"  -r|--reload              Send reload request - Pragma: no-cache.\n"
	"  -t|--time <sec>          Run benchmark for <sec> seconds. Default 30.\n"
	"  -p|--proxy <server:port> Use proxy server for request.\n"
	"  -c|--clients <n>         Run <n> HTTP clients at once. Default one.\n"
	"  -9|--http09              Use HTTP/0.9 style requests.\n"
	"  -1|--http10              Use HTTP/1.0 protocol.\n"
	"  -2|--http11              Use HTTP/1.1 protocol.\n"
	"  --get                    Use GET request method.\n"
	"  --head                   Use HEAD request method.\n"
	"  --options                Use OPTIONS request method.\n"
	"  --trace                  Use TRACE request method.\n"
	"  -?|-h|--help             This information.\n"
	"  -V|--version             Display program version.\n"
    );
};
int main(int argc, char *argv[]) {
    int opt = 0;
    int options_index = 0;
    char *tmp = NULL;
    if(argc == 1) {
        usage();
        return 2;
    }
    while((opt = getopt_long(argc, argv, "912Vfrt:p:c:?h", long_options, &options_index)) != EOF) {
        switch(opt) {
            case  0 : break;
            case 'f': force = 1; break;
            case 'r': force_reload = 1; break;
            case '9': http09 = 0; break;
            case '1': http10 = 1; break;
            case '2': http11 = 2; break;
            case 'V': printf(PROGRAM_VERSION"\n"); exit(0);
            case 't': benchtime = atoi(optarg); break;
            case 'p':
            {
                /* proxy server parsing server:port*/
                tmp = strrchr(optarg, ':');
                proxyhost = optarg;
                if(tmp == NULL) break;
                if(tmp == optarg) {
                    fprintf(stderr, "Error in option --proxy %s: Missing hostname.\n", optarg);
                    return 2;
                }
                if(tmp == optarg + strlen(optarg) - 1) {
                    fprintf(stderr, "Error in option --proxy %s: Port number is missing.\n", optarg);
                    return 2;
                }
                *tmp = '\0';
                proxyport = atoi(tmp + 1);
                break;
            }
            case ':':
            case 'h':
            case '?': 
            {
                usage(); 
                return 2;
                break;
            }
            case 'c':
            {
                clients = atoi(optarg);
                break;
            }
        }
    }
    if(optind == argc) {
        fprintf(stderr, "webbench: Missing URL!\n");
        usage();
        return 2;
    }
    if(clients == 0) clients = 1;
    if(benchtime == 0) benchtime = 60;
    /*copyright*/
    fprintf(stderr, "Webbench - Simple Web Benchmark " PROGRAM_VERSION "\n"
                    "Copyright (c) Radim Kolar 1997-2004, GPL Open Source Software.\n");
    /*构造请求报文*/
    build_request(argv[optind]); //参数为URL

    /*print bench info*/
    /*请求报文构造好了，开始测压 */
    printf("\nBenchmarking: ");

    /*选择请求方法*/
    switch(method) {
    case METHOD_GET:
    default:
        printf("GET");
        break;
    case METHOD_OPTIONS:
        printf("OPTIONS");
        break;
    case METHOD_HEAD:
        printf("HEAD");
        break;
    case METHOD_TRACE:
        printf("TRACE");
        break;
    }

    /*打印URL*/
    printf(" %s ", argv[optind]);

    switch(http10) {
    case 0:
        printf(" (using HTTP/0.9)");
        break;
    case 1:
        printf(" (using HTTP/1.0)");
        break;
    case 2:
        printf(" (using HTTP/1.1)");
        break;
    }

    printf("\n");

    if(clients == 1) {
        printf("1 client");
    }
    else {
        printf("%d clients", clients);
    }

    printf(", running %d sec", benchtime);

    if(force) {
        printf(", early socket close");
    }

    if(proxyhost != NULL) {
        printf(", via proxy server %s: %d", proxyhost, proxyport);
    }

    if(force_reload) {
        printf(", forcing reload");
    }

    printf(".\n"); 
    //换行不能少！库函数是默认行缓冲，子进程会复制整个缓冲区
    /*若不换行刷新缓冲区，子进程会把缓冲区也打印出来，而换行后缓冲区刷新了，子进程的标准库函数的那块缓冲区就不会有前面这些了*/
    
    /*真正开始压力测试！！！*/
    return bench();
}


//构造HTTP报文请求到request数组中

/*
典型的http/1.1的get请求如下：

从下一行开始
GET /test.jpg HTTP/1.1 //请求行： 请求方法+url+协议版本
User-Agent: WebBench 1.5
Host:192.168.79.130
Pragma: no-cache
Connection: close

//从上一行结束，最后必须要有一个空行


该函数的目的就是根据需求填充出这样一个http请求放到request报文请求数组中
*/
void build_request(const char *url) {
    //存放端口号的中间数组
    char tmp[10];
    int i;
    bzero(host, MAXHOSTNAMELEN);
    bzero(request, REQUEST_SIZE); 
    if(force_reload && proxyhost != NULL && http10 < 1) {
        http10 = 1;
    }
    if(method == METHOD_HEAD && http10 < 1) {
        http10 = 1;
    }
    if(METHOD_OPTIONS && http10 < 2) {
        http10 = 2;
    }
    if(method == METHOD_TRACE && http10 < 2) {
        http10 = 2;
    }

    //开始填写HTTP请求

    //填充请求方法到请求行
    switch (method)
    {
    default:
    case METHOD_GET:
        strcpy(request, "GET");
        break;
    case METHOD_HEAD:
        strcpy(request, "HEAD");
        break;
    case METHOD_OPTIONS:
        strcpy(request, "OPTIONS");
        break;
    case METHOD_TRACE:
        strcpy(request, "TRACE");
        break;
    }
    //按照请求报文格式在请求方法后填充一个空格
    strcat(request, " ");

    //判断url的合法性

    //1.url中没有 "://" 字符
    if(NULL == strstr(url, "://")) {
        fprintf(stderr, "\n%s: is not a valid URL.\n", url);
        exit(2); //结束当前进程 2表示是因为url不合法导致进程停止的
    }

    //2.url过长
    if(strlen(url) > 1500) {
        fprintf(stderr, "URL is too long.\n");
        exit(2);
    }

    //3.若无代理服务器，则只支持http协议
    if(proxyhost == NULL) {
        //忽略字母大小写比较前7位
        if(0 != strncasecmp("http://", url, 7)) {
            fprintf(stderr, "\nonly HTTP protocol is directly supported, set --proxy for others.\n");
            exit(2);
        }
    }

    /*protocol/host delimiter*/
    //在url中找到主机名开始的地方
    //比如：http://baidu.com:80/
    //主机名开始的地方为bai....
    //i==7
    i = strstr(url, "://") - url + 3;
    // printf("%d\n", i);

    //4.从主机名开始的地方开始往后找，没有 '/' 则url非法
    if(strchr(url + i, '/') == NULL) {
        fprintf(stderr, "\nInvalid URL syntax - hostname don't ends with '/'.\n");
        exit(2);
    }
    //url合法性判断到此结束

    //开始填写url到请求行

    //无代理时
    if(proxyhost == NULL) {
        //存在端口号 比如http://www.baidu.com:80/
        /* get port from hostname */
        if(index(url + i, ':') != NULL && index(url + i, ':') < index(url + i, '/')) {

            //填充主机名到host字符数组，比如www.baidu.com
            strncpy(host, url + i, strchr(url + i, ':') - url - i);
            /*char *strncpy(char *dest, const char *src, size_t n) 把 src 所指向的字符串复制到 
            dest，最多复制 n 个字符。当 src 的长度小于 n 时，dest 的剩余部分将用空字节填充*/

            //初始化存放端口号的中间数组
            bzero(tmp, 10);

            //切割得到端口号
            strncpy(tmp, index(url + i, ':') + 1, strchr(url + i, '/') - index(url + i, ':') - 1);
            /* printf("tmp = %s\n", tmp);*/

            //设置端口号 atoi将字符串转整型
            proxyport = atoi(tmp);

            //避免写了':'却没有写端口号，这种情况下默认设置端口号为80
            if(proxyport == 0) {
                proxyport = 80;
            }
        }
        //不存在端口号
        else {
            //填充主机名到host字符数组，比如www.baidu.com
            strncpy(host, url + i, strcspn(url + i, "/"));
        }
        // printf("Host = %s\n", host);

        //将主机名，以及可能存在的端口号以及请求路径填充到请求报文中
        //比如url为http://www.baidu.com:80/one.jpg/
        //就是将www.baidu.com:80/one.jpg填充到请求报文中
        strcat(request + strlen(request), url + i + strcspn(url + i, "/"));
    }
    //存在代理服务器时就比较简单了，直接填写，不用自己处理
    else {
        // printf("ProxyHost = %s\nProxyPort = %d\n",proxyhost, proxyport);

        //直接将url填充到请求报文
        strcat(request, url);
    }

    //填充http协议版本到请求报文的请求行
    if(http10 == 1) {
        strcat(request, " HTTP/1.0");
    }
    else if(http10 == 2) {
        strcat(request, " HTTP/1.1");
    }

    //请求行填充结束，换行
    strcat(request, "\r\n");

    //填写请求报文的报头
    if(http10 > 0)
        strcat(request, "User-Agent: WebBench " PROGRAM_VERSION "\r\n");
    
    //不存在代理服务器且http协议版本为1.0或1.1，填充Host字段
    //当存在代理服务器或者http协议版本为0.9时，不需要填充Host字段
    //因为http0.9版本没有Host字段，而代理服务器不需要Host字段
    if(proxyhost == NULL && http10 > 0) {
        strcat(request, "Host: ");
        strcat(request, host);
        strcat(request, "\r\n");
    }

    /*pragma是http/1.1之前版本的历史遗留问题，仅作为与http的向后兼容而定义
    规范定义的唯一形式：
    Pragma:no-cache
    若选择强制重新加载，则选择无缓存
    */
    if(force_reload && proxyhost != NULL) {
        strcat(request, "Pragma: no-cache\r\n");
    }

    /*我们的目的是构造请求给网站，不需要传输任何内容，所以不必用长连接
    http/1.1默认Keep-alive(长连接）
    所以需要当http版本为http/1.1时要手动设置为 Connection: close*/
    if(http10 > 1) {
        strcat(request, "Connection: close\r\n");
    }
    /*add empty line at end */
    if(http10 > 0) {
        strcat(request, "\r\n");
    }
    // printf("\nReq =\n %s\n", request);
}


/* 创建管道和子进程 */
/*读子进程测试到的数据，然后统计处理*/
static int bench(void) {

    int i, j, k;
    pid_t pid = 0; //进程号定义 实际上也是int型的
    FILE *f;  //文件

    /* check avaibility of target server */
    i = Socket((proxyhost == NULL ? host :proxyhost), proxyport);

    if(i < 0) {
        //目标服务器不可用
        fprintf(stderr, "\nConnect to server failed. Aborting benchmark.\n");
        return 1;
    }

    //尝试连接成功了，关闭连接
    close(i);

    /* create pipe */
    if(pipe(mypipe)) {
        perror("pipe failed.");
        return 3;
    }

   /*
    父进程创建子进程后，fork函数是让子进程完全拷贝父进程，
    包括父进程上下文，什么意思呢？
    就是说父进程的EIP(CPU的下一条指令地址)以及变量等等一律拷贝，
    也就是说，父进程执行过的代码子进程是不会再执行，
    子进程下一条该执行的命令与父进程完全一样！！！
    */
    //创建子进程进行测试，子进程数量和clients有关
   for(i = 0; i < clients; i++) {

        pid = fork();//建立子进程

        if(pid <= (pid_t)0) {
            /*child process or error*/
            sleep(1); /* 当前进程挂起1毫秒，将cpu时间交给其他进程 */
            break; //跳出去，阻止子进程继续fork
        }
   }
   /*子进程数量=1+2+3+...+(clients)*/
   /*关键是fork函数的理解：fork一个子进程，该子进程将要执行的指令和父进程继续执行的指令是一模一样的*/
   if(pid < (pid_t)0) {
    fprintf(stderr, "problems forking worker on. %d\n", i);
    perror("fork failed.");
    return 3;
   }

   //当前进程是子进程
   if(pid == (pid_t)0) {
    /*由子进程发出请求报文，根据是否采用代理发送不同的报文*/
    if(proxyhost == NULL) {
        benchcore(host, proxyport, request);
    }
    else {
        benchcore(proxyhost, proxyport, request);
    }

    /*write result to pipe*/
    f = fopen(mypipe[1], "w");

    //管道写端打开失败
    if(f == NULL) {
        perror("open pipe for writing failed.");
        return 3;
    }

     /*向管道中写入该孩子进程在一定时间内
        请求成功的次数
        失败次数
        读取到服务器回复的总字节数
    */
    fprintf(f, "%d %d %d\n", speed, failed, bytes);
    fclose(f); //关闭写端
    return 0;
   }
   else {
    //当前进程是父进程
    f= fopen(mypipe[0], "r");

    if(f == NULL) {
        perror(stderr, "open pipe for reading failed.");
        return 3;
    }
    /*
        fopen标准IO函数是自带缓冲区的
        我们输入的数据非常短，并且数据要及时
        所以没有缓冲是最合适的
        我们不需要缓冲区
        因此把缓冲类型设置为_IONBF*/

    setvbuf(f, NULL, _IONBF, 0);
    speed = 0;
    failed = 0;
    bytes = 0;

    //父进程不停地读
    while (1)
    {
        //读入参数
        pid = fscanf(f, "%d %d %d", &i, &j, &k);

        //成功得到的参数个数小于2
        if(pid < 2) {
            fprintf(stderr, "Some of our childrens died.\n");
            break;
        }
        //记总数
        speed += i;
        failed += j;
        bytes += k;
        /* fprintf(stderr, "*Knock* %d %d read=%d\n",speed, failed, pid); */

        if(--clients == 0) { //记录已经读了多少个子进程的数据，读完就退出
            break;
        }
    }
    fclose(f);
    fprintf("\nSpeed = %d pages/min, %d bytes/sec.\nRequests: %d successed, %d failed.\n", (int)((speed + failed)/(benchtime / 60.0f)),
            (int)(bytes/(float)benchtime), speed, failed);
   }
   return i;
}

//子进程真正向服务器发送请求报文并以其得到期间相关数据
void benchcore(const char *host, const int port, const char *req) {
    int rlen;
    char buf[1500];//记录服务器响应请求返回的数据
    int s, i;
    struct sigaction sa; //信号处理函数定义

    /*setup alarm signal handler*/
    sa.sa_handler = alarm_handler;
    sa.sa_flags = 0;
    if(sigaction(SIGALRM, &sa, NULL))
        exit(3);

    alarm(benchtime); //开始计时

    rlen = strlen(req); //得到请求报文的长度

nexttry:
    while(1) {
        //只有在收到闹钟信号后会使得timeout = 1
        if(timerexpired) { //超时返回
            if(failed > 0) { 
                //修正失败信号
                fprintf(stderr, "Correcting failed by signal\n");
                failed--;
            }
            return;
        }
        //建立到目的网站的tcp连接，发送http请求
        s = Socket(host, port);
        //连接失败
        if(s < 0) {
            failed++; //失败次数+1
            continue;
        }

        //发送请求报文
        if(rlen != write(s, req, rlen)) { //write函数会返回实际写入的字节数
            failed++; //实际写入的字节数和请求报文字节数不相同，写失败，发送1，失败次数+1
            close(s);
            continue;
        }

        //http/0.9的特殊处理
        /*
         *因为http/0.9是在服务器回复后自动断开连接
         *在此可以提前先彻底关闭套接字的写的一半，如果失败了那肯定是个不正常的状态
         *事实上，关闭写后，服务器没有写完数据也不会再写了，这个就不考虑了
         *如果关闭成功则继续往后，因为可能还需要接收服务器回复的内容
         *当这个写一定是可以关闭的，因为客户端也不需要写，只需要读
         *因此，我们主动破坏套接字的写，但这不是关闭套接字，关闭还是得用close
        */
        if(http10 == 0) {
            if(shutdown(s, 1)) { //1表示关闭写 关闭成功返回0，出错返回-1
                failed++;//关闭出错，失败次数+1
                close(s);//关闭套接字
                continue;
            }
        }
        //force=0是默认需要等待服务器回复
        if(force == 0) {
            /* 从套接字读取所有服务器回复的数据 */
            while (1)
            {
                //超时标志为1，不再读取服务器回复的数据
                if(timerexpired) {
                    break;
                }

                //读取套接字中1500个字节数据到buf数组中
                i = read(s, buf, 1500); //如果套接字中数据小于要读取的字节数1500会引起阻塞 返回-1

                //read返回值：

                //未读取任何数据   返回   0
                //读取成功         返回   已经读取的字节数
                //阻塞             返回   -1


                //读取阻塞了
                if(i < 0) {
                    failed++; //失败次数+1
                    close(s); //关闭套接字，不然失败次数多会严重浪费资源
                    goto nexttry; //这次失败了那么继续请求下一次连接和发出请求
                }
                //读取成功
                else if(i == 0) {
                    break; //没有读取到任何字节数
                }
                else bytes += i; //从服务器读取到的总字节数增加
            }
        }

        /*
        close返回返回值
        成功   返回 0
        失败   返回 -1
        */

       //套接字关闭失败
        if(close(s)) {
            failed++; //没有成功得到服务器响应的子进程数量
            continue;
        }

        //套接字关闭成功 成功得到服务器响应的子进程数量+1
        speed++;
    }
}