/*************************************************************************
	> File Name: client.h
	> Author: victoria 
	> Mail: 1105847344@qq.com
	> Created Time: 2019年08月02日 星期五 16时46分10秒
 ************************************************************************/
#include "util.h"
#include <sys/shm.h>
#include <sys/ipc.h>
#include <termios.h>
#include <pthread.h>
#define BUFFSIZE 2048

struct Share {
    int shareCnt;
　　 //互斥量
    pthread_mutex_t smutex;
    //条件变量
    pthread_cond_t sready;
};

char bsname[6][20] = {"cpu_info.sh", "disk_info.sh", "mem_info.sh", "user_info.sh", "SysInfo.sh", "enermy_pro.sh"};
char destname[6][20] = {"cpu.log", "disk.log", "mem.log", "user.log", "sys.log", "enermy.log"};
char tmp_info[BUFFSIZE * 4] = {0};
double dyAver = 0;
char *Error_log = "/opt/pi_client/Error_client.log";
char *sptpath = "/opt/pi_client/script";
char *logpath = "/opt/pi_client/log";

void recv_heart(int port, struct Share *share);
void recv_data(int dataport, int ctlport, struct Share *share);
void do_load(char *ip, int loadPort, struct Share *share);
void do_check(int inx, struct Share* share, int cnt, int port, char *ip);
int do_bash(int inx, struct Share *share, int cnt, int port, char *ip);
void send_warn(char *ip, int port, char *message, int ind);

//发送报警信息
void send_warn(char *ip, int port, char *message, int ind) {
    if (ind == 5 && !strcmp(message, "")) return ;
    if (ind == 0 && strstr(message, "warning") == NULL) return ;
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        write_log(Error_log, "[警报模块] [error] [process : %d] [message : %s]", getpid(), strerror(errno));
        return ;
    }
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip);
    //udp
    sendto(sockfd, message, strlen(message), 0, (struct sockaddr*)&addr, sizeof(addr));
    close(sockfd);
}
//运行脚本，存储日志信息，检测报警
int do_bash(int inx, struct Share *share, int cnt, int port, char *ip) {
    char opstr[100] = {0};
    FILE *pfile = NULL;
    if (inx == 2) {
        sprintf(opstr, "bash %s/%s %lf", sptpath, bsname[inx], dyAver);
    } else {
        sprintf(opstr, "bash %s/%s", sptpath, bsname[inx]);
    }
    pfile = popen(opstr, "r");
    if (!pfile) {
        write_log(Error_log, "[脚本执行] [error] [process : %d] [message : %s]", getpid(), strerror(errno));
        return -1;
    }
    char buff[BUFFSIZE] = {0};
    if (inx == 2) {
        if (fgets(buff, BUFFSIZE, pfile) != NULL) {
            strcat(tmp_info, buff);
        }
        if (fgets(buff, BUFFSIZE, pfile) != NULL) {
            dyAver = atof(buff);
        }
    } else {
        while (fgets(buff, BUFFSIZE, pfile) != NULL) {
            strcat(tmp_info, buff);
        }
    }
    pclose(pfile);
    
    if (share->shareCnt < 5 && (inx == 5 || inx == 0)) {
        char warnbuff[BUFFSIZE] = {0};
        strcpy(warnbuff, buff);
        send_warn(ip, port, warnbuff, inx);
    }
    
    if (cnt == 5) {
        char dest[100] = {0};
        sprintf(dest, "%s/%s", logpath, destname[inx]);
        FILE *fw = fopen(dest, "a+");
        if (!fw) {
            write_log(Error_log, "[脚本执行] [error] [process : %d] [message : %s]", getpid(), strerror(errno));
            return -1;
        }
	//对文件建立互斥锁
        flock(fw->_fileno, LOCK_EX);
        fprintf(fw, "%s", tmp_info);
        fclose(fw);
        memset(tmp_info, 0, sizeof(tmp_info));
    }
    return 0;
}

void do_check(int inx, struct Share *share, int cnt, int port, char *ip) {

    do_bash(inx, share, cnt, port, ip);
    
    if (inx == 0) {
	//线程互斥锁
	//第一个进程
        pthread_mutex_lock(&share->smutex);
        if (share->shareCnt == 5) {
            pthread_mutex_unlock(&share->smutex);
            return ;
        }
        share->shareCnt += 1; 
        if (share->shareCnt >= 5) {
　　　　　　　//pthread_cond_signal函数的作用是发送一个信号给
	    //另外一个正在处于阻塞等待状态的线程,使其脱离阻塞状态,继续执行.
	    //如果没有线程处在阻塞等待状态,pthread_cond_signal也会成功返回。
            pthread_cond_signal(&share->sready);
        }
	//解锁
        pthread_mutex_unlock(&share->smutex);
    }
}

//加载
void do_load(char *ip, int loadPort, struct Share* share) {
    printf("\nclient heart start\n");
    int stime = 10;
    while (1) {
        pthread_mutex_lock(&share->smutex);
        if (share->shareCnt < 5) {
            pthread_mutex_unlock(&share->smutex);
            break;
        }
        pthread_mutex_unlock(&share->smutex);
        int sockfd = socket_connect(loadPort, ip);
        if (sockfd < 0) {
            close(sockfd);
        write_log(Error_log, "[连接模块] [error] [process : %d] [message : failed to connect master]", getpid());

            sleep(stime);
            if (stime < 100)
                stime += 10;
            continue;
        }
        close(sockfd);
        break;
    }
}

//接受数据　？？　发送数据 主进程
void recv_data(int dataport, int ctlport, struct Share *share) {
    int listenfd = socket_create(ctlport);
    if (listenfd < 0) {
        perror("socket_create");
        write_log(Error_log, "[数据模块] [error] [process : %d] [message : %s]", getpid(), strerror(errno));
        return ;
    }
    while (1) {
        int newfd;
        //每次链接发6次
        newfd = accept(listenfd, NULL, NULL);
        if (newfd < 0) {
            perror("accept");
            write_log(Error_log, "[数据模块] [error] [process : %d] [message : %s]", getpid(), strerror(errno));
            continue;
        }
        for (int i = 0; i < 6; i++) {
            int fno = -1;
	    //接受一个文件标示 标示要发送的是那个文件
            int ret = recv(newfd, &fno, sizeof(int), 0);
            if (ret <= 0) {
                perror("recv");
                write_log(Error_log, "[数据模块] [error] [process : %d] [message : %s]", getpid(), strerror(errno));
                break;
            }
            char path[50] = {0};
	    //找到文件的路径
            sprintf(path, "%s/%s", logpath, destname[fno - 100]);
            int ack = 0;
            if (access(path, F_OK) < 0) {
                send(newfd, &ack, sizeof(int), 0);
                continue;
            }
            int sendfd = socket_create(dataport);
            if (sendfd < 0) {
                ack = 0;    
                send(newfd, &ack, sizeof(int), 0);
                continue;
            }
            ack = 1;
            send(newfd, &ack, sizeof(int), 0);
            int nsendfd = accept(sendfd, NULL, NULL);
            if (nsendfd < 0) {
                perror("accept");
                write_log(Error_log, "[数据模块] [error] [process : %d] [message : %s]", getpid(), strerror(errno));
                close(sendfd);
                continue;
            }
            FILE *fp = fopen(path, "r");
            flock(fp->_fileno, LOCK_EX);
            char buff[BUFFSIZE] = {0};
            while (fgets(buff, BUFFSIZE, fp) != NULL) {
                send(nsendfd, buff, strlen(buff), 0);
                memset(buff, 0, sizeof(buff));
            }
            fclose(fp);
            close(nsendfd);
            close(sendfd);
            remove(path);
        }
        close(newfd);
        pthread_mutex_lock(&share->smutex);
        share->shareCnt = 0;
        pthread_mutex_unlock(&share->smutex);
    }
    close(listenfd);
}

//心跳模块
void recv_heart(int port, struct Share *share) {
    int sockfd;
    sockfd = socket_create(port);
    if (sockfd < 0) {
        perror("Error bind on heartPort");
        write_log(Error_log, "[心跳模块] [error] [process : %d] [message : %s]", getpid(), strerror(errno));
        return ;
    }
    while (1) {
        int newfd = accept(sockfd, NULL, NULL);
        printf(" ❤ ");
        fflush(stdout);
        pthread_mutex_lock(&share->smutex);
        share->shareCnt = 0;
        pthread_mutex_unlock(&share->smutex);
        close(newfd);
    }
    close(sockfd);
}




