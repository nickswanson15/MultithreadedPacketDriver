/*
CS 415 Project 2
Nick Swanson
nswanso4
This is my own work
*/

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "BoundedBuffer.h"
#include "destination.h"
#include "diagnostics.h"
#include "fakeapplications.h"
#include "freepacketdescriptorstore__full.h"
#include "freepacketdescriptorstore.h"
#include "networkdevice__full.h"
#include "networkdevice.h"
#include "packetdescriptor.h"
#include "packetdescriptorcreator.h"
#include "packetdriver.h"
#include "pid.h"
#include "queue.h"

#define UNUSED __attribute__((unused))
#define BUFFERSIZE 10
#define POOLSIZE   4
#define RECSIZE    2

static FreePacketDescriptorStore *fpds;
static NetworkDevice *net;
static BoundedBuffer *sending;
static BoundedBuffer *receiving[MAX_PID + 1];
static BoundedBuffer *fpdPool;

void blocking_send_packet(PacketDescriptor *pd) {
    sending->blockingWrite(sending, (void *)pd);
}

int nonblocking_send_packet(PacketDescriptor *pd) {
    return sending->nonblockingWrite(sending, (void *)pd);
}

void blocking_get_packet(PacketDescriptor **pd, PID pid) {
    receiving[pid]->blockingRead(receiving[pid], (void**)pd);
}

int nonblocking_get_packet(PacketDescriptor **pd, PID pid) {
    return receiving[pid]->nonblockingRead(receiving[pid], (void**)pd);
}

//send packets
void *send(UNUSED void *args) {
    int i, status;
    PacketDescriptor *spd;
    //attempt to send packet 10 times
    while (1) {
        sending->blockingRead(sending, (void **)&spd);
        for (i = 0; i < 10; i++) {
            if ((status = net->sendPacket(net, spd)) == 1) {
                break;
            }
        }
        //if successful, print success message 
        if (status) {
            DIAGNOSTICS("Packet sent after %d attempts\n", i + 1);
        } else {
            DIAGNOSTICS("Failed to send packet... aborting\n");
        }
        if ((fpdPool->nonblockingWrite(fpdPool, (void *)spd)) == 0) {
            fpds->nonblockingPut(fpds, spd);
        }
    }
    return NULL;
}

//get packets
void *get(UNUSED void* args) {
    PID pid;
    PacketDescriptor *gpd;
    PacketDescriptor *fpd;
    //get packet from store, initialize it, and register it with the device
    fpds->blockingGet(fpds, &gpd);
    initPD(gpd);
    net->registerPD(net, gpd);
    //handle PD
    while (1) {
        net->awaitIncomingPacket(net);
        fpd = gpd;
        //if something is in the pool
        if ((fpdPool->nonblockingRead(fpdPool, (void **)&gpd)) == 1) {
            initPD(gpd);
            net->registerPD(net, gpd);
            pid = getPID(fpd);
            if (receiving[pid]->nonblockingWrite(receiving[pid], (void *)fpd) == 0) {
                if (fpdPool->nonblockingWrite(fpdPool, (void *)fpd) == 0) {
                    fpds->blockingPut(fpds, fpd);
                }
            }
        //if nothing is in the pool
        } else if ((fpds->nonblockingGet(fpds, &gpd)) == 1) {
            initPD(gpd);
            net->registerPD(net, gpd);
            pid = getPID(fpd);
            if (receiving[pid]->nonblockingWrite(receiving[pid], (void *)fpd) == 0) {
                if (fpdPool->nonblockingWrite(fpdPool, (void *)fpd) == 0) {
                    fpds->blockingPut(fpds, fpd);
                }
            }
        // else, initialize and register
        } else {
            gpd = fpd;
            initPD(gpd);
            net->registerPD(net, gpd);
        }
    }
    return NULL;
}

void init_packet_driver(NetworkDevice               *nd, 
                        void                        *mem_start, 
                        unsigned long               mem_length,
                        FreePacketDescriptorStore **fpds_ptr) {
    //initialize threads
    pthread_t s;
    pthread_t g;
    //create free packet descriptor store
    fpds = FreePacketDescriptorStore_create(mem_start, mem_length);
    *fpds_ptr = fpds;
    //initialize device
    net = nd;
    //create bounded buffers
    sending = BoundedBuffer_create(BUFFERSIZE);
    fpdPool = BoundedBuffer_create(POOLSIZE);
    for (int i = 0; i <= MAX_PID; i++) {
        receiving[i] = BoundedBuffer_create(RECSIZE);
    }
    for (int i = 0; i < POOLSIZE; i++) {
        PacketDescriptor *pd;
        fpds->nonblockingGet(fpds, &pd);
        fpdPool->nonblockingWrite(fpdPool, (void *)pd);
    }
    //create threads
    pthread_create(&s, NULL, &send, NULL);
    pthread_create(&g, NULL, &get, NULL);
}