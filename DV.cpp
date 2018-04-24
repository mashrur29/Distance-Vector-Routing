#include "DV.h"
#include <iostream>
#include <sstream>
#include <fstream>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctime>
#include <time.h>
#include <unistd.h>

#define PB      push_back

using namespace std;

char selfName;
std::map<int, char> m_nodename;
std::map<int, char> m_routerid;


DV::DV(const char *filename, const char *self) {
    fstream topology(filename);

    string line; // current line of file
    string field; // current token (to be put into entry's field)
    selfName = self[0]; // name of self
    m_self = indexOf(self[0]);

    // initialize m_entries
    for (int dest = 0; dest < NROUTERS; dest++) {
        m_entries[dest].setNexthopName('0');
        m_entries[dest].setNexthopPort(-1);
        m_entries[dest].setCost(-1);
        m_entries[dest].setValid();
    }

    getline(topology, line);
    while (getline(topology, line)) { // parse file line by line
        stringstream linestream(line);
        dv_entry entry;

        entry.setValid();

        // source router
        //getline(linestream, field, ' ');
        //char name = field[0];
        char name = self[0];

        // destination router
        getline(linestream, field, ' ');
        int dest = indexOf(field[0]);
        node n;
        n.name = field[0];
        entry.setNexthopName(field[0]);


        // link cost
        getline(linestream, field, ' ');
        entry.setCost(atoi(field.c_str()));

        // destination port number
        getline(linestream, field, ' ');
        int port = atoi(field.c_str());
        entry.setNexthopPort(port);
        n.portno = port;

        memset((char *)&n.addr, 0, sizeof(n.addr));
        n.addr.sin_family = AF_INET;
        n.addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        n.addr.sin_port = htons(port);



        startTimer(n);
        m_neighbors.PB(n); // store neighbor
        m_entries[dest] = entry;


        m_portnos[n.name] = n.portno;
        m_nodename[n.portno] = n.name;
    }

    // special port number for sending data packet
    m_portnos['H'] = 11111;

    memcpy((void*)m_entries_backup, (void*)m_entries, sizeof(m_entries));
}

void DV::reset(char dead) {
    for (int i = 0; i < m_neighbors.size(); i++) {
        if (m_neighbors[i].name == dead) {
            if (m_entries_backup[indexOf(dead)].cost() != -1)
                m_entries_backup[indexOf(dead)].setInvalid();
        }
    }
    memcpy((void*)m_entries, (void*)m_entries_backup, sizeof(m_entries));
    print(m_entries, nameOf(m_self), "Reset routing table", true);
}

// update this router's distance vector based on received advertisement from source
// return false if this router's distance vector was not changed
void DV::update(const void *advertisementBuf, char source) {
    dv_entry originalEntries[NROUTERS];
    memcpy((void*)originalEntries, (void*)m_entries, sizeof(m_entries));

    bool updatedDV = false;

    int intermediate = indexOf(source);
    if (m_entries_backup[intermediate].isValid()) {
        m_entries_backup[intermediate].setValid();
        m_entries[intermediate].setValid();

        updatedDV = true;
    }

    // load advertised distance vector
    dv_entry advertisement[NROUTERS];
    memcpy((void*)advertisement, advertisementBuf, sizeof(advertisement));

    // recalculate self's distance vector
    for (int dest = 0; dest < NROUTERS; dest++) {
        if (dest == m_self)
            continue;
        bool updatedEntry = false;
        m_entries[dest].setCost(min(m_entries[dest].cost(), m_entries[intermediate].cost(), advertisement[dest].cost(), m_entries[dest].nexthopName(), source, updatedEntry));
        if (updatedEntry) {
            updatedDV = true;
            m_entries[dest].setNexthopPort(portNoOf(source));
            m_entries[dest].setNexthopName(source);
        }
    }
    m_entries[intermediate].setCost(advertisement[m_self].cost());

    if (updatedDV) {
        print(originalEntries, nameOf(m_self), "Change detected!\nRouting table before change", true);
        print(advertisement, source, "DV that caused the change", false);
        print(m_entries, nameOf(m_self), "Routing table after change", false);
    }
}

// return name of indexed router
char DV::nameOf(int index) const {
    return (char)index + 'A';
}

// return index of router
int DV::indexOf(char router) const {
    return router - 'A';
}

// return port number of router
int DV::portNoOf(char router) {
    return m_portnos[router];
}

void DV::initMyaddr(int portno) {
    memset((char *)&m_myaddr, 0, sizeof(m_myaddr));
    m_myaddr.sin_family = AF_INET;
    m_myaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    m_myaddr.sin_port = htons(portno);
}

void DV::startTimer(node &n) {
    clock_gettime(CLOCK_MONOTONIC, &n.startTime);
}

bool DV::timerExpired(node &n) const {
    timespec tend= {0,0};
    clock_gettime(CLOCK_MONOTONIC, &tend);

    if (((double)tend.tv_sec + 1.0e-9*tend.tv_nsec) - ((double)n.startTime.tv_sec + 1.0e-9*n.startTime.tv_nsec) > 5)
        return true;
    else
        return false;
}

//-----------------
// HELPER FUNCTIONS
//-----------------

// return minimum cost and set updated flag
int DV::min(int originalCost, int selfToIntermediateCost, int intermediateToDestCost, char originalName, char newName, bool &updated) const {
    int newCost = selfToIntermediateCost + intermediateToDestCost;

    if (selfToIntermediateCost == -1 || intermediateToDestCost == -1) {
        return originalCost;
    } else if (originalCost == -1) {
        updated = true;
        return newCost;
    } else if (newCost < originalCost) {
        updated = true;
        return newCost;
    } else if (originalCost == newCost) {
        if (originalName <= newName)
            updated = false;
        else
            updated = true;
        return newCost;
    } else {
        return originalCost;
    }
}

// print a DV
// format: source, destination, port number of nexthop router, cost to destination
void DV::print(dv_entry dv[], char name, string msg, bool timestamp) const {
    if (timestamp) {
        time_t rawtime;
        time(&rawtime);
        cout << ctime(&rawtime);
    }

    cout<<msg<<": "<<name<<endl;
    int NROUTERSAV = 0;


    for (int dest = 0; dest < NROUTERS; dest++) {
        if(m_nodename[dv[dest].nexthopPort()] == 0) continue;
        if(nameOf(dest) == selfName) continue;

        cout<<"shortest path to node "<<nameOf(dest)<<": ";
        if (dv[dest].cost() == -1) {
            cout<<"not reachable INF"<<endl;
            continue;
        }

        cout<<"the next hop is "<<m_nodename[dv[dest].nexthopPort()]<<" and the cost is "<<(double)dv[dest].cost()<<endl;
    }
    cout << endl;
}
