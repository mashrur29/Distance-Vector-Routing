#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <stdlib.h>
#include <stdio.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctime>
#include <time.h>

#include "DV.h"

const int BUFSIZE = 2048;
using namespace std;

struct header {
    int type;
    char source;
    char dest;
    int length;
};

enum type {
    TYPE_DATA, TYPE_ADVERTISEMENT, TYPE_READY, TYPE_RESET
};

// create a packet with header and payload
void *create_packet(int type, char source, char dest, int payloadLength, void *payload) {
    int allocatedPayloadLength = payloadLength;
    if ((type != TYPE_DATA) && (type != TYPE_ADVERTISEMENT))
        allocatedPayloadLength = 0;

    // create empty packet
    void *packet = malloc(sizeof(header)+allocatedPayloadLength);

    // create header
    header h;
    h.type = type;
    h.source = source;
    h.dest = dest;
    h.length = payloadLength;

    // fill in packet
    memcpy(packet, (void*)&h, sizeof(header));
    memcpy((void*)((char*)packet+sizeof(header)), payload, allocatedPayloadLength);

    return packet;
}

// periodically wake yourself up to multicast advertisement
void notify_self(DV &distance_vector, int socketfd, int type, char source, char dest, int payloadLength, void *payload) {
    void *sendPacket = create_packet(type, source, dest, payloadLength, payload);
    sockaddr_in destAddr = distance_vector.myaddr();
    sendto(socketfd, sendPacket, sizeof(header), 0, (struct sockaddr *)&destAddr, sizeof(sockaddr_in));
    free(sendPacket);
}

// extract the header from the packet
header extract_header(void *packet) {
    header h;
    memcpy((void*)&h, packet, sizeof(header));
    return h;
}

// extract the payload from the packet
void *extract_data(void *packet, int length) {
    void *payload = malloc(length);
    memcpy(payload, (void*)((char*)packet+sizeof(header)), length);
    return payload;
}

// multicast advertisement to all neighbors
void multicast(DV &distance_vector, int socketfd) {
    vector<node> neighbors = distance_vector.neighbors();
    for (int i = 0; i < neighbors.size(); i++) {
        void *sendPacket = create_packet(TYPE_ADVERTISEMENT, distance_vector.getName(), neighbors[i].name, distance_vector.getSize(), (void*)distance_vector.getEntries());
        sendto(socketfd, sendPacket, sizeof(header) + distance_vector.getSize(), 0, (struct sockaddr *)&neighbors[i].addr, sizeof(sockaddr_in));
        free(sendPacket);
    }
}

int main(int argc, char **argv) {
    // check for errors
    if (argc < 3) {
        perror("Not enough arguments.\n");
        return 0;
    }

    DV distance_vector(argv[1], argv[2]);

    vector<node> neighbors = distance_vector.neighbors();

    int myPort = atoi(argv[3]); // my port

    distance_vector.initMyaddr(myPort);
    sockaddr_in myaddr = distance_vector.myaddr();

    socklen_t addrlen = sizeof(sockaddr_in); // length of addresses

    // create a UDP socket
    int socketfd; // our socket
    if ((socketfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("cannot create socket\n");
        return 0;
    }

    // bind the socket to localhost and myPort
    if (bind(socketfd, (struct sockaddr *)&myaddr, sizeof(myaddr)) < 0) {
        perror("bind failed");
        return 0;
    }

    // distance vector routing
    int process = fork();

    if (process < 0) {
        perror("Error");
        return 0;
    } else if (process == 0) { // send to each neighbor periodically
        while(1) {
            // periodically wake up parent process
            notify_self(distance_vector, socketfd, TYPE_READY, 0, 0, 0, 0);
            sleep(1);
        }
    } else { // listen for advertisements
        void *rcvbuf = malloc(BUFSIZE);
        sockaddr_in remaddr;
        while(1) {
            memset(rcvbuf, 0, BUFSIZE);
            int recvlen = recvfrom(socketfd, rcvbuf, BUFSIZE, 0, (struct sockaddr *)&remaddr, &addrlen);

            header h = extract_header(rcvbuf);
            void *payload = extract_data(rcvbuf, h.length);

            if(h.type == TYPE_ADVERTISEMENT) {
                dv_entry entries[NROUTERS];
                memcpy((void*)entries, payload, h.length);

                for (int i = 0; i < neighbors.size(); i++) {
                    if (neighbors[i].name == h.source) {
                        distance_vector.startTimer(neighbors[i]);
                    }
                }

                distance_vector.update(payload, h.source);

            }
            else if(h.type == TYPE_READY) { // perform periodic tasks
                for (int i = 0; i < neighbors.size(); i++) {
                    node curNeighbor = neighbors[i];
                    if ((distance_vector.getEntries()[distance_vector.indexOf(curNeighbor.name)].cost() != -1) && distance_vector.timerExpired(neighbors[i])) {
                        notify_self(distance_vector, socketfd, TYPE_RESET, distance_vector.getName(), neighbors[i].name, distance_vector.getSize() / sizeof(dv_entry) - 2, 0);
                    }
                }
                multicast(distance_vector, socketfd);

            }
            else if(h.type == TYPE_RESET) {
                int hopcount = (int)h.length - 1;
                distance_vector.reset(h.dest);
                if (hopcount > 0) {
                    void *forwardPacket = create_packet(TYPE_RESET, distance_vector.getName(), h.dest, hopcount, (void*)0);
                    for (int i = 0; i < neighbors.size(); i++) {
                        if (neighbors[i].name != h.source)
                            sendto(socketfd, forwardPacket, sizeof(header), 0, (struct sockaddr *)&neighbors[i].addr, sizeof(sockaddr_in));
                    }
                }
            }
        }

        free(rcvbuf);
    }
}
