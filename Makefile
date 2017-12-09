all: dhcptest dhcp_starve
 
dhcp_starve: dhcp_starve.o
	cc -g -o dhcp_starve dhcp_starve.o
 
dhcp_starve.o: dhcp_starve.c
	cc -c -Wall -g dhcp_starve.c 

dhcptest: dhcptest.o
	cc -g -o dhcptest dhcptest.o
 
dhcp_test.o: dhcptest.c
	cc -c -Wall -g dhcptest.c 
clean:
	rm dhcp_starve.o dhcp_starve dhcptest dhcptest.o
