#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <getopt.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <linux/if_ether.h>
#include <features.h>


#define HAVE_GETOPT_H

#define usage printf
/**** Common definitions ****/
#define STATE_OK          0
#define STATE_WARNING     1
#define STATE_CRITICAL    2
#define STATE_UNKNOWN     -1
#define OK                0
#define ERROR             -1
#define FALSE             0
#define TRUE              1

/**** DHCP definitions ****/
#define MAX_DHCP_CHADDR_LENGTH           16
#define MAX_DHCP_SNAME_LENGTH            64
#define MAX_DHCP_FILE_LENGTH             128
#define MAX_DHCP_OPTIONS_LENGTH          312


typedef struct dhcp_packet_struct{
        u_int8_t  op;                   /* packet type */
        u_int8_t  htype;                /* type of hardware address for this machine (Ethernet, etc) */
        u_int8_t  hlen;                 /* length of hardware address (of this machine) */
        u_int8_t  hops;                 /* hops */
        u_int32_t xid;                  /* random transaction id number - chosen by this machine */
        u_int16_t secs;                 /* seconds used in timing */
        u_int16_t flags;                /* flags */
        struct in_addr ciaddr;          /* IP address of this machine (if we already have one) */
        struct in_addr yiaddr;          /* IP address of this machine (offered by the DHCP server) */
        struct in_addr siaddr;          /* IP address of DHCP server */
        struct in_addr giaddr;          /* IP address of DHCP relay */
        unsigned char chaddr [MAX_DHCP_CHADDR_LENGTH];      /* hardware address of this machine */
        char sname [MAX_DHCP_SNAME_LENGTH];    /* name of DHCP server */
        char file [MAX_DHCP_FILE_LENGTH];      /* boot file name (used for diskless booting?) */
	char options[MAX_DHCP_OPTIONS_LENGTH];  /* options */
        }dhcp_packet;


typedef struct dhcp_offer_struct{
	struct in_addr server_address;   /* address of DHCP server that sent this offer */
	struct in_addr offered_address;  /* the IP address that was offered to us */
	u_int32_t lease_time;            /* lease time in seconds */
	u_int32_t renewal_time;          /* renewal time in seconds */
	u_int32_t rebinding_time;        /* rebinding time in seconds */
	struct dhcp_offer_struct *next;
        }dhcp_offer;


typedef struct requested_server_struct{
	struct in_addr server_address;
	struct requested_server_struct *next;
        }requested_server;


#define BOOTREQUEST     1
#define BOOTREPLY       2

#define DHCPDISCOVER    1
#define DHCPOFFER       2
#define DHCPREQUEST     3
#define DHCPDECLINE     4
#define DHCPACK         5
#define DHCPNACK        6
#define DHCPRELEASE     7

#define DHCP_OPTION_MESSAGE_TYPE        53
#define DHCP_OPTION_HOST_NAME           12
#define DHCP_OPTION_BROADCAST_ADDRESS   28
#define DHCP_OPTION_REQUESTED_ADDRESS   50
#define DHCP_OPTION_LEASE_TIME          51
#define DHCP_OPTION_RENEWAL_TIME        58
#define DHCP_OPTION_REBINDING_TIME      59

#define DHCP_INFINITE_TIME              0xFFFFFFFF

#define DHCP_BROADCAST_FLAG 32768

#define DHCP_SERVER_PORT   67
#define DHCP_CLIENT_PORT   68

#define ETHERNET_HARDWARE_ADDRESS            1     /* used in htype field of dhcp packet */
#define ETHERNET_HARDWARE_ADDRESS_LENGTH     6     /* length of Ethernet hardware addresses */

unsigned char client_hardware_address[MAX_DHCP_CHADDR_LENGTH]="";
unsigned int my_client_mac[MAX_DHCP_CHADDR_LENGTH];
int mymac = 0;

char * network_interface_name;

u_int32_t packet_xid=0;

u_int32_t dhcp_lease_time=0;
u_int32_t dhcp_renewal_time=0;
u_int32_t dhcp_rebinding_time=0;

int dhcpoffer_timeout=2;

dhcp_offer *dhcp_offer_list=NULL;
requested_server *requested_server_list=NULL;

int valid_responses=0;     /* number of valid DHCPOFFERs we received */
int requested_servers=0;   
int requested_responses=0;

int request_specific_address=FALSE;
int received_requested_address=FALSE;
int verbose=0;
struct in_addr requested_address;
int create_dhcp_socket(void){
        struct sockaddr_in myname;
	struct ifreq interface;
        int sock;
        int flag=1;

        /* Set up the address we're going to bind to. */
	bzero(&myname,sizeof(myname));
        myname.sin_family=AF_INET;
        myname.sin_port=htons(DHCP_CLIENT_PORT);
        myname.sin_addr.s_addr=INADDR_ANY;                 /* listen on any address */
        bzero(&myname.sin_zero,sizeof(myname.sin_zero));

        /* create a socket for DHCP communications */
	sock=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
        if(sock<0){
		printf("Error: Could not create socket!\n");
		exit(STATE_UNKNOWN);
	        }

	if (verbose)
		printf("DHCP socket: %d\n",sock);

        /* set the reuse address flag so we don't get errors when restarting */
        flag=1;
        if(setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,(char *)&flag,sizeof(flag))<0){
		printf("Error: Could not set reuse address option on DHCP socket!\n");
		exit(STATE_UNKNOWN);
	        }

        /* set the broadcast option - we need this to listen to DHCP broadcast messages */
        if(setsockopt(sock,SOL_SOCKET,SO_BROADCAST,(char *)&flag,sizeof flag)<0){
		printf("Error: Could not set broadcast option on DHCP socket!\n");
		exit(STATE_UNKNOWN);
	        }

	/* bind socket to interface */
	strncpy(interface.ifr_ifrn.ifrn_name,network_interface_name,IFNAMSIZ);
	if(setsockopt(sock,SOL_SOCKET,SO_BINDTODEVICE,(char *)&interface,sizeof(interface))<0){
		printf("Error: Could not bind socket to interface %s.  Check your privileges...\n",network_interface_name);
		exit(STATE_UNKNOWN);
	        }


        /* bind the socket */
        if(bind(sock,(struct sockaddr *)&myname,sizeof(myname))<0){
		printf("Error: Could not bind to DHCP socket (port %d)!  Check your privileges...\n",DHCP_CLIENT_PORT);
		exit(STATE_UNKNOWN);
	        }

        return sock;
        }





int send_answer(int sock,int argc, char **argv){
	char *server_ip;
	char *offered_ip;

	if (argc>=2){network_interface_name=argv[1];
	}
	else {network_interface_name="wlp2s0"; printf("\nInterface autoset to wlp2s0 \n");}
	if (argc>=3) server_ip=argv[2];
	else {server_ip="192.168.1.254"; printf("\n ip server set to 192.168.1.254 \n");}
	if (argc>=4) offered_ip=argv[3];
	else {server_ip="192.168.1.35"; printf("\n ip server set to 192.168.1.35 \n");}
	dhcp_packet demand_packet;
	dhcp_offer offer_packet;
	struct sockaddr_in sockaddr_offer;
	struct sockaddr_in sockaddr_listen;
	
	sockaddr_listen.sin_addr.s_addr=inet_addr("0.0.0.0");
	

	int result;

	
	result=receive_dhcp_packet(&demand_packet,sizeof(demand_packet),sock,dhcpoffer_timeout,&sockaddr_listen);

	if(demand_packet.siaddr.s_addr==inet_addr("255.255.255.255")){

	verbose=1;
	/* clear the packet data structure */
	bzero(&offer_packet,sizeof(offer_packet));

	offer_packet.server_address.s_addr=inet_addr(server_ip);   /* address of DHCP server that sent this offer */
	offer_packet.offered_address.s_addr=inet_addr(offered_ip);  /* the IP address that was offered to us */
	offer_packet.lease_time=(u_int32_t)5;            /* lease time in seconds */
	offer_packet.renewal_time=(u_int32_t)10;          /* renewal time in seconds */
	offer_packet.rebinding_time=(u_int32_t)10;        /* rebinding time in seconds */
	offer_packet.next=NULL;


	
	
	
	/* send the DHCPOFFERpacket to broadcast address */
        sockaddr_offer.sin_family=AF_INET;
        sockaddr_offer.sin_port=htons(DHCP_CLIENT_PORT);
        sockaddr_offer.sin_addr.s_addr=INADDR_BROADCAST;
	bzero(&sockaddr_offer.sin_zero,sizeof(sockaddr_offer.sin_zero));


	
	

	/* send the DHCPDISCOVER packet out */
	send_dhcp_packet(&offer_packet,sizeof(offer_packet),sock,&sockaddr_offer);

	if (verbose) 
		printf("Ok");}

	return OK;

}

int send_dhcp_packet(void *buffer, int buffer_size, int sock, struct sockaddr_in *dest){
	struct sockaddr_in myname;
	int result;

	result=sendto(sock,(char *)buffer,buffer_size,0,(struct sockaddr *)dest,sizeof(*dest));

	if (verbose) 
		printf("send_dhcp_packet result: %d\n",result);

	if(result<0)
		return ERROR;

	return OK;
        }


/* receives a DHCP packet */
int receive_dhcp_packet(void *buffer, int buffer_size, int sock, int timeout, struct sockaddr_in *address){
        struct timeval tv;
        fd_set readfds;
	int recv_result;
	socklen_t address_size;
	struct sockaddr_in source_address;


        /* wait for data to arrive (up time timeout) */
        tv.tv_sec=timeout;
        tv.tv_usec=0;
        FD_ZERO(&readfds);
        FD_SET(sock,&readfds);
        select(sock+1,&readfds,NULL,NULL,&tv);

        /* make sure some data has arrived */
        if(!FD_ISSET(sock,&readfds)){
		if (verbose)
                	printf("No (more) data received\n");
                return ERROR;
                }

        else{

		/* why do we need to peek first?  i don't know, its a hack.  without it, the source address of the first packet received was
		   not being interpreted correctly.  sigh... */
		bzero(&source_address,sizeof(source_address));
		address_size=sizeof(source_address);
                recv_result=recvfrom(sock,(char *)buffer,buffer_size,MSG_PEEK,(struct sockaddr *)&source_address,&address_size);
		if (verbose)
			printf("recv_result_1: %d\n",recv_result);
                recv_result=recvfrom(sock,(char *)buffer,buffer_size,0,(struct sockaddr *)&source_address,&address_size);
		if (verbose)
			printf("recv_result_2: %d\n",recv_result);

                if(recv_result==-1){
			if (verbose) {
				printf("recvfrom() failed, ");
				printf("errno: (%d) -> %s\n",errno,strerror(errno));
			}
                        return ERROR;
                        }
		else{
			if (verbose) {
				printf("receive_dhcp_packet() result: %d\n",recv_result);
				printf("receive_dhcp_packet() source: %s\n",inet_ntoa(source_address.sin_addr));
			}

			memcpy(address,&source_address,sizeof(source_address));
			return OK;
		        }
                }

	return OK;
        }

int main(int argc, char **argv){	
	int sock=create_dhcp_socket();
	while(1){send_answer(sock,argc,argv);}	}

