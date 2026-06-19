#include <stdlib.h>
#include <pcap/pcap.h>
#include <iostream>
#include <stdio.h>
#include <cstring>
#include <vector>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#define SNAP_LEN 1518
#define SIZE_ETHERNET 14
#define ETHER_ADDR_LEN 6
#define	ETHERTYPE_IP 0x0800		    // IP 
#define	ETHERTYPE_ARP 0x0806		// Address resolution protocol
#define	ETHERTYPE_RARP 0x8035		// Reverse ARP 
#define IP_HL(ip) (((ip)->ip_vhl) & 0x0f)
#define IP_V(ip) (((ip)->ip_vhl) >> 4)
using namespace std;

// ethernet header 
typedef u_int tcp_seq;
struct sniff_ethernet {
        u_char  ether_dhost[ETHER_ADDR_LEN];    /* destination host address */
        u_char  ether_shost[ETHER_ADDR_LEN];    /* source host address */
        u_short ether_type;                     /* IP? ARP? RARP? etc */
};

// ip header 
struct sniff_ip {
        u_char  ip_vhl;                
        u_char  ip_tos;                 
        u_short ip_len;                 /* total length */
        u_short ip_id;                  
        u_short ip_off;                 
        #define IP_RF 0x8000            
        #define IP_DF 0x4000            
        #define IP_MF 0x2000            
        #define IP_OFFMASK 0x1fff       
        u_char  ip_ttl;                 
        u_char  ip_p;                   
        u_short ip_sum;                 
        struct  in_addr ip_src,ip_dst;  /* source and dest address */
};

// tcp header	
struct sniff_tcp {
        u_short th_sport;               /* source port */
        u_short th_dport;               /* destination port */
        tcp_seq th_seq;                 
        tcp_seq th_ack;                 
        u_char  th_offx2;  
		#define TH_OFF(th) (((th)->th_offx2 & 0xf0) >> 4)
        u_char  th_flags;
        #define TH_FIN  0x01
        #define TH_SYN  0x02
        #define TH_RST  0x04
        #define TH_PUSH 0x08
        #define TH_ACK  0x10
        #define TH_URG  0x20
        #define TH_ECE  0x40
        #define TH_CWR  0x80
        #define TH_FLAGS (TH_FIN|TH_SYN|TH_RST|TH_ACK|TH_URG|TH_ECE|TH_CWR)
        u_short th_win;                 
        u_short th_sum;                 
        u_short th_urp;                 
};

// udp header
struct sniff_udp {
         u_short uh_sport;               /* source port */
         u_short uh_dport;               /* destination port */
         u_short uh_ulen;                /* udp length */
         u_short uh_sum;                 
		 #define SIZE_UDP 8 //size of UDP header is 8 bytes

};
// printing the payload in hex as well as in ascii format
void print_hex_ascii_line(const u_char *payload, int len, int offset)
{

	int i;
	int gap;
	const u_char *ch;
	printf(" ");
	
	// hex 
	ch = payload;
	for(i = 0; i < len; i++) {
		printf("%02x ", *ch);
		ch++;
		/* print extra space after 8th byte for visual aid */
		if (i == 7)
			printf(" ");
	}
	/* print space to handle line less than 8 bytes */
	if (len < 8)
		printf(" ");
	
	/* fill hex gap with spaces if not full line */
	if (len < 16) {
		gap = 16 - len;
		for (i = 0; i < gap; i++) {
			printf("   ");
		}
	}
	printf("   ");
	
	/* ascii (if printable) */
	ch = payload;
	for(i = 0; i < len; i++) {
		if (isprint(*ch))
			printf("%c", *ch);
		else
			printf(".");
		ch++;
	}
	printf("\n");

	
return;
}
void print_payload(const u_char *payload, int len)
{
	int len_rem = len;
	int line_width = 16;			 
	int line_len;
	int offset = 0;					
	const u_char *ch = payload;

	if (len <= 0)
		return; 
	if (len <= line_width) {
		print_hex_ascii_line(ch, len, offset);
		return;
	}
	for ( ;; ) { 
		line_len = line_width % len_rem; 
		print_hex_ascii_line(ch, line_len, offset); 
		len_rem = len_rem - line_len;
		ch = ch + line_len; 
		offset = offset + line_width; 
		if (len_rem <= line_width) { 
			print_hex_ascii_line(ch, len_rem, offset);
			break;
		}
	}
return;
}

void pcap_handler_callback(u_char *args, const struct pcap_pkthdr *header,const u_char *packet)
{                    
	struct sniff_ethernet *ethernet;  
	struct sniff_ip *ip;              
	struct sniff_tcp *tcp;            
	struct sniff_udp *udp;
	u_char *payload;                    
	int size_ip;
	int size_protocol;
	int size_payload;
	int validPacket=-1;
	char buffer[80];
	struct tm * timeinfo; 
	u_char *ptr;
	int i;

	ethernet = (struct sniff_ethernet*)(packet);

	if(ntohs (ethernet->ether_type) == ETHERTYPE_IP)
	{
		ip = (struct sniff_ip*)(packet + SIZE_ETHERNET);
		size_ip = IP_HL(ip)*4;
		//size_ip = (((ip)->ip_vhl) & 0x0f)*4;
		if (size_ip < 20) 
		{
			printf("* Invalid IP header length: %u bytes\n", size_ip);
			return;
		}
	
		std::string protocolName;
		int srcPort = -1;
		int destPort = -1;
	
		switch(ip->ip_p) 
		{
			case IPPROTO_TCP:
				protocolName = "TCP";
				tcp = (struct sniff_tcp*)(packet + SIZE_ETHERNET + size_ip);
				size_protocol= TH_OFF(tcp)*4;
				if (size_protocol < 20) 
				{
					printf("   * Invalid TCP header length: %u bytes\n", size_protocol);
					return;
				}
				srcPort = ntohs(tcp->th_sport);
				destPort = ntohs(tcp->th_dport);
				payload = (u_char *)(packet + SIZE_ETHERNET + size_ip + size_protocol);
				size_payload = ntohs(ip->ip_len) - (size_ip + size_protocol);
				break;

			case IPPROTO_UDP:
				protocolName = "UDP";
				udp=(struct sniff_udp*)(packet + SIZE_ETHERNET + size_ip);
				size_protocol = SIZE_UDP;
				srcPort = ntohs(udp->uh_sport);
				destPort = ntohs(udp->uh_dport);
				payload = (u_char *)(packet + SIZE_ETHERNET + size_ip + size_protocol);
				size_payload = ntohs(ip->ip_len) - (size_ip + size_protocol);
				break;

			case IPPROTO_ICMP:
				protocolName = "ICMP";
				size_protocol = 8; //size of ICMP header is 8 bytes
				payload = (u_char *)(packet + SIZE_ETHERNET + size_ip + size_protocol);
				size_payload = ntohs(ip->ip_len) - (size_ip + size_protocol);
				break;
			
			default:
				protocolName = "Unknown Protocol";
				payload = (u_char *)(packet + SIZE_ETHERNET + size_ip);
				size_payload = ntohs(ip->ip_len) - size_ip;
				break;
		}
		char *payloadCopy = (char *)malloc(size_payload);
		char *payloadCopyPtr = payloadCopy;
		char c;
		memcpy(payloadCopy,payload,size_payload);
		for(i = 0; i < size_payload; i++) {
			if(!isprint(*payloadCopyPtr))
			{
				c='.';
				*payloadCopyPtr=c;	
			}
			*payloadCopyPtr++;	
		}

		if(args==NULL)
		{
			validPacket=1;
		}
		else if(strstr((char *)payloadCopy,(char *)args))
		{
			validPacket=1;
		}

		if(validPacket==1)
		{
			timeinfo = localtime((const time_t *)&header->ts.tv_sec);
			strftime(buffer,80,"%Y-%m-%d",timeinfo);
			printf("\n %s\n",buffer);
			
			ptr = ethernet->ether_shost;
			i = ETHER_ADDR_LEN;
			do{
				printf("%s%02x",(i == ETHER_ADDR_LEN) ? " " : ":",*ptr++);
			}while(--i>0);
			printf(" ->");

			ptr = ethernet->ether_dhost;
			i = ETHER_ADDR_LEN;
			do{
				printf("%s%02x",(i == ETHER_ADDR_LEN) ? " " : ":",*ptr++);
			}while(--i>0);

			printf(" type 0x%x (IP packet)",ntohs(ethernet->ether_type));

			printf(" len %d", header->len);

			printf("\n %s:", inet_ntoa(ip->ip_src));
			if(srcPort!=-1)
				printf("%d",srcPort);

			printf(" -> %s:", inet_ntoa(ip->ip_dst));
			if(destPort!=-1)
				printf("%d",destPort);

			printf(" %s\n",protocolName.c_str()); 

			if (size_payload > 0) 
			{
				print_payload(payload, size_payload);
			}
		}	
	}
	else
	{
		if(args==NULL)
		{
			timeinfo = localtime((const time_t *)&header->ts.tv_sec);
			strftime(buffer,80,"%Y-%m-%d",timeinfo);
			printf("\n %s\n",buffer);

			ptr = ethernet->ether_shost;
			i = ETHER_ADDR_LEN;
			do{
				printf("%s%02x",(i == ETHER_ADDR_LEN) ? " " : ":",*ptr++);
			}while(--i>0);
			printf(" -> ");

			ptr = ethernet->ether_dhost;
			i = ETHER_ADDR_LEN;
			do{
				printf("%s%02x",(i == ETHER_ADDR_LEN) ? " " : ":",*ptr++);
			}while(--i>0);

			if (ntohs (ethernet->ether_type) == ETHERTYPE_ARP)
	    	{
	        	printf(" type 0x%x (ARP packet)\n", ntohs(ethernet->ether_type));
	    	}
	    	else if (ntohs(ethernet->ether_type) == ETHERTYPE_RARP)
			{
				printf(" type 0x%x (RARP packet)\n", ntohs(ethernet->ether_type));
			}
			else
	    	{
	        	printf(" type 0x%x (not IP or ARP or RARP)\n", ntohs(ethernet->ether_type));
	    	}
    	}

	}
	return;	
}
int main(int argc, char **argv)
{
	char *dev, errbuf[PCAP_ERRBUF_SIZE];
	struct bpf_program fp;
	bpf_u_int32 mask;		
	bpf_u_int32 net;
	pcap_t *handle;
	struct pcap_pkthdr header;	// The header that pcap gives us 
	const u_char *packet;
	string interface, file, s, expression;
	int c, index;
    while ((c = getopt (argc, argv, "i:r:s:")) != -1)
    switch (c)
    {
            case 'i':
                    interface = string(optarg);
                    break;
            case 'r':
                    file = string(optarg);
                    break;
            case 's':
                    s = string(optarg);
                    break;
            default:
                    exit(0);
    }
    for (index = optind; index < argc; index++) {
            expression += " ";
            expression += string(argv[index]);
    }

    if(file!="")
    {
    	handle = pcap_open_offline(file.c_str(), errbuf);
    	if (handle == NULL) 
		{
			fprintf(stderr, "Couldn't open pcap file %s: %s\n", file.c_str(), errbuf);
			return(2);
		}
    }
    else
    {
    	if(interface!="" )
    	{
    		dev = (char*)malloc(sizeof(interface.length()+1));
 			strcpy(dev, interface.c_str());	
    	}
    	else
    	{
    		dev = pcap_lookupdev(errbuf);
			if (dev == NULL) {
				fprintf(stderr, "Couldn't find default device: %s\n", errbuf);
				return(2);
				}
    	}
    	if (pcap_lookupnet(dev, &net, &mask, errbuf) == -1) 
    	{
			fprintf(stderr, "Can't get netmask for device %s\n", dev);
			net = 0;
			mask = 0;
	 	}

		handle = pcap_open_live(dev, BUFSIZ, 1, 1000, errbuf);
		if (handle == NULL) 
		{
			fprintf(stderr, "Couldn't open device %s: %s\n", dev, errbuf);
			return(2);
		}
    }
	
	if (pcap_datalink(handle) != DLT_EN10MB) {
		fprintf(stderr, "%s is not an Ethernet\n", dev);
		exit(EXIT_FAILURE);
	}

	if(expression!="")
	{
		
		if (pcap_compile(handle, &fp, expression.c_str(), 0, net) == -1) 
		{
			fprintf(stderr, "Couldn't parse filter %s: %s\n", expression.c_str(), pcap_geterr(handle));
			return(2);
		}
		if (pcap_setfilter(handle, &fp) == -1) 
		{
			fprintf(stderr, "Couldn't install filter %s: %s\n", expression.c_str(), pcap_geterr(handle));
			return(2);
		}
	}
	
	if(s!="")
		pcap_loop(handle, 0, pcap_handler_callback, (u_char *)s.c_str());
	else
		pcap_loop(handle, 0, pcap_handler_callback, NULL);

	pcap_close(handle);
	return(0);

}