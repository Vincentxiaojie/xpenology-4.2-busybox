#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <arpa/inet.h>
#include <errno.h>
#include <syslog.h>
#include <time.h>
#include <linux/limits.h>

#include "pathnames.h"

#if 1
	#ifdef	__MAIN__
		#define	DEBUGMSG(x...)	printf(x);
	#else
		#define	DEBUGMSG(x...)	syslog(LOG_ERR, x);
	#endif
	
#else
#define DEBUGMSG(x...)
#endif

#define	IPRANGE_MIN	"169.254.0.0"
//#define	IPRANGE_MAX	0xA9FEFEFEU	// 169.254.254.254
#define	IPMASK		"255.255.0.0"

int SYNOSetIP(const char *szInterface, const in_addr_t IpAddr, const in_addr_t MaskAddr);
int GetHWAddr(const char *szInterface, unsigned char *crgHwAddr, const int Length);
int PeekFd(int fd, time_t tv_usec);
int SYNOARPCheck(const char *szInterface, const in_addr_t TargetAddr);
int SYNOLinkDetect(const char *szDevname);
int SYNOSetTmpIP(const char *szInterface);

/* 
 * Set ip/mask on szInterface
 * You can use inet_addr() to get the in_addr_t format. eg:
 *
 *      IpAddr = inet_addr("192.168.1.50");
 *
 * Reture value:
 *  0: Success
 * -1: Failed
 */
int SYNOSetIP(const char *szInterface, const in_addr_t IpAddr, const in_addr_t MaskAddr)
{
	struct ifreq	ifrInfo;
	struct sockaddr_in	*p = (struct sockaddr_in *)&(ifrInfo.ifr_addr);
	char szPath[PATH_MAX];
	int ctlSocket = -1, err = -1;
	FILE *pFile = NULL;
	in_addr_t Network, Broadcast;

    if (szInterface == NULL) {
		syslog(LOG_ERR, "%s (%d) szInterface is NULL.", __FILE__, __LINE__);
		return -1;
	}

	ctlSocket = socket( PF_INET, SOCK_DGRAM, 0 );
	if (ctlSocket == -1) {
		syslog(LOG_ERR, "%s (%d) Failed to open socket. (%s)", __FILE__, __LINE__, strerror(errno));
		return -1;
	}

	memset(&ifrInfo,0,sizeof(struct ifreq));
	memcpy(ifrInfo.ifr_name, szInterface, strlen(szInterface));
	p->sin_family = AF_INET;
	p->sin_addr.s_addr = IpAddr;
	if ( ioctl(ctlSocket,SIOCSIFADDR,&ifrInfo) == -1 )	 /* setting IP address */
	{
		syslog(LOG_ERR, "%s (%d) ioctl SIOCSIFADDR (0x%x) (%s)", 
			   __FILE__, __LINE__, IpAddr, strerror(errno));
		goto Err;
	}
	
	p->sin_addr.s_addr = MaskAddr;
	if ( ioctl(ctlSocket,SIOCSIFNETMASK,&ifrInfo) == -1 )	/* setting netmask */
	{
		syslog(LOG_ERR, "%s (%d) ioctl SIOCSIFNETMASK (0x%x) (%s)", 
			   __FILE__, __LINE__, MaskAddr, strerror(errno));
		goto Err;
	}

	snprintf(szPath, sizeof(szPath), DHCP_HOSTINFO, SZD_CONFIG_DIR, szInterface);
	pFile = fopen(szPath, "w");
	if (NULL == pFile) {
		syslog(LOG_ERR, "%s (%d) Failed to open %s", __FILE__, __LINE__, szPath);
		goto Err;
	}
	Network = IpAddr & MaskAddr;
	Broadcast = Network | ~MaskAddr;
	fprintf(pFile, "IPADDR=%u.%u.%u.%u\n"
			"NETMASK=%u.%u.%u.%u\n"
			"NETWORK=%u.%u.%u.%u\n"
			"BROADCAST=%u.%u.%u.%u\n"
			"INTERFACE=%s\n",
			((unsigned char *)&IpAddr)[0], ((unsigned char *)&IpAddr)[1],
			((unsigned char *)&IpAddr)[2], ((unsigned char *)&IpAddr)[3],
			((unsigned char *)&MaskAddr)[0], ((unsigned char *)&MaskAddr)[1],
			((unsigned char *)&MaskAddr)[2], ((unsigned char *)&MaskAddr)[3],
			((unsigned char *)&Network)[0], ((unsigned char *)&Network)[1],
			((unsigned char *)&Network)[2], ((unsigned char *)&Network)[3],
			((unsigned char *)&Broadcast)[0], ((unsigned char *)&Broadcast)[1],
			((unsigned char *)&Broadcast)[2], ((unsigned char *)&Broadcast)[3], 
			szInterface);
	err = 0;
Err:
	if (NULL != pFile) {
		fclose(pFile);
	}
	if (ctlSocket != -1) {
		close(ctlSocket);
	}
	return err;
}

/* Get the hardware address of szInterface (MAC address)
 * To print the MAC address: 
 *
 *   printf("MAC address = %02x:%02x:%02x:%02x:%02x:%02x\n",
 *           crgHwAddr[0], crgHwAddr[1], crgHwAddr[2],
 *           crgHwAddr[3], crgHwAddr[4], crgHwAddr[5]);
 *
 * Return Value:
 *   0: Success
 *  -1: Failed (error or interface is not an ehternet interface
 */
int GetHWAddr(const char *szInterface, unsigned char *crgHwAddr, const int Length)
{
	struct ifreq	ifrInfo;
	int ctlSocket = -1, err = -1;
    
	if ((szInterface == NULL) || (crgHwAddr == NULL) || (Length == 0)) {
		syslog(LOG_ERR, "%s (%d) Parameter error", __FILE__, __LINE__);
		return -1;
	}

	ctlSocket = socket( PF_INET, SOCK_DGRAM, 0 );
	if (ctlSocket == -1) {
		syslog(LOG_ERR, "%s (%d) Failed to open socket.(%s) ", __FILE__, __LINE__, strerror(errno));
		goto Err;
	}

	bzero(crgHwAddr, Length);

	memset(&ifrInfo, 0, sizeof(struct ifreq));
	memcpy(ifrInfo.ifr_name, szInterface, strlen(szInterface));
	if ( ioctl(ctlSocket, SIOCGIFHWADDR, &ifrInfo) )
	{
		syslog(LOG_ERR, "%s (%d) Failed to ioctl SIOCGIFHWADDR. Interface:%s (%s)", 
			   __FILE__, __LINE__, szInterface, strerror(errno));
		goto Err;
	}
	if ( ifrInfo.ifr_hwaddr.sa_family != ARPHRD_ETHER && ifrInfo.ifr_hwaddr.sa_family != ARPHRD_IEEE802 )
	{
		syslog(LOG_ERR, "%s (%d) Interface %s is not Ethernet or 802.2 Token Ring\n", 
			   __FILE__, __LINE__, ifrInfo.ifr_name);
		goto Err;
	}
	memcpy(crgHwAddr,ifrInfo.ifr_hwaddr.sa_data, Length);
	
	DEBUGMSG("%s (%d) MAC address of interface %s is [%02x:%02x:%02x:%02x:%02x:%02x]\n",
			 __FILE__, __LINE__, szInterface,
			 crgHwAddr[0], crgHwAddr[1], crgHwAddr[2],
			 crgHwAddr[3], crgHwAddr[4], crgHwAddr[5]);
	err = 0;

Err:
	if (ctlSocket != -1) {
		close(ctlSocket);
	}
	return err;
}

int PeekFd(int fd, time_t tv_usec)
{
	fd_set fs;
	struct timeval tv;

	FD_ZERO(&fs);
	FD_SET(fd, &fs);
	tv.tv_sec = tv_usec/1000000;
	tv.tv_usec = tv_usec%1000000;

	if ( select(fd+1, &fs, NULL, NULL, &tv) == -1 ) return -1;
	if ( FD_ISSET(fd, &fs) ) return 0;
	return 1;
}

struct packed_ether_header {
  u_int8_t  ether_dhost[ETH_ALEN];      /* destination eth addr */
  u_int8_t  ether_shost[ETH_ALEN];      /* source ether addr    */
  u_int16_t ether_type;                 /* packet type ID field */
} __attribute__((packed));

typedef struct arpMessage {
	struct packed_ether_header    ethhdr;
	u_short htype;	  /* hardware type (must be ARPHRD_ETHER) */
	u_short ptype;	  /* protocol type (must be ETHERTYPE_IP) */
	u_char  hlen;	  /* hardware address length (must be 6) */
	u_char  plen;	  /* protocol address length (must be 4) */
	u_short operation;	  /* ARP opcode */
	u_char  sHaddr[ETH_ALEN]; /* sender's hardware address */
	u_char  sInaddr[4];	  /* sender's IP address */
	u_char  tHaddr[ETH_ALEN]; /* target's hardware address */
	u_char  tInaddr[4];	  /* target's IP address */
	u_char  pad[18];  /* pad for min. Ethernet payload (60 bytes) */
} __attribute__((packed)) arpMessage;

#define MAC_BCAST_ADDR		"\xff\xff\xff\xff\xff\xff"

/*
 * Copy from dhcpcd arp.c and remove global variables.
 *
 * This function will check whether ip TargetAddr is in used.
 * It use MAC address to broadcasts to ehternet from 
 * interface szInterface.
 * 
 * Return Value:
 * -1: Error
 *	0: If ip target addr is NOT used.
 *  1: Ip TargetAddr is in use.
 *
 */
int SYNOARPCheck(const char *szInterface, const in_addr_t TargetAddr)
{
	arpMessage ArpMsgSend, ArpMsgRecv;
	struct sockaddr addr;
	socklen_t j = 0;
	int i = 0, OptVal = 1, err = -1;
	int ctlSocket = -1;
	unsigned char crgHWAddr[ETH_ALEN];

	if (NULL == szInterface) {
		syslog(LOG_ERR, "%s (%d) Parameter error.", __FILE__, __LINE__);
		return -1;
	}

	// Get the MAC address of szInterface.
	if (GetHWAddr(szInterface, crgHWAddr, ETH_ALEN) < 0) {
		syslog(LOG_ERR, "%s (%d) Failed to get hardware address.", __FILE__, __LINE__);
		return -1;
	}

	ctlSocket = socket(AF_PACKET, SOCK_PACKET, htons(ETH_P_ALL));
	if (ctlSocket == -1) {
		syslog(LOG_ERR, "%s (%d) Failed to open socket", __FILE__, __LINE__);
		return -1;
	}
	if ( setsockopt(ctlSocket, SOL_SOCKET, SO_BROADCAST, &OptVal, sizeof(OptVal)) == -1 ) {
		syslog(LOG_ERR, "%s (%d) Failed to set socket option (%s)", 
			   __FILE__, __LINE__, strerror(errno));
		goto Err;
	}

	memset(&ArpMsgSend, 0, sizeof(arpMessage));
	memcpy(ArpMsgSend.ethhdr.ether_dhost, MAC_BCAST_ADDR, ETH_ALEN);
	memcpy(ArpMsgSend.ethhdr.ether_shost, crgHWAddr, ETH_ALEN);
	ArpMsgSend.ethhdr.ether_type = htons(ETHERTYPE_ARP);

	ArpMsgSend.htype  = htons(ARPHRD_ETHER);
	ArpMsgSend.ptype  = htons(ETHERTYPE_IP);
	ArpMsgSend.hlen   = ETH_ALEN;
	ArpMsgSend.plen   = 4;
	ArpMsgSend.operation  = htons(ARPOP_REQUEST);
	memcpy(ArpMsgSend.sHaddr, crgHWAddr, ETH_ALEN);
	memcpy(ArpMsgSend.tInaddr, &TargetAddr, 4);

	DEBUGMSG("%s (%d) Broadcasting ARPOP_REQUEST for %u.%u.%u.%u\n",
			 __FILE__, __LINE__, 
			 ArpMsgSend.tInaddr[0],ArpMsgSend.tInaddr[1],
			 ArpMsgSend.tInaddr[2],ArpMsgSend.tInaddr[3]);

	do {
		do {
			if ( i++ > 4 ) { /*  5 probes  */
				err = 0;
				goto Err; 
			}
			memset(&addr, 0, sizeof(struct sockaddr));
			memcpy(addr.sa_data, szInterface, strlen(szInterface));
			if ( sendto(ctlSocket, &ArpMsgSend, sizeof(arpMessage), 0,
						&addr, sizeof(struct sockaddr)) == -1 ) {
				syslog(LOG_ERR, "%s (%d) Failed to sendto()", __FILE__, __LINE__);
				goto Err;
			}
		} while ( PeekFd(ctlSocket,50000) );	/* 50 msec timeout */

		do {
			memset(&ArpMsgRecv, 0, sizeof(arpMessage));
			j = sizeof(struct sockaddr);
			if ( recvfrom(ctlSocket, &ArpMsgRecv, sizeof(arpMessage), 0,
						  (struct sockaddr *)&addr, &j) == -1 ) {
				syslog(LOG_ERR, "%s (%d) Failed to recvfrom()", __FILE__, __LINE__);
				goto Err;
			}

			if ( ArpMsgRecv.ethhdr.ether_type != htons(ETHERTYPE_ARP) )
				continue;

			if ( ArpMsgRecv.operation == htons(ARPOP_REPLY) ) {
				DEBUGMSG("%s (%d) ARPOP_REPLY received from %u.%u.%u.%u for %u.%u.%u.%u\n",
						 __FILE__, __LINE__, 
						 ArpMsgRecv.sInaddr[0], ArpMsgRecv.sInaddr[1],
						 ArpMsgRecv.sInaddr[2], ArpMsgRecv.sInaddr[3],
						 ArpMsgRecv.tInaddr[0], ArpMsgRecv.tInaddr[1],
						 ArpMsgRecv.tInaddr[2], ArpMsgRecv.tInaddr[3]);
			} else
				continue;

			if ( (memcmp(ArpMsgRecv.sInaddr, ArpMsgRecv.tInaddr, 4) == 0 ) && 
				 !((ArpMsgRecv.sInaddr[0] == 0 ) && ( ArpMsgRecv.sInaddr[1] == 0 ) &&
				  (ArpMsgRecv.sInaddr[2] == 0 ) && ( ArpMsgRecv.sInaddr[3] == 0 )) ){
				/* Special hack for Linux: It will return this own IP address in
				 * both ArpMsgRecv.sInaddr and ArpMsgRecv.tInaddr.  However, 
				 * it should not be 0.0.0.0 */
				err = 1;
				goto Err;
			}

			if ( memcmp(ArpMsgRecv.tHaddr, crgHWAddr, ETH_ALEN) ) {
				DEBUGMSG("%s (%d) Target hardware address mismatch: %02X.%02X.%02X.%02X.%02X.%02X received, %02X.%02X.%02X.%02X.%02X.%02X expected\n",
						 __FILE__, __LINE__, 
						 ArpMsgRecv.tHaddr[0], ArpMsgRecv.tHaddr[1], ArpMsgRecv.tHaddr[2],
						 ArpMsgRecv.tHaddr[3], ArpMsgRecv.tHaddr[4], ArpMsgRecv.tHaddr[5],
						 crgHWAddr[0], crgHWAddr[1],
						 crgHWAddr[2], crgHWAddr[3],
						 crgHWAddr[4], crgHWAddr[5]);
				continue;
			}
			if ( memcmp(&ArpMsgRecv.sInaddr, &TargetAddr, 4) ) {
				syslog(LOG_ERR, "%s (%d) sender IP address mismatch: %u.%u.%u.%u received, %u.%u.%u.%u expected",
					   __FILE__, __LINE__,
					   ArpMsgRecv.sInaddr[0], ArpMsgRecv.sInaddr[1], 
					   ArpMsgRecv.sInaddr[2], ArpMsgRecv.sInaddr[3],
					   ((unsigned char *)&TargetAddr)[0],
					   ((unsigned char *)&TargetAddr)[1],
					   ((unsigned char *)&TargetAddr)[2],	
					   ((unsigned char *)&TargetAddr)[3]);
				continue;
			}
			err = 1;
			goto Err;
		} while ( PeekFd(ctlSocket,50000) == 0 );
	} while ( 1 );

	/* Never reached, don't live here */

Err:
	if (ctlSocket != -1) {
		close(ctlSocket);
	}
	return err;
}

/**
 * Detect link status on interface <devname>.
 *
 * @param devname name string of interface
 * @return
 * -  1: Link detected
 * -  0: No link
 * - <0: Error
 */
int SYNOLinkDetect(const char *szDevname)
{
	int Ret = -1;
	int fd;
	int err;
	struct ifreq ifr;

	if (NULL == szDevname) {
		goto Return;
	}

	/* Setup our control structures. */
	memset(&ifr, 0, sizeof(ifr));
	strcpy(ifr.ifr_name, szDevname);

	/* Open control socket. */
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		// syslog(LOG_ERR, "Cannot open control socket");
		goto Return;
	}

	err = ioctl(fd, SIOCGIFFLAGS, &ifr);
	if (err != 0) {
		// syslog(LOG_ERR, "Cannot get link status: %m");
		Ret = -1;
		goto CloseFd;
	}
	// syslog(LOG_ERR, "   Link detected: %s\n",
	//		(ifr.ifr_flags & IFF_RUNNING) ? "yes":"no");
	if (!(ifr.ifr_flags & IFF_RUNNING)) {
	//	syslog(LOG_ERR, "%s: No link", szDevname);
		Ret = 0;
		goto CloseFd;
	}

	Ret = 1; /* Link detected */

CloseFd:
	if (fd < 0) {
		close(fd);
	}
Return:
	return Ret;
}

int SYNOSetTmpIP(const char *szInterface)
{
	in_addr_t	ipAddr, Mask;
	unsigned int BaseIP;
	int err;
	unsigned char crgHWAddr[ETH_ALEN];
	unsigned int Seed;
	int cTimeout;

	Mask = inet_addr(IPMASK);	// network byte order
	BaseIP = inet_network(IPRANGE_MIN);	// host byte order

	// Get the MAC address of szInterface.
	if (GetHWAddr(szInterface, crgHWAddr, ETH_ALEN) < 0) {
		syslog(LOG_ERR, "%s (%d) Failed to get hardware address.", __FILE__, __LINE__);
		return -1;
	}
	Seed = 
		((crgHWAddr[5] << 24) | (crgHWAddr[4] << 16) | (crgHWAddr[3] << 8) | (crgHWAddr[2])) ^
		(crgHWAddr[1] << 24) ^
		(crgHWAddr[0] << 8);
	srand(Seed);

	/* 
	 * Wait for link to be ready before we detect IP conflicts.
	 * Time out after 5 seconds
	 */	
	cTimeout = 5;
	do {
		if (0 != SYNOLinkDetect(szInterface)) {
			/* detected link or error occured */
			break;
		}
		sleep(1);
	} while (--cTimeout > 0);

	while (1) {
		ipAddr = rand() & 0x0000FFFF; // Host Endian

		if ((ipAddr == 0x0000FFFF) || // skip x.x.255.255
			(ipAddr == 0x0)){  // skip x.x.0.0
			continue;
		} 

		ipAddr = htonl(BaseIP+ipAddr); // Network Endian

		err = SYNOARPCheck(szInterface, ipAddr);
		if (err == 0) {
			SYNOSetIP(szInterface, ipAddr, Mask);
			return 0;
		} else if (err < 0) {
			return -1;
		}
	}
	return -1;
}

#ifdef	__MAIN__
int main(int argc, char **argv)
{
	int err;
	char *szIp, *szInterface;

	if (argc == 2) {
		SYNOSetTmpIP(argv[1]);
		exit(0);
	}
	else if (argc != 3) {
		printf("Usage: %s interface ip \n", argv[0]);
		exit(0);
	}
	szInterface = argv[1];
	szIp = argv[2];
	err = SYNOARPCheck(szInterface, inet_addr(szIp));
	printf("%s\n", err?"Found!!":"Not found");
	exit(0);
}
#endif /* __MAIN__ */
