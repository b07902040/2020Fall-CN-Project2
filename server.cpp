/* NOTE: 本程式僅供檢驗格式，並未檢查 congestion control 與 go back N */
/* NOTE: UDP socket is connectionless，每次傳送訊息都要指定ip與埠口 */
/* HINT: 建議使用 sys/socket.h 中的 bind, sendto, recvfrom */

/*
 * 連線規範：
 * 本次作業之 agent, sender, receiver 都會綁定(bind)一個 UDP socket 於各自的埠口，用來接收訊息。
 * agent接收訊息時，以發信的位址與埠口來辨認訊息來自sender還是receiver，同學們則無需作此判斷，因為所有訊息必定來自agent。
 * 發送訊息時，sender & receiver 必需預先知道 agent 的位址與埠口，然後用先前綁定之 socket 傳訊給 agent 即可。
 * (如 agent.c 第126行)
 */
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <fcntl.h>
#include "opencv2/opencv.hpp"
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <algorithm>

using namespace std;
using namespace cv;

typedef struct {
	int length;
	int seqNumber;
	int ackNumber;
	int fin;
	int syn;
	int ack;
} header;

typedef struct{
	header head;
	unsigned char data[3960];
    int width, height;
    int cwnd, cwnd_i;
} segment;

void setIP(char *dst, char *src) {
    if(strcmp(src, "0.0.0.0") == 0 || strcmp(src, "local") == 0
            || strcmp(src, "localhost")) {
        sscanf("127.0.0.1", "%s", dst);
    } else {
        sscanf(src, "%s", dst);
    }
}

int thres, cwnd, i_seq, resend, end_resend;
uchar buffer[6500000];
uchar *iptr;
int idx;
int timeout_back[200];
int timeout_i;

void time_out(int last_ack)
{
    //printf("%d %d\n", i_seq, last_ack);
    int total_timeout = 0;
    //printf("%d %d\n", cwnd-(i_seq-last_ack-1), timeout_i);
    for ( int i = cwnd-(i_seq-last_ack-1); i < timeout_i; i++ ) {
        total_timeout += timeout_back[i];
    }
    idx -= total_timeout;
    iptr -= total_timeout;
    timeout_i = 0;
    fill(timeout_back, timeout_back+200, 0);
    thres = max(cwnd/2, 1);
    cwnd = 1;
    printf("time out,   threshold=%d\n", thres);
    resend = last_ack+1;
    end_resend = i_seq-1;
    i_seq = last_ack+1;
    return;
}

int main(int argc, char* argv[]){
    thres = 16;
    cwnd = 2;
    int sender_fd;
    struct sockaddr_in agent, sender;
    char ip[2][50];
    int port[2];
    socklen_t agent_size, sender_size;
    segment s_tmp;
    
    setIP(ip[0], argv[1]); // agent IP
    setIP(ip[1], argv[2]); // sender IP
    sscanf(argv[3], "%d", &port[0]); // agent port
    sscanf(argv[4], "%d", &port[1]); // sender port

    sender_fd = socket(PF_INET, SOCK_DGRAM, 0);

    //memset(&agent, 0, sizeof(agent));
    agent.sin_family = AF_INET;
    agent.sin_port = htons(port[0]);
    agent.sin_addr.s_addr = inet_addr(ip[0]);
    memset(agent.sin_zero, '\0', sizeof(agent.sin_zero));

    //memset(&sender, 0, sizeof(sender));
    sender.sin_family = AF_INET;
    sender.sin_port = htons(port[1]);
    sender.sin_addr.s_addr = inet_addr(ip[1]);
    memset(sender.sin_zero, '\0', sizeof(sender.sin_zero));

    bind(sender_fd, (struct sockaddr *)&sender, sizeof(sender));

    agent_size = sizeof(agent);  
    sender_size = sizeof(sender);

    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    socklen_t time_len = sizeof(timeout);
    int ino = setsockopt(sender_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, time_len);
    //
    int recv_n;
    int filefd = open(argv[5], O_RDONLY);
    Mat imgServer;
    VideoCapture cap(argv[5]);
    int width = cap.get(CV_CAP_PROP_FRAME_WIDTH);
    int height = cap.get(CV_CAP_PROP_FRAME_HEIGHT);
    //
    memset(&s_tmp, 0, sizeof(s_tmp));
    s_tmp.width = width;
    s_tmp.height = height;
    s_tmp.head.seqNumber = 1;
    sendto(sender_fd, &s_tmp, sizeof(s_tmp), 0, (struct sockaddr *)&agent, agent_size);
    printf("send   data	#1,     winSize=1\n");
    recv_n = recvfrom(sender_fd, &s_tmp, sizeof(s_tmp), 0, (struct sockaddr *)&agent, &agent_size);
    while(recv_n == -1) {
        thres = 1;
        printf("time out,   threshold=%d\n", thres);
        printf("resend   data	#1,     winSize=1\n");
        memset(&s_tmp, 0, sizeof(s_tmp));
        s_tmp.width = width;
        s_tmp.height = height;
        s_tmp.head.seqNumber = 1;
        sendto(sender_fd, &s_tmp, sizeof(s_tmp), 0, (struct sockaddr *)&agent, agent_size);
        recv_n = recvfrom(sender_fd, &s_tmp, sizeof(s_tmp), 0, (struct sockaddr *)&agent, &agent_size);
    }
    printf("recv   ack	#1\n");
    //
    imgServer = Mat::zeros(height,width, CV_8UC3);    
    if(!imgServer.isContinuous()){
        imgServer = imgServer.clone();
    }

    //printf("setsockopt: %d\n", ino);
    int imgSize = width*height*3;
    int send_n;
    i_seq = 2;
    int thread_i = 0;
    resend = 0;
    end_resend = -1;
    int last_ack = -1;
    int flag = -1;
    int cnt = 0;
    while(1){
        cap >> imgServer;
        cnt += 1;
        if ( imgServer.empty() ) {
            memset(&s_tmp, 0, sizeof(s_tmp));
            s_tmp.head.fin = 1;
            sendto(sender_fd, &s_tmp, recv_n, 0, (struct sockaddr *)&agent, agent_size);
            printf("send     fin\n");
            recvfrom(sender_fd, &s_tmp, sizeof(s_tmp), 0, (struct sockaddr *)&agent, &agent_size);
            printf("recv    finack\n");
            break;
        }
        
        memcpy( buffer, imgServer.data, imgSize );
        iptr = buffer;
        idx = 0;
        timeout_i = 0;
        fill(timeout_back, timeout_back+200, 0);
        while( idx < imgSize ) {
            // send packet 
            for ( int i = 0; i < cwnd; i++ ) {
                //printf("idx: %d\n", idx);
                memset(&s_tmp, 0, sizeof(s_tmp));
                s_tmp.head.seqNumber = i_seq;
                i_seq++;
                s_tmp.head.length = min(4000, imgSize-idx+40);
                //printf("lenght:     %d\n", s_tmp.head.length);
                timeout_back[timeout_i++] = min(3960, imgSize-idx);
                s_tmp.cwnd = cwnd;
                s_tmp.cwnd_i = i+1;
                memcpy(s_tmp.data, iptr, min(3960, imgSize-idx));
                if ( resend <= end_resend ) {
                    printf("resend   data	#%d,    winSize=%d\n", s_tmp.head.seqNumber, cwnd);
                    resend++;
                }
                else {
                    printf("send   data	#%d,    winSize=%d\n", s_tmp.head.seqNumber, cwnd);
                    resend = 0;
                    end_resend = -1;
                }
                sendto(sender_fd, &s_tmp, sizeof(s_tmp), 0, (struct sockaddr *)&agent, agent_size);
                idx += (s_tmp.head.length-40);
                iptr += (s_tmp.head.length-40);
            }
            flag = -1;
            // recv ack
            for ( int i = 0; i < cwnd; i++ ) {
                memset(&s_tmp, 0, sizeof(s_tmp));
                recv_n = recvfrom(sender_fd, &s_tmp, sizeof(s_tmp), 0, (struct sockaddr *)&agent, &agent_size);
                if ( s_tmp.head.fin == 1 ) {
                    printf("recv    finack\n");
                    exit(0);
                }
                if ( recv_n == -1 ) {
                    //printf("packet drop\n");
                    time_out(last_ack);
                    flag = 0;
                    timeout_i = 0;
                    break;
                }
                printf("recv   ack	#%d\n", s_tmp.head.ackNumber);
                if ( last_ack == s_tmp.head.ackNumber ) {
                    flag = 1;
                }
                last_ack = s_tmp.head.ackNumber;
            }
            if ( flag == 1 ) {
                //printf("flush\n");
                time_out(last_ack);
                timeout_i = 0;
                continue;
            }
            timeout_i = 0;
            if ( flag == -1 ) {
                if ( cwnd < thres ) {
                    cwnd *= 2;
                }
                else {
                    cwnd += 1;
                }
            }
        }
    }
    cap.release();
    return 0;
}
