/* Course: CNT5505 - Data and Computer Communications
   Semester: Fall 2023
   Team: Sai Kalyan Tarun Tiruchirapally - ST22Q
         Shivam Agnihotri - SA22B
 */

#ifndef ETHER_H
#define ETHER_H

#include <climits>

#define PEER_CLOSED 2
#define TYPE_IP_PKT 1
#define TYPE_ARP_PKT 0

typedef char MacAddr[18];

/* structure of an ethernet pkt */
typedef struct __etherpkt 
{
  /* destination address in net order */
  MacAddr dst;

  /* source address in net order */
  MacAddr src;

  /************************************/
  /* payload type in host order       */
  /* type = 0 : ARP frame             */
  /* type = 1 : IP  frame             */
  /************************************/
  short  type;
  
  /* size of the data in host order */
  //short   size;

  /* actual payload */
  //char  dat[SHRT_MAX];

} EtherPkt;

#endif
