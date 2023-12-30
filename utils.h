/* Course: CNT5505 - Data and Computer Communications
   Semester: Fall 2023
   Team: Sai Kalyan Tarun Tiruchirapally - ST22Q
         Shivam Agnihotri - SA22B
 */


#ifndef UTILS_H
#define UTILS_H

#include "ip.h"
#include <vector>
#include <string>
#include <cstdlib>
#include <chrono>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <iomanip>
#include <map>
#include <list>
#include <unordered_map>


using namespace std;

/**
 * Utility function to check if the user input is correct while starting the station.
*/
bool checkIfUserInputCorrect(int argc){
    // Verify the correct number of arguments was provided
    if(argc != 5)
    {
        cout << "Usage: chatclient <flags> <interface> <routingtable> <hostname>\n";
        return false;
    }
    return true;
}

/**
 * Utility function to convert a string formatted IP to an unsigned integer value.
*/
IPAddr stringtoIP (const char * a)
{
    IPAddr IP = 0;

    string temp = "";

    int j = 0;
    for (int i = 0; i < 4; i++)
    {
        while (a[j] != '.')
            temp.push_back(a[j++]);
        // add segment of IP left-shifted by a multiple of 8 bits
        IP += atoi(temp.c_str()) << 8*i;
        temp = ""; 
        j++;
    }

    return IP;
}



/**
 * Utility function to convert Ip address to string format.
*/
string IPtostring (IPAddr IP)
{
    string temp;
    char tempstr[10];

    for (int i = 0; i < 4; i++)
    {
        sprintf(tempstr, "%lu", IP&((1<<8)-1));
        // Append first 8 bits of IP to string
        temp.append(tempstr);
        if (i != 3)
            temp.push_back('.');

        IP >>= 8;
    }

    return temp; 
}

/**
 * Function to check if the user command is quit?
*/
bool isQuitCommand(const string& input) {
    return input == "quit";
}


/**
 * Utility function to print the table border.
*/
void  printTableBorder(){
    cout << "*************************************************" << endl;
}

/**
 * Utility function to print the show table commands.
*/
void tableHeaderPrinter(string heading){
    printTableBorder();
    cout << heading << endl;
    printTableBorder();
}

/**
 * Displays the IP packet.
*/
void displayIPPacket(IPAddr dstIP, IPAddr srcIP, string data ){
    tableHeaderPrinter("IP Packet");
    cout << "dstip: " << IPtostring(dstIP) << setw(17) << "srcip: " << IPtostring(srcIP) << endl;
    cout << "data:  " << data << endl;
    printTableBorder();
}

#endif