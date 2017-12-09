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
char * server_ip;





int get_hardware_address(int,char *);

int send_dhcp_discover(int);
int get_dhcp_offer(int);

int get_results(void);

int create_dhcp_socket(void);
int close_dhcp_socket(int);
int send_dhcp_packet(void *,int,int,struct sockaddr_in *);
int receive_dhcp_packet(void *,int,int,int,struct sockaddr_in *);
int set_up_connection();

int main(int argc, char **argv){
	if (argc>=2){network_interface_name=argv[1];
				
	}
	else {network_interface_name="wlp2s0"; printf("\nInterface autoset to wlp2s0 \n");}
	if (argc>=3) server_ip=argv[2];
	else {server_ip="192.168.1.254"; printf("\n ip server set to 192.168.1.254 \n");
}
	int result;
	srand(42);
	int count=0;
	while(1){
	printf("#############  Begin DHCP procedure number %d\n",count);
	result=set_up_connection();
	printf("\n%d\n",result);
	++count;	
	}
	return 0;
	}


int set_up_connection(){
	int result;
	int dhcp_socket;
        /* create socket for DHCP communications */
	dhcp_socket=create_dhcp_socket();
	
	/* get hardware address of client machine */
	get_hardware_address(dhcp_socket,network_interface_name);

	/* send DHCPDISCOVER packet */

	//send_dhcp_discover(dhcp_socket);

	/* wait for a DHCPOFFER packet */
	get_dhcp_offer(dhcp_socket);

	/* close socket we created */
	//close_dhcp_socket(dhcp_socket);

	
	return dhcp_socket;
        }


/* determines hardware address on client machine */
int get_hardware_address(int sock,char *interface_name){

	int i;

	struct ifreq ifr;

	
	/* try and grab hardware address of requested interface */
	if(ioctl(sock,SIOCGIFHWADDR,&ifr)<0){
		printf("Error: Could not get hardware address of interface '%s'\n",interface_name);
		exit(STATE_UNKNOWN);
	}
	memcpy(&client_hardware_address[0],&ifr.ifr_hwaddr.sa_data,6);
	


	if (verbose) { 
		printf("Hardware address: ");
		for (i=0; i<6; ++i)
			printf("%2.2x", client_hardware_address[i]);
		printf( "\n");
	}

	return OK;
        }


/* sends a DHCPDISCOVER broadcast message in an attempt to find DHCP servers */
int send_dhcp_discover(int sock){
	dhcp_packet discover_packet;
	struct sockaddr_in sockaddr_broadcast;

	verbose=1;
	/* clear the packet data structure */
	bzero(&discover_packet,sizeof(discover_packet));


	/* boot request flag (backward compatible with BOOTP servers) */
	discover_packet.op=BOOTREQUEST;

	/* hardware address type */
	discover_packet.htype=ETHERNET_HARDWARE_ADDRESS;

	/* length of our hardware address */
	discover_packet.hlen=ETHERNET_HARDWARE_ADDRESS_LENGTH;

	discover_packet.hops=0;

	/* transaction id is supposed to be random */
	//srand(time(NULL));
	packet_xid=random();
	discover_packet.xid=htonl(packet_xid);

	/**** WHAT THE HECK IS UP WITH THIS?!?  IF I DON'T MAKE THIS CALL, ONLY ONE SERVER RESPONSE IS PROCESSED!!!! ****/
	/* downright bizzarre... */
	ntohl(discover_packet.xid);

	/*discover_packet.secs=htons(65535);*/
	discover_packet.secs=0xFF;

	/* tell server it should broadcast its response */ 
	discover_packet.flags=htons(DHCP_BROADCAST_FLAG);

	/* Set random hardware address */
	char *fake=malloc(sizeof(char)*10);
	sprintf(fake,"%d:%d:%d:%d:%d:%d",4,4,rand()%9,rand()%9,rand()%9,rand()%9);			
	printf("\n Fake MAC adress : %s\n",fake);
			
	sscanf(fake,"%x:%x:%x:%x:%x:%x", 
		my_client_mac+0,
		my_client_mac+1,
		my_client_mac+2,
		my_client_mac+3,
		my_client_mac+4,
		my_client_mac+5);
	for(int i=0;i<6;++i) client_hardware_address[i] = my_client_mac[i];
	
	memcpy(discover_packet.chaddr,client_hardware_address,ETHERNET_HARDWARE_ADDRESS_LENGTH);

	/* first four bytes of options field is magic cookie (as per RFC 2132) */
	discover_packet.options[0]='\x63';
	discover_packet.options[1]='\x82';
	discover_packet.options[2]='\x53';
	discover_packet.options[3]='\x63';

	/* DHCP message type is embedded in options field */
	discover_packet.options[4]=DHCP_OPTION_MESSAGE_TYPE;    /* DHCP message type option identifier */
	discover_packet.options[5]='\x01';               /* DHCP message option length in bytes */
	discover_packet.options[6]=DHCPDISCOVER;

	/* the IP address we're requesting */
	if(request_specific_address==TRUE){
		discover_packet.options[7]=DHCP_OPTION_REQUESTED_ADDRESS;
		discover_packet.options[8]='\x04';
		memcpy(&discover_packet.options[9],&requested_address,sizeof(requested_address));
	        }
	
	/* send the DHCPDISCOVER packet to broadcast address */
        sockaddr_broadcast.sin_family=AF_INET;
        sockaddr_broadcast.sin_port=htons(DHCP_SERVER_PORT);
        sockaddr_broadcast.sin_addr.s_addr=INADDR_BROADCAST;
	bzero(&sockaddr_broadcast.sin_zero,sizeof(sockaddr_broadcast.sin_zero));


	if (verbose) {
		printf("DHCPDISCOVER to %s port %d\n",inet_ntoa(sockaddr_broadcast.sin_addr),ntohs(sockaddr_broadcast.sin_port));
		printf("DHCPDISCOVER XID: %lu (0x%X)\n",(unsigned long) ntohl(discover_packet.xid),ntohl(discover_packet.xid));
		printf("DHCDISCOVER ciaddr:  %s\n",inet_ntoa(discover_packet.ciaddr));
		printf("DHCDISCOVER yiaddr:  %s\n",inet_ntoa(discover_packet.yiaddr));
		printf("DHCDISCOVER siaddr:  %s\n",inet_ntoa(discover_packet.siaddr));
		printf("DHCDISCOVER giaddr:  %s\n",inet_ntoa(discover_packet.giaddr));
	}

	/* send the DHCPDISCOVER packet out */
	send_dhcp_packet(&discover_packet,sizeof(discover_packet),sock,&sockaddr_broadcast);

	if (verbose) 
		printf("\n\n");

	return OK;
        }




/* waits for a DHCPOFFER message from one or more DHCP servers */
int get_dhcp_offer(int sock){
	dhcp_packet offer_packet;
	struct sockaddr_in source;
	int result=OK;
	int timeout=1;
	int responses=0;
	int x;
	time_t start_time;
	time_t current_time;

	time(&start_time);

	/* receive as many responses as we can */
	for(responses=0,valid_responses=0;;){

		time(&current_time);
		if((current_time-start_time)>=dhcpoffer_timeout)
			break;

		if (verbose) 
			printf("\n\n");

		bzero(&source,sizeof(source));
		bzero(&offer_packet,sizeof(offer_packet));

		result=OK;
		result=receive_dhcp_packet(&offer_packet,sizeof(offer_packet),sock,dhcpoffer_timeout,&source);
		
		if(result!=OK){
			if (verbose)
				printf("Result=ERROR\n");

			continue;
		        }
		else{
			if (verbose) 
				printf("Result=OK\n");

			responses++;
		        }

		if (verbose) {
			printf("DHCPOFFER from IP address %s\n",inet_ntoa(source.sin_addr));
			printf("DHCPOFFER XID: %lu (0x%X)\n",(unsigned long) ntohl(offer_packet.xid),ntohl(offer_packet.xid));
		}

		/* check packet xid to see if its the same as the one we used in the discover packet */
		if(ntohl(offer_packet.xid)!=packet_xid){
			if (verbose)
				printf("DHCPOFFER XID (%lu) did not match DHCPDISCOVER XID (%lu)\n",(unsigned long) ntohl(offer_packet.xid),(unsigned long) packet_xid);

			//continue;
		        }

		/* check hardware address */
		result=OK;
		if (verbose)
			printf("DHCPOFFER chaddr: ");

		for(x=0;x<ETHERNET_HARDWARE_ADDRESS_LENGTH;x++){
			if (verbose)
				printf("%02X",(unsigned char)offer_packet.chaddr[x]);

			if(offer_packet.chaddr[x]!=client_hardware_address[x])
				result=ERROR;
		}
		if (verbose)
			printf("\n");

		if(result==ERROR){
			if (verbose) 
				printf("DHCPOFFER hardware address did not match our own \n");

			//continue;
		        }

		if (verbose) {
			printf("DHCPOFFER ciaddr: %s\n",inet_ntoa(offer_packet.ciaddr));
			printf("DHCPOFFER yiaddr: %s\n",inet_ntoa(offer_packet.yiaddr));
			printf("DHCPOFFER siaddr: %s\n",inet_ntoa(offer_packet.siaddr));
			printf("DHCPOFFER giaddr: %s\n",inet_ntoa(offer_packet.giaddr));
		}

	
		valid_responses++;
	        }

	if (verbose) {
		printf("Total responses seen on the wire: %d\n",responses);
		printf("Valid responses for this machine: %d\n",valid_responses);
	}

	return OK;
        }



/* sends a DHCP packet */
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


/* creates a socket for DHCP communication */
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


/* closes DHCP socket */
int close_dhcp_socket(int sock){

	close(sock);

	return OK;
        }



