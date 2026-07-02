#include "zk/xiaojusdk/porting/linux/xiaoju_pal.h"
#include "zk/xiaojusdk/include/mqtt.h"
#include "zk/xiaojusdk/include/xiaoju.h"



/********************************************************
  * @Description：   创建一个任务
  * @Arguments	：
			    threadName      任务名称，指向ASCII字符串的指针
			    thread          任务控制块指针
			    start_routine   任务函数，通常是一个死循环
			    arg             传递给任务函数的参数
			    prio            任务的优先级
			    stack_size      任务的堆栈的总空间大小，单位是word (4byte)
  * @Returns	：
                0   正常
                -1   错误
 *******************************************************/
int xj_pal_thread_create(char* threadName,xj_pal_thread_t *thread,void *(*start_routine)(void *arg),void *par,Task_Priority prio,uint32_t stack_size )
{
    int ret = 1;
    pthread_attr_t tmp_attr;

    pthread_attr_init(&tmp_attr);
    pthread_attr_setdetachstate(&tmp_attr, PTHREAD_CREATE_DETACHED);
    ret = pthread_create(thread,&tmp_attr,start_routine,par);

    if(0 == ret)
    {
        return 0;
    }
    else
    {
        //报错
        return -1;
    }
}



/********************************************************
  * @Description：删除一个任务
  * @Arguments	：
			    thread          任务控制块指针

  * @Returns	：
                无
 *******************************************************/
void xj_pal_thread_cancel(xj_pal_thread_t* thread)
{
    if(NULL == thread)
    {
        while(1)
        {
            sleep(1);
        }
    }
    pthread_cancel(*thread);
}



/********************************************************
  * @Description：任务延时(阻塞)
  * @Arguments	：
			    millsec  延时心跳周期值(1=1ms)

  * @Returns	：
                无
 *******************************************************/
void xj_pal_msleep(uint32_t millsec)
{
    struct timespec ts;

    ts.tv_nsec = (millsec % 1000) * 1000000;
    ts.tv_sec = (millsec / 1000);

    while (nanosleep (&ts, &ts) == -1 && errno == EINTR);
}





/********************************************************
  * @Description：创建互斥信号量-重定义
  * @Arguments	：
			    p_mutex          信号量指针

  * @Returns	：
                无
 *******************************************************/
void Redefine_OSMutexCreate(xj_pal_mutex_t  * p_mutex )
{
    pthread_mutex_init(p_mutex, NULL);
}



/********************************************************
  * @Description：申请互斥信号量-重定义
  * @Arguments	：
			    p_mutex     信号量指针
                timeout     等待时间
  * @Returns	：
                无
 *******************************************************/
void Redefine_OSMutexPend(xj_pal_mutex_t*         p_mutex,uint32_t timeout)
{
    pthread_mutex_lock(p_mutex);
}



/********************************************************
  * @Description：释放一个互斥信号量-重定义
  * @Arguments	：
			    p_mutex     信号量指针
  * @Returns	：
                无
 *******************************************************/
void Redefine_OSMutexPost(xj_pal_mutex_t*         p_mutex)
{
    pthread_mutex_unlock(p_mutex);
}



/********************************************************
  * @Description：删除一个互斥信号量-重定义
  * @Arguments	：
			    p_mutex     信号量指针
  * @Returns	：
                无
 *******************************************************/
void Redefine_OSMutexDel(xj_pal_mutex_t*         p_mutex)
{
    pthread_mutex_destroy(p_mutex);
}



/********************************************************
  * @Description：消息发送
  * @Arguments	：
			    fd  消息端口句柄
			    buf 待发送缓存
                len 待发送长度
                flags
  * @Returns	：发送字节数

 *******************************************************/
ssize_t xj_pal_sendall(xj_pal_socket_handle fd, const void* buf, size_t len, int flags)
{
    size_t sent = 0;
    if(len>0)
    {
        //Printf_TCP_Log((char*)buf,len,_TCP_SEND_);
    }
    while(sent < len)
    {
        ssize_t tmp = send(fd, buf + sent, len - sent, flags);
        if (tmp < 1)
        {
            return MQTT_ERROR_SOCKET_ERROR;
        }
        sent += (size_t) tmp;
    }
    return sent;

}

/********************************************************
  * @Description：消息接收
  * @Arguments	：
			    fd  消息端口句柄
			    buf 待接收缓存
                len 待接收长度
                flags
  * @Returns	：接收字节数

 *******************************************************/
ssize_t xj_pal_recvall(xj_pal_socket_handle fd, void* buf, size_t bufsz, int flags)
{
    const void const *start = buf;
    ssize_t rv = 0;
    do
    {
        rv = recv(fd, buf, bufsz, flags);

        if (rv > 0)
        {
            /* successfully read bytes from the socket */
            buf += rv;
            bufsz -= rv;
        }
        else if (rv < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
        {
            /* an error occurred that wasn't "nothing to read". */
            return MQTT_ERROR_SOCKET_ERROR;
        }

    }
    while (rv > 0);

    if((char*)buf - (char*)start > 0)
    {
        //Printf_TCP_Log((char*)start,buf-start,_TCP_RECEIVE_);
    }
    return buf - start;
}


/********************************************************
  * @Description：建立网络连接
  * @Arguments	：
			    addr  ASSCII地址指针
			    port ASCII端口指针
  * @Returns	：连接控制块指针

 *******************************************************/
xj_pal_socket_handle* xj_pal_open_nb_socket(const char* addr, const char* port)
{

    /*struct addrinfo hints={0};*/
    struct addrinfo hints;
    memset(&hints,0,sizeof(struct addrinfo));

    hints.ai_family = AF_UNSPEC; /* IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM; /* Must be TCP */
    int sockfd = -1;
    int rv;
    struct addrinfo *p, *servinfo;

    /* get address information */
    rv = getaddrinfo(addr, port, &hints, &servinfo);
    if(rv != 0)
    {
        fprintf(stderr, "Failed to open socket (getaddrinfo): %s\n", gai_strerror(rv));
        return NULL;
    }

    /* open the first possible socket */
    int connectSucceed = 0;
    for(p = servinfo; p != NULL; p = p->ai_next)
    {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd < 0) continue;

        /* connect to server */
        rv = connect(sockfd, servinfo->ai_addr, servinfo->ai_addrlen);
        if( 0 != rv )
        {
            close(sockfd);
            continue;
        }
        else
        {
            connectSucceed = 1;
            break;
        }
    }
    if(connectSucceed==0)
    {
        return NULL;
    }

    /* free servinfo */
    freeaddrinfo(servinfo);

    /* make non-blocking */
    if (sockfd != -1) fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL) | O_NONBLOCK);

    /* return the new socket fd */
    xj_pal_socket_handle* ret = (xj_pal_socket_handle*)malloc(sizeof(xj_pal_socket_handle));
    *ret = sockfd;
    return ret;
}

/********************************************************
  * @Description：断开网络连接
  * @Arguments	：
			    socket  网络控制块指针
  * @Returns	：

 *******************************************************/
void xj_pal_close_socket(xj_pal_socket_handle* socket)
{
    if(NULL != socket)
    {
        close(*socket);
        free(socket);
    }
}


/********************************************************
  * @Description：获取实时时间-年
  * @Arguments	：
			    NULL
  * @Returns	：
                NULL
 *******************************************************/
int xj_pal_get_int_year(void)
{
    char year[5];
    memset(year, 0, 5);
    time_t now = time(NULL);
    strftime(year, 5, "%Y", localtime(&now));
    return atoi(year);
}


/********************************************************
  * @Description：获取实时时间-月
  * @Arguments	：
			    NULL
  * @Returns	：
                NULL
 *******************************************************/
int xj_pal_get_int_month(void)
{
    char month[5];
    memset(month, 0, 5);
    time_t now = time(NULL);
    strftime(month, 5, "%m", localtime(&now));
    return atoi(month);
}


/********************************************************
  * @Description：获取实时时间-日
  * @Arguments	：
			    NULL
  * @Returns	：
                NULL
 *******************************************************/
int xj_pal_get_int_day(void)
{
    char day[5];
    memset(day, 0, 5);
    time_t now = time(NULL);
    strftime(day, 5, "%d", localtime(&now));
    return atoi(day);
}


/********************************************************
  * @Description：获取实时时间-时
  * @Arguments	：
			    NULL
  * @Returns	：
                NULL
 *******************************************************/
int xj_pal_get_int_hour(void)
{
    char hour[5];
    memset(hour, 0, 5);
    time_t now = time(NULL);
    strftime(hour, 5, "%H", localtime(&now));
    return atoi(hour);
}


/********************************************************
  * @Description：获取实时时间-分
  * @Arguments	：
			    NULL
  * @Returns	：
                NULL
 *******************************************************/
int xj_pal_get_int_minute(void)
{
    char minute[5];
    memset(minute, 0, 5);
    time_t now = time(NULL);
    strftime(minute, 5, "%M", localtime(&now));
    return atoi(minute);
}

/********************************************************
  * @Description：获取实时时间-秒
  * @Arguments	：
			    NULL
  * @Returns	：
                NULL
 *******************************************************/
int xj_pal_get_int_sec(void)
{
    char sec[5];
    memset(sec, 0, 5);
    time_t now = time(NULL);
    strftime(sec, 5, "%S", localtime(&now));
    return atoi(sec);
}


/********************************************************
  * @Description：获取实时时间-时间戳
  * @Arguments	：
			    NULL
  * @Returns	：
                NULL
 *******************************************************/
xj_pal_time_t xj_pal_time(void)
{
    return time(NULL);
}


/********************************************************
  * @Description：保存参数
  * @Arguments	：
                input：参数指针
                size：大小
  * @Returns	：
                0：写入成功
               -1：写入失败
 *******************************************************/
int8_t xj_pal_write_persist_params(char* input,int size)
{
    FILE * fp = NULL;

    if(NULL != input)
    {
        fp = fopen ("xj_params","wb");
        if(NULL != fp)
        {
            fwrite(input,size,1,fp);
            fclose(fp);
            return 0;
        }
    }
    return -1;
}

/********************************************************
  * @Description：读取参数
  * @Arguments	：
                input：参数指针
                size：大小
  * @Returns	：
                0：读取成功
               -1：读取失败
 *******************************************************/
int8_t xj_pal_read_persist_params(char* output,int limit)
{
    int8_t result = -1;
    int size = 0;
    FILE * fp = NULL;

    if(NULL != output)
    {

        fp = fopen ("xj_params","rb");
        if(fp != NULL)
        {
            fseek(fp, 0, SEEK_END);
            size = ftell(fp);
            fseek(fp, 0, SEEK_SET);

            if(limit != size)//可能由于软件升级，改变了参数结构体的大小，导致读取上来大小不一致,避免数据错乱 舍弃数据
            {
                result = -1;
            }
            else
            {
                fread(output, 1, size, fp);
                result = 0;
            }
            fclose(fp);
        }
    }
    return result;
}
/*
*@brief 通用保存数据写入
*/
int8_t xj_pal_write_phm(char* input,int size)
{
    FILE * fp = NULL;

    if(NULL != input)
    {
        fp = fopen ("xj_phm","wb");
        if(NULL != fp)
        {
            fwrite(input,size,1,fp);
            fclose(fp);
            return 0;
        }
    }
    return -1;
}
/*
*@brief 通用保存数据读取
*/
int8_t xj_pal_read_phm(char* output,int limit)
{
    int8_t result = -1;
    int size = 0;
    FILE * fp = NULL;

    if(NULL != output)
    {
        fp = fopen ("xj_phm","rb");
        if(fp != NULL)
        {
            fseek(fp, 0, SEEK_END);
            size = ftell(fp);
            fseek(fp, 0, SEEK_SET);

            if(limit != size)//可能由于软件升级，改变了参数结构体的大小，导致读取上来大小不一致,避免数据错乱 舍弃数据
            {
                result = -1;
            }
            else
            {
                fread(output, 1, size, fp);
                result = 0;
            }
            fclose(fp);
        }
    }
    return result;
}

/********************************************************
  * @Description：日志导出
  * @Arguments	：
			    str  日志字符串
			    len  日志长度
  * @Returns	：

 *******************************************************/
void User_log_export(const char*str,uint32_t len)
{
    printf("%s",str);
}




/** @endcond */
