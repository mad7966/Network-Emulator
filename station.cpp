/* Course: CNT5505 - Data and Computer Communications
   Semester: Fall 2023
   Team: Madhu Gangadhar - mkg22b
         Kartik Gupta - kg22e
 */


#include <iostream>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#include <sys/select.h>
#include <fstream>
#include <vector>
#include <list>
#include <fcntl.h>
#include <iomanip>
#include "ip.h"
#include <cerrno>
#include <map>
#include <netdb.h>
#include <unordered_map>
#include "utils.h"


using namespace std;

Rtable getNextHop(vector<Rtable> rtable, IPAddr dst)
{
    // Find next hop IP address in routing table
    for (int j = 0; j < rtable.size(); j++)
        if((dst & rtable[j].mask) == rtable[j].destsubnet)
            // Return the nexthop in rtable (or dstip if on same LAN)
            return rtable[j];
}


bool GetMAC(IPAddr nexthop, const vector<ARP_Cache_Entry>& ARP_cache, EtherPkt& ether_packet) {
    // Iterate through ARP cache until a matching entry is found
    for (const auto& entry : ARP_cache) {
        if (entry.ip == nexthop) {
            // If an entry was found, copy over its mac into the ether packet destination
            strncpy(ether_packet.dst, entry.mac, 18);
            return true;
        }
    }

    // If no matching entry was found, set the mac to 'x' (empty)
    ether_packet.dst[0] = 'x';
    return false;
}


// Function to set socket to non-blocking mode
void setNonBlocking(int sockfd) {
    int socket_flag = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, socket_flag | O_NONBLOCK);
}

// Function to restore the blocking mode of the socket
void restoreBlocking(int sockfd, int socket_flag) {
    fcntl(sockfd, F_SETFL, socket_flag);
}

// Function to manage the TTL countdown for each ARP cache entry
void checkARPTimeout(vector<ARP_Cache_Entry>& ARP_cache) {
    time_t currentTime = time(NULL); // Get current time

    // Use an iterator to remove expired entries
    for (auto it = ARP_cache.begin(); it != ARP_cache.end();)
    {
        if (currentTime - it->timer >= it->ttl) {
            cout << "One entry in ARP-cache timed out" << endl;
            it = ARP_cache.erase(it); // Remove expired entry
        } else {
            ++it;
        }
    }
}


int main(int argc, char** argv)
{
    fd_set readset;
    struct hostent *host;
    struct sockaddr_in ma;
    char ip[20], message_buffer[10];
    char lanport_num[10], lan_address[20], 
         tempIP1[20], tempIP2[20], tempIP3[20], temp_mac_address[20];
    Iface temp_interface; 
    Rtable temp_route;
    Host temp_host;
    EtherPkt ether_packet;
    IP_PKT IP_pkt;
    ARP_PKT ARP_pkt;
    list<IP_PKT> pending_queue;
    vector<ARP_Cache_Entry> ARP_cache;
    IPAddr nextHop;
    ARP_Cache_Entry cache_entry;
    ifstream iface_file, rout_file, host_file, port_file, addr_file;
    string port_file_name, address_file_name;
    bool messageReceived;
    vector<Iface> ifaces; 
    vector<Rtable> rtable;
    vector<Host> host_list;

    int sockfd, maxfd = 0, result, socket_flag;

    if(!checkIfUserInputCorrect(argc)){
        return 1;
    }

    cout << "initializing.." << endl;

    iface_file.open(argv[2]);
    rout_file.open(argv[3]);
    host_file.open(argv[4]);

    cout << endl << "reading ifaces.."<< endl <<endl;
    while (iface_file >> temp_interface.ifacename >> tempIP1 >> tempIP2
                      >> temp_interface.macaddr >> temp_interface.lanname)
    {
        temp_interface.ipaddr = stringtoIP(tempIP1);
        temp_interface.netmask = stringtoIP(tempIP2);
        ifaces.push_back(temp_interface);
        cout << temp_interface.ifacename << " " << tempIP1 << " " << tempIP2 << " " << temp_interface.macaddr << " " << temp_interface.lanname << endl;
    }

    cout << endl <<  "reading rtables.."<< endl << endl;
    while (rout_file >> tempIP1 >> tempIP2 >> tempIP3 
                     >> temp_route.ifacename)
    {
        temp_route.destsubnet = stringtoIP(tempIP1);
        temp_route.nexthop = stringtoIP(tempIP2);
        temp_route.mask = stringtoIP(tempIP3);
        rtable.push_back(temp_route);
       cout << tempIP1 << " " << tempIP2 << " " << tempIP3 << " " << temp_route.ifacename << endl;

    }

    cout <<  endl << "reading hosts.."<< endl << endl;
    while (host_file >> temp_host.name >> ip)
    {
        temp_host.addr = stringtoIP(ip);
        host_list.push_back(temp_host);
        cout << temp_host.name << " " << ip << endl;
    }

    iface_file.close();
    rout_file.close();
    host_file.close();

   for (int i = 0; i < ifaces.size(); i++) {
    // Set up the client socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    ifaces[i].sockfd = sockfd;

    port_file_name = string(".") + ifaces[i].lanname + ".port";
    address_file_name = string(".") + ifaces[i].lanname + ".addr";
    readlink(port_file_name.c_str(), lanport_num, sizeof(lanport_num) - 1);
    readlink(address_file_name.c_str(), lan_address, sizeof(lan_address) - 1);

    // Set up the sockaddr_in structure
    ma.sin_family = AF_INET;
    ma.sin_port = htons(atoi(lanport_num));
    host = gethostbyname(lan_address);
    memcpy(&ma.sin_addr, host->h_addr, host->h_length);

    // Set socket to non-blocking
    setNonBlocking(sockfd);

    bool connection_accepted = false;

    // Attempt to connect for a preset number of times
    const int max_attempts = 5;
    const int wait_time_seconds = 2;

    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        // If connection fails...
        if (connect(sockfd, (struct sockaddr*)&ma, sizeof(ma)) == -1) {
            cerr << "Could not connect to server. Retrying...\n";
            std::this_thread::sleep_for(std::chrono::seconds(wait_time_seconds));
        } else {
            // Connection successful
            connection_accepted = true;
            break;
        }
    }

    // Restore the blocking mode
    restoreBlocking(sockfd, socket_flag);

    // If all connection attempts fail, declare rejection and exit
    if (!connection_accepted) {
        cerr << "Connection rejected by the bridge.\n";
        exit(0);
    }

    // Receive "accept" or "reject"
    recv(sockfd, message_buffer, 7, 0);
    cout << message_buffer << endl;
    if (strncmp("reject", message_buffer, 7) == 0)
        exit(0);

    // Get the port number of the server
    socklen_t maLen = sizeof(ma);
    getsockname(sockfd, (struct sockaddr*)&ma, &maLen);

    if (sockfd >= maxfd)
        maxfd = sockfd + 1;

    cout << "Connection accepted!\n";
}

    // Clear the message buffer
    bzero(&ether_packet, sizeof(EtherPkt));

    for(;true;)
    {
        
        FD_ZERO(&readset);

        FD_SET(fileno(stdin), &readset);
        for (int i = 0; i < ifaces.size(); i++)
            FD_SET(ifaces[i].sockfd, &readset);
        
        // Call function to check and remove expired entries from ARP cache
        checkARPTimeout(ARP_cache);

        // Block until activity on server socket or over stdin
        if(select(maxfd, &readset, NULL, NULL, NULL) > 0)
        {
            for (sockfd = fileno(stdin)+1; sockfd < maxfd; sockfd++)
            {
                // Check for message from server
                if(FD_ISSET(sockfd, &readset))
                {

                    result = recv(sockfd, &ether_packet, sizeof(EtherPkt), 0);
            
                    if(result == 0)
                    {
                        cout << "Disconnected from server.\n";
                        return 0;
                    }

                    if (!strcmp(ether_packet.src, "")); // Discard blank packets
                    else if (ether_packet.type == TYPE_IP_PKT)
                    {
                        // cout << "IP packet\n";// << IPtostring(IP_pkt.dstip) << endl;
                        recv(sockfd, &IP_pkt, sizeof(IP_PKT), 0);
                        messageReceived = 0;
                        string sourceStationName;
                        for (int j = 0; j < ifaces.size(); j++)
                        {
                            if (IP_pkt.dstip == ifaces[j].ipaddr)
                            {
                                cout << "Message of size " << IP_pkt.length << " received from station " << sourceStationName << endl ;
                                cout << ">>> " << IP_pkt.data << endl;
                                displayIPPacket(IP_pkt.dstip, IP_pkt.srcip, IP_pkt.data);
                                messageReceived = 1;
                            }
                        }
                        if (!strcmp(argv[1], "-route") && !messageReceived)
                        {
                            // Router functionality
                            cout << "Forwarding IP packet.\n";
                            // Get next hop IP
                            temp_route = getNextHop(rtable, IP_pkt.dstip);
                            if (temp_route.nexthop != 0)
                                IP_pkt.nexthop = temp_route.nexthop;
                            else
                                IP_pkt.nexthop = IP_pkt.dstip;

                            cout << "Destination IP = " << IPtostring(IP_pkt.dstip) << endl
                                 << "Sending to " << IPtostring(IP_pkt.nexthop) << endl;
                            for (int i = 0; i < ifaces.size(); i++)
                                if (!strncmp(temp_route.ifacename, ifaces[i].ifacename, 32))
                                    temp_interface = ifaces[i];

                            strncpy(ether_packet.src, temp_interface.macaddr, 18);

                            // look up in ARP cache
                            if (GetMAC(IP_pkt.nexthop, ARP_cache, ether_packet))
                            {
                                ether_packet.type = TYPE_IP_PKT;
                            }
                            // if in ARP cache, send IP
                            // otherwise, send ARP, add to pending queue
                            else
                            {
                                cout << "MAC address not in ARP cache, put IP packet in queue" << endl;
                                pending_queue.push_back(IP_pkt);

                                ether_packet.type = TYPE_ARP_PKT;

                                ARP_pkt.op = ARP_REQUEST;
                                ARP_pkt.srcip = temp_interface.ipaddr;
                                ARP_pkt.dstip = IP_pkt.nexthop;
                                strncpy(ARP_pkt.srcmac, ether_packet.src, 18);
                            }
                            send(temp_interface.sockfd, &ether_packet, sizeof(EtherPkt), 0);

                            // Send ARP or IP
                            if (ether_packet.type == TYPE_IP_PKT)
                                send(temp_interface.sockfd, &IP_pkt, sizeof(IP_PKT), 0);
                            else if (ether_packet.type == TYPE_ARP_PKT){
                                 send(temp_interface.sockfd, &ARP_pkt, sizeof(ARP_PKT), 0);
                                 cout << "Sent ARP request!" << endl;
                            }
                        }
                    }
                    else if (ether_packet.type == TYPE_ARP_PKT)
                    {
            
                        recv(sockfd, &ARP_pkt, sizeof(ARP_PKT), 0);
            
                        for (int i = 0; i < ifaces.size(); i++)
                        {
            
                            if (ARP_pkt.dstip == ifaces[i].ipaddr)
                            {
                                if (ARP_pkt.op == ARP_REQUEST)
                                {
                                    cout << "Got ARP request\n";
                                    cout << IPtostring(ARP_pkt.srcip) << "->" << IPtostring(ARP_pkt.dstip) << endl;
                                    if (ARP_pkt.dstip == ifaces[i].ipaddr)
                                    {
                                        //cout << "Sending response\n";
                                        MacAddr temp_mac;

                                        ARP_pkt.op = ARP_RESPONSE;

                                        // Swap ARP destination and source IP's
                                        IPAddr temp_interfacep = ARP_pkt.dstip;
                                        ARP_pkt.dstip = ARP_pkt.srcip;
                                        ARP_pkt.srcip = temp_interfacep;

                                        // Swap ARP destination and source macs
                                        strncpy(ARP_pkt.dstmac, ARP_pkt.srcmac, 18);
                                        strncpy(ARP_pkt.srcmac, ifaces[i].macaddr, 18);
                                          
                                        // Swap ether destination and source macs
                                        strncpy(ether_packet.dst, ether_packet.src, 18);
                                        strncpy(ether_packet.src, ifaces[i].macaddr, 18);

                                        cache_entry.ip = ARP_pkt.dstip;
                                        strncpy(cache_entry.mac, ARP_pkt.dstmac, 18);
                                        cache_entry.timer = time(NULL);
                                        cache_entry.ttl = 30;

                                        ARP_cache.push_back(cache_entry);

                                        send(ifaces[i].sockfd, &ether_packet, sizeof(EtherPkt), 0);
                                        send(ifaces[i].sockfd, &ARP_pkt, sizeof(ARP_PKT), 0);
                                    }
                                }
                                else if (ARP_pkt.op == ARP_RESPONSE)// ARP_RESPONSE
                                {
                                    cout << "Got ARP response, updating pending queue\n";
                                    if (ARP_pkt.dstip == ifaces[i].ipaddr)
                                    {
                                        for (list<IP_PKT>::iterator it = pending_queue.begin();
                                            it != pending_queue.end(); ++it)
                                        {
                                            cout << "Nexthop = " << IPtostring(it->nexthop)
                                                 << ' ' << IPtostring(ARP_pkt.srcip) << endl;
                                            if (ARP_pkt.srcip == it->nexthop)
                                            {
                                                ether_packet.type = TYPE_IP_PKT;
                                                strncpy(ether_packet.src, ifaces[i].macaddr, 18);
                                                strncpy(ether_packet.dst, ARP_pkt.srcmac, 18);

                                                cache_entry.ip = ARP_pkt.srcip;
                                                strncpy(cache_entry.mac, ARP_pkt.srcmac, 18);
                                                cache_entry.timer = time(NULL);
                                                cache_entry.ttl = 30;

                                                ARP_cache.push_back(cache_entry);


                                                IP_pkt = *it;

                                                pending_queue.erase(it);

                                                send(ifaces[i].sockfd, &ether_packet, sizeof(EtherPkt), 0);
                                                send(ifaces[i].sockfd, &IP_pkt, sizeof(IP_PKT), 0);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            // Check for input from stdin
            if(FD_ISSET(fileno(stdin), &readset))
            {
                string user_input;
                cin >> user_input;

                if(isQuitCommand(user_input)){
                    cout<<"quitting";
                    return 0;
                }
                else if (!user_input.compare("send"))
                {
                    char dest[32];
                    cin >> dest;

                    // Find the destination station's IP
                    for (int i = 0; i < host_list.size(); i++)
                    {
                        if (strncmp(dest, host_list[i].name, 32) == 0)
                        {
                            cout << "Destination IP = " << IPtostring(host_list[i].addr) << endl;
                            IP_pkt.dstip = host_list[i].addr;
                            break;
                        }
                    }

                    temp_route = getNextHop(rtable, IP_pkt.dstip);
                    if (temp_route.nexthop != 0)
                        IP_pkt.nexthop = temp_route.nexthop;
                    else
                        IP_pkt.nexthop = IP_pkt.dstip;

                    for (int i = 0; i < ifaces.size(); i++)
                        if (!strncmp(temp_route.ifacename, ifaces[i].ifacename, 32))
                            temp_interface = ifaces[i];

                    cout << "Next hop interface: " << temp_interface.ifacename << endl
                         << "Next hop IP: " << IPtostring(IP_pkt.nexthop) << endl;

                    // Copy over source MAC address
                    strncpy(ether_packet.src, temp_interface.macaddr, 18);


                    if (GetMAC(IP_pkt.nexthop, ARP_cache, ether_packet))
                    {
                        ether_packet.type = TYPE_IP_PKT;

                        IP_pkt.srcip = temp_interface.ipaddr;
                        cin.getline(IP_pkt.data, SHRT_MAX);
                        IP_pkt.length = strlen(IP_pkt.data);
                    }
                    else 
                    {
                        cin.getline(IP_pkt.data, SHRT_MAX);
                        IP_pkt.length = strlen(IP_pkt.data);
                        IP_pkt.srcip = temp_interface.ipaddr;
                        pending_queue.push_back(IP_pkt);

                        ether_packet.type = TYPE_ARP_PKT;

                        ARP_pkt.op = ARP_REQUEST;
                        ARP_pkt.srcip = temp_interface.ipaddr;
                        strncpy(ARP_pkt.srcmac, ether_packet.src, 18);
                        ARP_pkt.dstip = IP_pkt.nexthop;
                    }

                    cout << "IP packet destination IP: " << IPtostring(IP_pkt.dstip) << endl
                         << "IP packet source IP: " << IPtostring(IP_pkt.srcip) << endl
                         << "IP packet data: " << IP_pkt.data << endl;
                    
                    
                    send(temp_interface.sockfd, &ether_packet, sizeof(EtherPkt), 0);

                    // Send ARP or IP
                    if (ether_packet.type == TYPE_IP_PKT)
                        send(temp_interface.sockfd, &IP_pkt, sizeof(IP_PKT), 0);
                    else if (ether_packet.type == TYPE_ARP_PKT)
                        send(temp_interface.sockfd, &ARP_pkt, sizeof(ARP_PKT), 0);

                }
                else if (!user_input.compare("show"))
                {
                    cin >> user_input;

                    if (!user_input.compare("arp"))
                    {
                        cout << "*************************************************" << endl;
                        cout << "ARP Cache:" << endl;
                        cout << "*************************************************" << endl;

                        time_t currentTime = time(NULL); 

                        for (int c = 0; c < ARP_cache.size(); ++c)
                        {
                            int remainingTTL = ARP_cache[c].timer + ARP_cache[c].ttl - currentTime;
                            cout << left << setw(20) << IPtostring(ARP_cache[c].ip)
                                 << left << setw(20) << ARP_cache[c].mac
                                 << left << setw(20) << (remainingTTL > 0 ? remainingTTL : 0) << endl;
                        }
                        cout << "*************************************************" << endl;
                    }
                    else if (!user_input.compare("host"))
                    {
                        cout << "*************************************************" << endl;
                        cout << "Hosts:" << endl;
                        cout << "*************************************************" << endl;
                        for (int c = 0; c < host_list.size(); ++c)
                        {
                           cout << left << setw(10) << host_list[c].name
                                << IPtostring(host_list[c].addr) << endl;
                        }
                         cout << "*************************************************" << endl;
                    }
                    else if (!user_input.compare("iface"))
                    {
                        cout << "*************************************************" << endl;
                        cout << "iface_list" << endl;
                        cout << "*************************************************" << endl;
                        for (int c = 0; c < ifaces.size(); ++c)
                        {
                            cout << left << setw(10) << ifaces[c].ifacename
                                 << left << setw(20) << IPtostring(ifaces[c].ipaddr)
                                 << left << setw(20) << IPtostring(ifaces[c].netmask)
                                 << left << setw(20) << ifaces[c].macaddr << endl;
                        }
                         cout << "*************************************************" << endl;
                    }
                    else if (!user_input.compare("rtable"))
                    {
                        cout << "*************************************************" << endl;
                        cout << "rtable" << endl;
                        cout << "*************************************************" << endl;
                        for (int c = 0; c < rtable.size(); ++c)
                        {
                            cout << left << setw(20) << IPtostring(rtable[c].destsubnet)
                                 << left << setw(20) << IPtostring(rtable[c].nexthop)
                                 << left << setw(20) << IPtostring(rtable[c].mask)
                                 << left << setw(6) << rtable[c].ifacename << endl;
                        }
                         cout << "*************************************************" << endl;
                    }
                    else if (!user_input.compare("pq"))
                    {
                        cout << "*************************************************" << endl;
                        cout << "pending queue" << endl;
                        cout << "*************************************************" << endl;
                        for (auto it = pending_queue.begin(); it != pending_queue.end(); ++it)
                        {
                            const IP_PKT& pkt = *it;
                            cout << "Destination IP: " << IPtostring(pkt.dstip) << "\n";
                            cout << "Next Hop IP: " << IPtostring(pkt.nexthop) << "\n";
                            cout << "Data: " << pkt.data << "\n";
                            cout << "------------------------\n";
                        }
                        cout << "*************************************************" << endl;
                    }

                }
            }

            bzero(&ether_packet, sizeof(EtherPkt));
            bzero(&ARP_pkt, sizeof(ARP_PKT));
            bzero(&IP_pkt, sizeof(IP_PKT));

        }
    }

    return 0;
}
