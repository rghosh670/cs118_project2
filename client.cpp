#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <fcntl.h>
#include <netdb.h>
#include <iostream>
#include <map>
#include <vector>
#include <climits>
#include <algorithm>
using namespace std;

// =====================================

#define RTO 500000 /* timeout in microseconds */
#define HDR_SIZE 12 /* header size*/
#define PKT_SIZE 524 /* total packet size */
#define PAYLOAD_SIZE 512 /* PKT_SIZE - HDR_SIZE */
#define WND_SIZE 10 /* window size*/
#define MAX_SEQN 25601 /* number of sequence numbers [0-25600] */
#define FIN_WAIT 2 /* seconds to wait after receiving FIN*/
#define MAX_NUM_PKTS 30000
#define MAX_TIMEOUT 10000

// Packet Structure: Described in Section 2.1.1 of the spec. DO NOT CHANGE!
struct packet {
    unsigned short seqnum;
    unsigned short acknum;
    char syn;
    char fin;
    char ack;
    char dupack;
    unsigned int length;
    char payload[PAYLOAD_SIZE];
};

// Printing Functions: Call them on receiving/sending/packet timeout according
// Section 2.6 of the spec. The content is already conformant with the spec,
// no need to change. Only call them at correct times.
void printRecv(struct packet* pkt) {
    printf("RECV %d %d%s%s%s\n", pkt->seqnum, pkt->acknum, pkt->syn ? " SYN": "", pkt->fin ? " FIN": "", (pkt->ack || pkt->dupack) ? " ACK": "");
}

void printSend(struct packet* pkt, int resend) {
    if (resend)
        printf("RESEND %d %d%s%s%s\n", pkt->seqnum, pkt->acknum, pkt->syn ? " SYN": "", pkt->fin ? " FIN": "", pkt->ack ? " ACK": "");
    else
        printf("SEND %d %d%s%s%s%s\n", pkt->seqnum, pkt->acknum, pkt->syn ? " SYN": "", pkt->fin ? " FIN": "", pkt->ack ? " ACK": "", pkt->dupack ? " DUP-ACK": "");
}

void printTimeout(struct packet* pkt) {
    printf("TIMEOUT %d\n", pkt->seqnum);
}

// Building a packet by filling the header and contents.
// This function is provided to you and you can use it directly
void buildPkt(struct packet* pkt, unsigned short seqnum, unsigned short acknum, char syn, char fin, char ack, char dupack, unsigned int length, const char* payload) {
    pkt->seqnum = seqnum;
    pkt->acknum = acknum;
    pkt->syn = syn;
    pkt->fin = fin;
    pkt->ack = ack;
    pkt->dupack = dupack;
    pkt->length = length;
    memcpy(pkt->payload, payload, length);
}

// =====================================

double setTimer() {
    struct timeval e;
    gettimeofday(&e, NULL);
    return (double) e.tv_sec + (double) e.tv_usec/1000000 + (double) RTO/1000000;
}

double setFinTimer() {
    struct timeval e;
    gettimeofday(&e, NULL);
    return (double) e.tv_sec + (double) e.tv_usec/1000000 + (double) FIN_WAIT;
}

int isTimeout(double end) {
    struct timeval s;
    gettimeofday(&s, NULL);
    double start = (double) s.tv_sec + (double) s.tv_usec/1000000;
    return ((end - start) < 0.0);
}

// =====================================


int main (int argc, char *argv[])
{
    if (argc != 5) {
        perror("ERROR: incorrect number of arguments\n "
               "Please use \"./client <HOSTNAME-OR-IP> <PORT> <ISN> <FILENAME>\"\n");
        exit(1);
    }

    struct in_addr servIP;
    if (inet_aton(argv[1], &servIP) == 0) {
        struct hostent* host_entry; 
        host_entry = gethostbyname(argv[1]); 
        if (host_entry == NULL) {
            perror("ERROR: IP address not in standard dot notation\n");
            exit(1);
        }
        servIP = *((struct in_addr*) host_entry->h_addr_list[0]);
    }

    unsigned int servPort = atoi(argv[2]);
    unsigned short initialSeqNum = atoi(argv[3]);

    FILE* fp = fopen(argv[4], "r");
    if (fp == NULL) {
        perror("ERROR: File not found\n");
        exit(1);
    }

    // =====================================
    // Socket Setup

    int sockfd;
    struct sockaddr_in servaddr;
    
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr = servIP;
    servaddr.sin_port = htons(servPort);
    memset(servaddr.sin_zero, '\0', sizeof(servaddr.sin_zero));

    int servaddrlen = sizeof(servaddr);

    // NOTE: We set the socket as non-blocking so that we can poll it until
    //       timeout instead of getting stuck. This way is not particularly
    //       efficient in real programs but considered acceptable in this
    //       project.
    //       Optionally, you could also consider adding a timeout to the socket
    //       using setsockopt with SO_RCVTIMEO instead.
    fcntl(sockfd, F_SETFL, O_NONBLOCK);

    // =====================================
    // Establish Connection: This procedure is provided to you directly and is
    // already working.
    // Note: The third step (ACK) in three way handshake is sent along with the
    // first piece of along file data thus is further below

    struct packet synpkt, synackpkt;
    unsigned short seqNum = initialSeqNum;

    buildPkt(&synpkt, seqNum, 0, 1, 0, 0, 0, 0, NULL);

    printSend(&synpkt, 0);
    sendto(sockfd, &synpkt, PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
    double timer = setTimer();
    int n;

    while (1) {

        while (1) {
            n = recvfrom(sockfd, &synackpkt, PKT_SIZE, 0, (struct sockaddr *) &servaddr, (socklen_t *) &servaddrlen);

            if (n > 0)
                break;
            else if (isTimeout(timer)) {
                printTimeout(&synpkt);
                // cout << "timing out here 4" << endl;
                printSend(&synpkt, 1);
                sendto(sockfd, &synpkt, PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
                timer = setTimer();
            }
        }

        printRecv(&synackpkt);
        if ((synackpkt.ack || synackpkt.dupack) && synackpkt.syn && synackpkt.acknum == (seqNum + 1) % MAX_SEQN) {
            seqNum = synackpkt.acknum;
            break;
        }
    }

    // =====================================
    // FILE READING VARIABLES
    
    char buf[PAYLOAD_SIZE];
    size_t m;

    // =====================================
    // CIRCULAR BUFFER VARIABLES

    struct packet ackpkt;
    ackpkt.acknum = 0;
    struct packet firstpkt;
    double firstPktTimer = INT_MAX;
    static struct packet pkts[MAX_NUM_PKTS];
    // int s = 0;
    int e = 0;
    // int full = 0;

    // =====================================
    // Send First Packet (ACK containing payload)

    m = fread(buf, 1, PAYLOAD_SIZE, fp);

    buildPkt(&firstpkt, seqNum, (synackpkt.seqnum + 1) % MAX_SEQN, 0, 0, 1, 0, m, buf);
    printSend(&firstpkt, 0);
    // cout << "sent firstpkt" << endl;
    sendto(sockfd, &firstpkt, PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
    firstPktTimer = setTimer();
    buildPkt(&firstpkt, seqNum, (synackpkt.seqnum + 1) % MAX_SEQN, 0, 0, 0, 1, m, buf);

    e = 1;

    // =====================================
    // *** TODO: Implement the rest of reliable transfer in the client ***
    // Implement GBN for basic requirement or Selective Repeat to receive bonus

    // Note: the following code is not the complete logic. It only sends a
    //       single data packet, and then tears down the connection without
    //       handling data loss.
    //       Only for demo purpose. DO NOT USE IT in your final submission

    int pktCounter = 0;
    int mySeqNum = seqNum;
    size_t windowLo = 0;
    size_t windowHi = windowLo + WND_SIZE - 1;
    vector<int> insertOrder;
    static int packetMapSent[MAX_NUM_PKTS] = {0};

    int previous_m = m;
    while (1) {
        m = fread(buf, 1, PAYLOAD_SIZE, fp);
        if (m == 0)
            break;

        mySeqNum += previous_m;
        previous_m = m;
        mySeqNum %= MAX_SEQN;
        buildPkt(&pkts[pktCounter], mySeqNum, 0, 0, 0, 0, 0, m, buf);
        insertOrder.push_back(mySeqNum);
        packetMapSent[pktCounter] = 0;
        pktCounter++;
    }

    windowHi = min(insertOrder.size(), windowHi);
    double onepktTimer = INT_MAX;
    bool onePacket = false;


    while (insertOrder.size() == 1){
        onePacket = true;
        n = recvfrom(sockfd, &ackpkt, PKT_SIZE, 0, (struct sockaddr *) &servaddr, (socklen_t *) &servaddrlen);
        if (n > 0) {
            if (ackpkt.acknum >= (pkts[0].seqnum + pkts[0].length)){
                printRecv(&ackpkt);
                break;
            }

            printSend(&pkts[0], 0);
            sendto(sockfd, &pkts[0], PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
            onepktTimer = setTimer();
            printRecv(&ackpkt);
        } else {
            if (isTimeout(firstPktTimer) && ackpkt.acknum <= firstpkt.seqnum){
                printTimeout(&firstpkt);
                printSend(&firstpkt, 1);
                sendto(sockfd, &firstpkt, PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
                firstPktTimer = setTimer();
            } 
            if (isTimeout(onepktTimer) && ackpkt.acknum <= pkts[0].seqnum){
                printTimeout(&pkts[0]);
                printSend(&pkts[0], 1);
                sendto(sockfd, &pkts[0], PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
                onepktTimer = setTimer();
              }
        }
    }

    size_t insertOrderSizeMinusOne = (insertOrder.size() > 0) ? (insertOrder.size()- 1) : 0;
    double mainTimer = INT_MAX;
    bool sendFirstPackets = false;
    size_t lastWindowLo = -1;
    size_t lastPacketSent = -1;
    bool foundNextPacket = false;
    
    while (windowLo < insertOrderSizeMinusOne) {
        n = recvfrom(sockfd, &ackpkt, PKT_SIZE, 0, (struct sockaddr *) &servaddr, (socklen_t *) &servaddrlen);
        int servAckNum = ackpkt.acknum;

        if (!sendFirstPackets){
            for (size_t i = windowLo; i < windowHi; i++){ 
                if (packetMapSent[i] == 0){
                    printSend(&pkts[i], 0);
                    sendto(sockfd, &pkts[i], PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
                    packetMapSent[i] = 1;
                    mainTimer = setTimer();
                }
            }
            sendFirstPackets = true;
        }

        if (n > 0) {
            printRecv(&ackpkt);
            size_t tempWindowLo = windowLo;
            for (size_t i = tempWindowLo; i < min(tempWindowLo + WND_SIZE, insertOrder.size()); i++){
                if (servAckNum >= insertOrder[i] && !(servAckNum > int(MAX_SEQN/2) && insertOrder[i] < int(MAX_SEQN/2))){
                    if (i >= windowLo) {
                        windowLo = i;
                        windowHi = windowLo + WND_SIZE;
                        windowHi = min(insertOrder.size(), windowHi);
                    }
                }
            }

            for (size_t i = windowLo; i < windowHi; i++){ 
                if (packetMapSent[i] == 0){
                    printSend(&pkts[i], 0);
                    sendto(sockfd, &pkts[i], PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
                    packetMapSent[i] = 1;
                    lastWindowLo = windowLo;
                    lastPacketSent = i;
                    mainTimer = setTimer();
                }
            }


        } else {
            if (isTimeout(firstPktTimer) && ackpkt.acknum <= firstpkt.seqnum && windowLo < 5){
                printTimeout(&firstpkt);
                printSend(&firstpkt, 1);
                sendto(sockfd, &firstpkt, PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
                firstPktTimer = setTimer();
            } 
            if (isTimeout(onepktTimer) && ackpkt.acknum <= pkts[0].seqnum && windowLo < 5){
                printTimeout(&pkts[0]);
                printSend(&pkts[0], 1);
                sendto(sockfd, &pkts[0], PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
                onepktTimer = setTimer();
              }

            if (isTimeout(mainTimer)){
                auto it = find(insertOrder.begin(), insertOrder.end(), ackpkt.acknum);
                int idx = distance(insertOrder.begin(), it);
                idx = (it == insertOrder.end()) ? 0 : idx;
                printTimeout(&pkts[idx]);
                for (size_t i = windowLo; i < windowHi; i ++){ 
                    printSend(&pkts[i], 1);
                    sendto(sockfd, &pkts[i], PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
                    mainTimer = setTimer();
                }
            }
        }
    }

    while (!onePacket) {
         if (ackpkt.acknum >= (pkts[insertOrder.size() - 1].seqnum + pkts[insertOrder.size() - 1].length))
                break;
        n = recvfrom(sockfd, &ackpkt, PKT_SIZE, 0, (struct sockaddr *) &servaddr, (socklen_t *) &servaddrlen);
        if(n > 0){
            printRecv(&ackpkt);
            if (ackpkt.acknum >= (pkts[insertOrder.size() - 1].seqnum + pkts[insertOrder.size() - 1].length))
                break;
        }
        else if (isTimeout(mainTimer)){
            printTimeout(&pkts[insertOrder.size() - 1]);
            printSend(&pkts[insertOrder.size() - 1], 1);
            sendto(sockfd, &pkts[insertOrder.size() - 1], PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
            mainTimer = setTimer();
        }
    }


    // *** End of your client implementation ***
    fclose(fp);

    // =====================================
    // Connection Teardown: This procedure is provided to you directly and is
    // already working.

   struct packet finpkt, recvpkt;
    buildPkt(&finpkt, ackpkt.acknum, 0, 0, 1, 0, 0, 0, NULL);
    buildPkt(&ackpkt, (ackpkt.acknum + 1) % MAX_SEQN, (ackpkt.seqnum + 1) % MAX_SEQN, 0, 0, 1, 0, 0, NULL);

    printSend(&finpkt, 0);
    sendto(sockfd, &finpkt, PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
    timer = setTimer();
    int timerOn = 1;

    double finTimer;
    int finTimerOn = 0;

    while (1) {
        while (1) {
            n = recvfrom(sockfd, &recvpkt, PKT_SIZE, 0, (struct sockaddr *) &servaddr, (socklen_t *) &servaddrlen);

            if (n > 0)
                break;
            if (timerOn && isTimeout(timer)) {
                printTimeout(&finpkt);
                printSend(&finpkt, 1);
                if (finTimerOn)
                    timerOn = 0;
                else
                    sendto(sockfd, &finpkt, PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
                timer = setTimer();
            }
            if (finTimerOn && isTimeout(finTimer)) {
                close(sockfd);
                if (! timerOn)
                    exit(0);
            }
        }
        printRecv(&recvpkt);
        if ((recvpkt.ack || recvpkt.dupack) && recvpkt.acknum == (finpkt.seqnum + 1) % MAX_SEQN) {
            timerOn = 0;
        }
        else if (recvpkt.fin && (recvpkt.seqnum + 1) % MAX_SEQN == ackpkt.acknum) {
            printSend(&ackpkt, 0);
            sendto(sockfd, &ackpkt, PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
            finTimer = setFinTimer();
            finTimerOn = 1;
            buildPkt(&ackpkt, ackpkt.seqnum, ackpkt.acknum, 0, 0, 0, 1, 0, NULL);
        }
    }
}
