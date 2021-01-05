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
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <fcntl.h>
#include "opencv2/opencv.hpp"

using namespace std;
using namespace cv;

#define BUFFER_SIZE = 32;

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

int main(int argc, char* argv[]){
    int receiver_fd;
    struct sockaddr_in agent, receiver;
    char ip[2][50];
    int port[2];
    socklen_t agent_size, receiver_size;
    segment s_tmp;
    segment buffer_s[32];
    
    setIP(ip[0], argv[1]); // agent IP
    setIP(ip[1], argv[2]); // receiver IP
    sscanf(argv[3], "%d", &port[0]); // agent port
    sscanf(argv[4], "%d", &port[1]); // receiver port

    receiver_fd = socket(PF_INET, SOCK_DGRAM, 0);

    //memset(&agent, 0, sizeof(agent));
    agent.sin_family = AF_INET;
    agent.sin_port = htons(port[0]);
    agent.sin_addr.s_addr = inet_addr(ip[0]);
    memset(agent.sin_zero, '\0', sizeof(agent.sin_zero));

    //memset(&receiver, 0, sizeof(receiver));
    receiver.sin_family = AF_INET;
    receiver.sin_port = htons(port[1]);
    receiver.sin_addr.s_addr = inet_addr(ip[1]);
    memset(receiver.sin_zero, '\0', sizeof(receiver.sin_zero));

    bind(receiver_fd, (struct sockaddr *)&receiver, sizeof(receiver));

    agent_size = sizeof(agent);  
    receiver_size = sizeof(receiver);

    //

    Mat imgClient;
    int height, width;
    //
    memset(&s_tmp, 0, sizeof(s_tmp));
    recvfrom(receiver_fd, &s_tmp, sizeof(s_tmp), 0, (struct sockaddr *)&agent, &agent_size);
    printf("recv   data	#1\n");
    width = s_tmp.width;
    height = s_tmp.height;
    s_tmp.head.ack = 1;
    s_tmp.head.ackNumber = 1;
    printf("send   ack	#1\n");
    sendto(receiver_fd, &s_tmp, sizeof(s_tmp), 0, (struct sockaddr *)&agent, agent_size);
    //
    imgClient = Mat::zeros(height,width, CV_8UC3);
    if(!imgClient.isContinuous()){
        imgClient = imgClient.clone();
    }

    int imgSize = width*height*3;
    uchar buffer[imgSize];
    int recv_n, idx;
    int lenght;
    int buffer_i = 2;
    int ack_num = 1;
    uchar *iptr, *client;
    while (1) {
        iptr = buffer;
        idx = 0;
        while( idx < imgSize ) {
            memset(&s_tmp, 0, sizeof(s_tmp));
            recv_n = recvfrom(receiver_fd, &s_tmp, sizeof(s_tmp), 0, (struct sockaddr *)&agent, &agent_size);
            if ( recv_n == -1 ) {
                perror("ERROR");
                exit(0);
            }
            if(s_tmp.head.fin == 1) { //connection ending
                printf("recv     fin\n");
                memset(&s_tmp, 0, sizeof(s_tmp));
                s_tmp.head.fin = 1;
                s_tmp.head.ack = 1;
                sendto(receiver_fd, &s_tmp, recv_n, 0, (struct sockaddr *)&agent, agent_size);
                printf("send     finack\n");
                destroyAllWindows();
                exit(0);
            }
            if ( s_tmp.head.seqNumber != ack_num+1 ) { // packet loss
                //printf("------%d %d------\n", s_tmp.head.seqNumber, ack_num);
                //printf("packet loss\n");
                printf("drop   data	#%d\n", s_tmp.head.seqNumber);
                // resend ack
                memset(&s_tmp, 0, sizeof(s_tmp));
                s_tmp.head.ack = 1;
                s_tmp.head.ackNumber = ack_num;
                printf("send   ack	#%d\n", s_tmp.head.ackNumber);
                sendto(receiver_fd, &s_tmp, sizeof(s_tmp), 0, (struct sockaddr *)&agent, agent_size);
                continue;
            }
            if ( buffer_i > 32 ) { // buffer overflow
                //printf("buffer overflow\n");
                printf("drop   data	#%d\n", s_tmp.head.seqNumber);
                int resend_num = s_tmp.cwnd-s_tmp.cwnd_i;
                //printf("resend_num = %d\n", resend_num);
                // resend ack
                memset(&s_tmp, 0, sizeof(s_tmp));
                s_tmp.head.ack = 1;
                s_tmp.head.ackNumber = ack_num;
                printf("send   ack	#%d\n", s_tmp.head.ackNumber);
                sendto(receiver_fd, &s_tmp, sizeof(s_tmp), 0, (struct sockaddr *)&agent, agent_size);
                //
                for ( int i = 0; i < resend_num; i++ ) { //drop other all packet
                    memset(&s_tmp, 0, sizeof(s_tmp));
                    recv_n = recvfrom(receiver_fd, &s_tmp, sizeof(s_tmp), 0, (struct sockaddr *)&agent, &agent_size);
                    if(s_tmp.head.fin == 1) { //connection ending
                        printf("recv     fin\n");
                        memset(&s_tmp, 0, sizeof(s_tmp));
                        s_tmp.head.fin = 1;
                        s_tmp.head.ack = 1;
                        sendto(receiver_fd, &s_tmp, recv_n, 0, (struct sockaddr *)&agent, agent_size);
                        printf("send     finack\n");
                        destroyAllWindows();
                        exit(0);
                    }
                    printf("drop   data	#%d\n", s_tmp.head.seqNumber);
                    // resend ack
                    memset(&s_tmp, 0, sizeof(s_tmp));
                    s_tmp.head.ack = 1;
                    s_tmp.head.ackNumber = ack_num;
                    printf("send   ack	#%d\n", s_tmp.head.ackNumber);
                    sendto(receiver_fd, &s_tmp, sizeof(s_tmp), 0, (struct sockaddr *)&agent, agent_size);
                }
                printf("flush\n");
                buffer_i = 1;
                continue;
            }
            if(recv_n > 0) {
                if(s_tmp.head.fin == 1) { //connection ending
                    printf("recv     fin\n");
                    memset(&s_tmp, 0, sizeof(s_tmp));
                    s_tmp.head.fin = 1;
                    s_tmp.head.ack = 1;
                    sendto(receiver_fd, &s_tmp, recv_n, 0, (struct sockaddr *)&agent, agent_size);
                    printf("send     finack\n");
                    break;
                }
                printf("recv   data	#%d\n", s_tmp.head.seqNumber);
                buffer_i++;
                lenght = s_tmp.head.length;
                // copy to buffer
                if( lenght > 40 ) {
                    memcpy(iptr, s_tmp.data, lenght);
                    iptr += (lenght-40);
                    idx += (lenght-40);
                }
                // send ack
                ack_num = s_tmp.head.seqNumber;
                memset(&s_tmp, 0, sizeof(s_tmp));
                s_tmp.head.ack = 1;
                s_tmp.head.ackNumber = ack_num;
                printf("send   ack	#%d\n", s_tmp.head.ackNumber);
                sendto(receiver_fd, &s_tmp, sizeof(s_tmp), 0, (struct sockaddr *)&agent, agent_size);
                //printf("recv_n = %d\n", lenght);
            }
        }
        // play frame
        client = imgClient.data;
        memcpy(client, buffer, imgSize);
        imshow("Video", imgClient);
        char c = (char)waitKey(33.3333);
        if(c==27) {
            memset(&s_tmp, 0, sizeof(s_tmp));
            s_tmp.head.fin = 1;
            s_tmp.head.ack = 1;
            sendto(receiver_fd, &s_tmp, sizeof(s_tmp), 0, (struct sockaddr *)&agent, agent_size);
            break;
        }
    }
    destroyAllWindows();
    //close(receiver_fd);
    return 0;
}
