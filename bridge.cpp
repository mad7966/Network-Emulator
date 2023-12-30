/* Course: CNT5505 - Data and Computer Communications
   Semester: Fall 2023
   Team: Sai Kalyan Tarun Tiruchirapally - ST22Q
         Shivam Agnihotri - SA22B
 */

#include <unistd.h>
#include <iostream>
#include <csignal>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <thread>
#include <vector>
#include <fstream>
#include <algorithm>  
#include <netdb.h>
#include "ip.h"


#define DOMAIN_NAME_LENGTH 256
#define MESSAGE_LENGTH 200
#define TIMEOUT 30

using namespace std;

//bridge table structure
struct bridge_tbl
{
    int sockfd; //socket file descriptor
    MacAddr macaddr; //mac address
    time_t timer; //entry timer tracking
    int ttl; // entry time to live
    bridge_tbl(int a, MacAddr m, time_t t, int ttl_val) : sockfd(a), timer(t), ttl(ttl_val)
    {
        strncpy(macaddr, m, 18);
    }
};


// client structure
struct client_t
{
   int port; //port
   char name[DOMAIN_NAME_LENGTH]; // client name
};


static volatile int noInterupt = 1;

int default_ttl = 100; 

// all function declaration
bool isPrintableAndValidMAC(const char* str);
void removeInvalidEntries(std::vector<bridge_tbl>& bridge_table);
void removeExpiredEntries(std::vector<bridge_tbl>& bridge_table);
void handle_interrupt(int dummy);


// handling interrupt
void handle_interrupt(int dummy) 
{
    noInterupt = 0;
}

// removing expired bridge table entries
void removeExpiredEntries(std::vector<bridge_tbl> &bridge_table)
{
    int ttl_check = 5; // TTL check interval in seconds

    while (noInterupt)
    {
        std::this_thread::sleep_for(std::chrono::seconds(ttl_check));

        auto currentTime = time(NULL);

        // Use an iterator to remove expired entries
        for (auto it = bridge_table.begin(); it != bridge_table.end();)
        {
            if (currentTime - it->timer >= it->ttl) {
                cout << "Removing entry for MAC: " << it->macaddr << endl;
                it = bridge_table.erase(it); // Remove expired entry
            } else {
                ++it;
            }
        }
    }
}


// checking if mac address is valid or not
bool isPrintableAndValidMAC(const char* str) {
    // Check if the MAC address is printable and has the correct format
    if (strlen(str) != 17 || std::count(str, str + 17, ':') != 5)
        return false;

    // Check if each character is a valid hexadecimal digit or ':'
    for (int i = 0; i < 17; i++) {
        if (i % 3 != 2 && !isxdigit(str[i])) {
            return false;
        }
    }

    return true;
}

// removing invalid entries from the bridge table
void removeInvalidEntries(std::vector<bridge_tbl>& bridge_table) {
    // Remove entries with invalid MAC addresses
    bridge_table.erase(
        std::remove_if(bridge_table.begin(), bridge_table.end(),
                       [](const bridge_tbl& entry) {
                           return !isPrintableAndValidMAC(entry.macaddr);
                       }),
        bridge_table.end());
}


/* bridge : recvs pkts and relays them */
/* usage: bridge lan-name max-port */
int main (int argc, char *argv[]) 
{
    /* create the symbolic links to its address and port number
     * so that others (stations/routers) can connect to it
     */
    
    if (argc != 3)
    {
        cout << "Usage: bridge <lan-nam> <num-ports>" << endl;
        exit(0);
    }

    EtherPkt ether_pkt;
    
    IP_PKT IP_pkt;
    
    ARP_PKT ARP_pkt;
    
    char socket_buffer[SHRT_MAX];
    
    vector<bridge_tbl> bridge_table;
    
    string input;
    
    ofstream port_file, addr_file;
    
    int servfd, maxfd, max_ports = atoi(argv[2]) + 4;
    
    struct sockaddr_in serverAddr, newaddr;
    
    socklen_t length;
    
    fd_set readset;
    
    client_t clients[max_ports];
    
    char domainName[DOMAIN_NAME_LENGTH], message[MESSAGE_LENGTH];
    
    string port_fname, addr_fname;
    
    struct hostent *host;
   
    // This is used so that ctrl+C can be properly handled
    signal(SIGINT, handle_interrupt);

    // Initialize the message socket_buffer and client arrays to 0
    bzero (clients,max_ports*sizeof(client_t));
    bzero (message, MESSAGE_LENGTH*sizeof(char));

    // Open a new socket
    servfd = socket(AF_INET, SOCK_STREAM, 0);
    
    // Variable to keep track of the largest file descriptor
    maxfd = servfd + 1;

    serverAddr.sin_port = 0;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_family = AF_INET;

    // Bind the socket to a free port (by specifying a port of 0)
    length = sizeof(serverAddr);
    bind (servfd, (struct sockaddr *) &serverAddr, length);
    listen (servfd, maxfd);

    // Get the sockaddr that was created to find the assigned port
    getsockname(servfd, (struct sockaddr *) &serverAddr, &length);
    clients[servfd].port = ntohs(serverAddr.sin_port);

    // Get the server host name
    // Not needed?
    gethostname(domainName, DOMAIN_NAME_LENGTH);
    host = gethostbyname(domainName);
    strcpy(clients[servfd].name, host->h_name);

    port_fname = string(".") + argv[1] + ".port";
    addr_fname = string(".") + argv[1] + ".addr";

    symlink(to_string(clients[servfd].port).c_str(), port_fname.c_str());
    symlink(inet_ntoa((*(struct in_addr *) host->h_addr)), addr_fname.c_str());
   
    cout << "Bridge created on " << inet_ntoa((*(struct in_addr *) host->h_addr)) << ":" << clients[servfd].port << endl;
    

    /* listen to the socket.
     * two cases:
     * 1. connection open/close request from stations/routers
     * 2. regular data ether_pkts
     */

    // Start a separate thread to periodically remove expired entries
    std::thread ttl_thread(removeExpiredEntries, std::ref(bridge_table));

    while(noInterupt)
    {
        
        // Reset the read status of all sockets at the beginning of each loop
        FD_ZERO(&readset);

        // Set the read status for the server and active client sockets
        FD_SET(servfd, &readset);
        FD_SET(fileno(stdin), &readset);
        for(int i = 0; i < max_ports; i++)
            if (clients[i].port != 0)
                FD_SET(i, &readset);

        // Block until activity on one of the sockets
        if(select(maxfd, &readset, NULL, NULL, NULL) > 0)
        {
            // If activity on the server, there is a new client
            if(FD_ISSET(servfd, &readset))
            {
                int newfd;
                length = sizeof(newaddr);
                newfd = accept(servfd, (struct sockaddr *) &newaddr, &length);

                if (newfd < max_ports)
                {
                    send(newfd, "accept", sizeof("accept"), 0);

                    // Get the client's port and host name
                    clients[newfd].port = ntohs(newaddr.sin_port);
                    host = gethostbyaddr((const void*)&newaddr.sin_addr,4,AF_INET);
                    //strcpy(clients[newfd].name, host->h_name);
                    if (newfd >= maxfd)
                        maxfd = newfd + 1;
                    cout << "Accepting a new host on sockfd " << newfd << ", port "  << clients[newfd].port << "!" << endl;
                }
                else
                {
                    send(newfd, "reject", sizeof("recent"), 0);
                    close(newfd);
                }
            }
            if (FD_ISSET(fileno(stdin), &readset))
            {
                getline(cin, input);
                if (!input.compare("show sl"))
                {
                    if (bridge_table.size() == 0)
                        cout << "Table is empty.\n";
                    else 
                    {
                        time_t currentTime = time(NULL);
                        cout << "*************************************************\n";
                        cout << "Bridge self-learning table:\n"
                             << "MAC address        Port  Socket  TTL\n";
                        cout << "*************************************************\n";
                            
                        // Remove invalid entries from the bridge table
                        removeInvalidEntries(bridge_table);

                        for (int i = 0; i < bridge_table.size(); i++)
                            {
                                int remainingTTL = bridge_table[i].timer + bridge_table[i].ttl - currentTime;
                              
                                cout <<  bridge_table[i].macaddr << "  "
                                    << clients[bridge_table[i].sockfd].port << "  "
                                    << bridge_table[i].sockfd << "  "
                                    << (remainingTTL > 0 ? remainingTTL : 0) << endl;
                            }

                        cout << "*************************************************\n"; 
                    }
                } 
                else if (input == "quit") {
                    cout << "Quitting the bridge.\n";
                    noInterupt = 0; // Set the flag to exit the loop
                }
            }
            // If activity from one of the clients, retrieve its message
            else
            {
                for(int i = 3; i < max_ports; ++i)
                {
                    if(FD_ISSET(i, &readset))
                    {
                        // Get ether_pkt and check for disconnection
                        if(recv(i, &ether_pkt, sizeof(EtherPkt), 0) == 0) 
                        {
                            close(i);
                            if(i == (maxfd - 1))
                                maxfd = maxfd - 1;

                            cout << "admin: disconnect from '" 
                                 << clients[i].name << "("                      
                                 << clients[i].port << ")'\n";
                            // Zero port is used to show socket is inactive
                            clients[i].port = 0;
                        }
                        // If message recieved, echo to all other clients
                        else if (ether_pkt.src[0])
                        {
                            cout << "Received ethernet header" << endl;
                            
                            if (ether_pkt.type == TYPE_IP_PKT)
                            {
                                recv(i, &IP_pkt, sizeof(IP_PKT), 0);
                                cout << "Received " << sizeof(IP_PKT)
                                     << " byte IP frame\n";
                            }
                            else if (ether_pkt.type == TYPE_ARP_PKT)
                            {
                                recv(i, &ARP_pkt, sizeof(ARP_PKT), 0);
                                cout << "Received " << sizeof(ARP_PKT)
                                     << " byte ARP frame\n";
                            }

                            bool found = 0;

                            // Check if src mac is already in bridge table
                            for (int j = 0; j < bridge_table.size(); ++j)
                            {
                                if (!strncmp(ether_pkt.src, bridge_table[j].macaddr, 18))
                                {
                                    if (bridge_table[j].timer + TIMEOUT > time(NULL))
                                    {
                                        found = 1;
                                        bridge_table[j].timer = time(NULL);
                                    }
                                    break;
                                }
                            }
                            if (!found)
                            {
                                bridge_table.push_back(
                                    bridge_tbl(i, ether_pkt.src, time(NULL), default_ttl));
                            }
                            
                            found = 0;
                            // Check to see if we should search for dst mac in bridge table
                            if (ether_pkt.dst[0] != 'x')
                            {

                                for (int j = 0; j < bridge_table.size(); ++j)
                                {
                                    if (!strncmp(ether_pkt.dst, bridge_table[j].macaddr, 18))
                                    {
                                        // MAC found in table
                                        //cout << "Sending ether_pkt to " << bridge_table[j].sockfd << endl;
                                        send(bridge_table[j].sockfd, &ether_pkt, sizeof(EtherPkt), 0);
                                        if (ether_pkt.type == TYPE_IP_PKT)
                                        {
                                            send(bridge_table[j].sockfd, &IP_pkt, sizeof(IP_PKT), 0);
                                        }
                                        else if (ether_pkt.type == TYPE_ARP_PKT)
                                        {
                                            send(bridge_table[j].sockfd, &ARP_pkt, sizeof(ARP_PKT), 0);
                                        }
                                        found = 1;
                                        break;
                                    }
                                }
                            }
                            if (!found)
                            {
                                for(int j = 0; j < max_ports; ++j)
                                {
                                    if(clients[j].port && j != i &&  j != servfd)
                                    {
                                        send(j, &ether_pkt, sizeof(EtherPkt), 0);
                                        if (ether_pkt.type == TYPE_IP_PKT)
                                        {
                                            cout << "Broadcasting " << sizeof(IP_PKT)
                                                 << " byte ip_pkt\n";
                                            send(j, &IP_pkt, sizeof(IP_PKT), 0);
                                        }
                                        else if (ether_pkt.type == TYPE_ARP_PKT)
                                        {
                                            cout << "Broadcasting " << sizeof(ARP_PKT)
                                                 << " byte arp_pkt\n";
                                            send(j, &ARP_pkt, sizeof(ARP_PKT), 0);
                                        }
                                    }
                                }
                            }
                        } 

                        // Clear the message socket_buffer afterward
                        bzero(&ether_pkt, sizeof(EtherPkt));
                        bzero(&ARP_pkt,  sizeof(ARP_PKT));
                        bzero(&IP_pkt, sizeof(IP_PKT));

                    }
                }
            }
        }
    }


    // Wait for the TTL thread to finish (optional)
    ttl_thread.join();

    cout << "\nShutting down.\n";

    close(servfd);
    return 0;

}