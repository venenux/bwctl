/*
**      $Id$
*/
/************************************************************************
*									*
*			     Copyright (C)  2003			*
*				Internet2				*
*			     All Rights Reserved			*
*									*
************************************************************************/
/*
**	File:		api.c
**
**	Author:		Jeff W. Boote
**
**	Date:		Tue Sep 16 14:24:49 MDT 2003
**
**	Description:	
*/
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <netinet/in.h>
#include <string.h>
#include <assert.h>

#include "./ipcntrlP.h"

#ifndef EFTYPE
#define	EFTYPE	ENOSYS
#endif

IPFAddr
_IPFAddrAlloc(
	IPFContext	ctx
)
{
	IPFAddr	addr = calloc(1,sizeof(struct IPFAddrRec));

	if(!addr){
		IPFError(ctx,IPFErrFATAL,IPFErrUNKNOWN,
			":calloc(1,%d):%M",sizeof(struct IPFAddrRec));
		return NULL;
	}

	addr->ctx = ctx;

	addr->node_set = 0;
	strncpy(addr->node,"unknown",sizeof(addr->node));
	addr->port_set = 0;
	strncpy(addr->port,"unknown",sizeof(addr->port));
	addr->ai_free = 0;
	addr->ai = NULL;

	addr->saddr = NULL;
	addr->saddrlen = 0;

	addr->fd_user = 0;
	addr->fd= -1;

	return addr;
}

IPFErrSeverity
IPFAddrFree(
	IPFAddr	addr
)
{
	IPFErrSeverity	err = IPFErrOK;

	if(!addr)
		return err;

	if(addr->ai){
		if(!addr->ai_free){
			freeaddrinfo(addr->ai);
		}else{
			struct addrinfo	*ai, *next;

			ai = addr->ai;
			while(ai){
				next = ai->ai_next;

				if(ai->ai_addr) free(ai->ai_addr);
				if(ai->ai_canonname) free(ai->ai_canonname);
				free(ai);

				ai = next;
			}
		}
		addr->ai = NULL;
		addr->saddr = NULL;
	}

	if((addr->fd >= 0) && !addr->fd_user){
		if(close(addr->fd) < 0){
			IPFError(addr->ctx,IPFErrWARNING,
					errno,":close(%d)",addr->fd);
			err = IPFErrWARNING;
		}
	}

	free(addr);

	return err;
}

IPFAddr
IPFAddrByNode(
	IPFContext	ctx,
	const char	*node
)
{
	IPFAddr		addr;
	char		buff[MAXHOSTNAMELEN+1];
	const char	*nptr=node;
	char		*pptr=NULL;
	char		*s1,*s2;

	if(!node)
		return NULL;

	if(!(addr=_IPFAddrAlloc(ctx)))
		return NULL;

	strncpy(buff,node,MAXHOSTNAMELEN);

	/*
	 * Pull off port if specified. If syntax doesn't match URL like
	 * node:port - ipv6( [node]:port) - then just assume whole string
	 * is nodename and let getaddrinfo report problems later.
	 * (This service syntax is specified by rfc2396 and rfc2732.)
	 */

	/*
	 * First try ipv6 syntax since it is more restrictive.
	 */
	if( (s1 = strchr(buff,'['))){
		s1++;
		if(strchr(s1,'[')) goto NOPORT;
		if(!(s2 = strchr(s1,']'))) goto NOPORT;
		*s2++='\0';
		if(strchr(s2,']')) goto NOPORT;
		if(*s2++ != ':') goto NOPORT;
		nptr = s1;
		pptr = s2;
	}
	/*
	 * Now try ipv4 style.
	 */
	else if( (s1 = strchr(buff,':'))){
		*s1++='\0';
		if(strchr(s1,':')) goto NOPORT;
		nptr = buff;
		pptr = s1;
	}


NOPORT:
	strncpy(addr->node,nptr,MAXHOSTNAMELEN);
	addr->node_set = 1;

	if(pptr){
		strncpy(addr->port,pptr,MAXHOSTNAMELEN);
		addr->port_set = 1;
	}

	return addr;
}

static struct addrinfo*
_IPFCopyAddrRec(
	IPFContext		ctx,
	const struct addrinfo	*src
)
{
	struct addrinfo	*dst = calloc(1,sizeof(struct addrinfo));

	if(!dst){
		IPFError(ctx,IPFErrFATAL,errno,
				":calloc(1,sizeof(struct addrinfo))");
		return NULL;
	}

	*dst = *src;

	if(src->ai_addr){
		dst->ai_addr = malloc(src->ai_addrlen);
		if(!dst->ai_addr){
			IPFError(ctx,IPFErrFATAL,errno,
				"malloc(%u):%s",src->ai_addrlen,
				strerror(errno));
			free(dst);
			return NULL;
		}
		memcpy(dst->ai_addr,src->ai_addr,src->ai_addrlen);
		dst->ai_addrlen = src->ai_addrlen;
	}
	else
		dst->ai_addrlen = 0;

	if(src->ai_canonname){
		int	len = strlen(src->ai_canonname);

		if(len > MAXHOSTNAMELEN){
			IPFError(ctx,IPFErrWARNING,
					IPFErrUNKNOWN,
					":Invalid canonname!");
			dst->ai_canonname = NULL;
		}else{
			dst->ai_canonname = malloc(sizeof(char)*(len+1));
			if(!dst->ai_canonname){
				IPFError(ctx,IPFErrWARNING,
					errno,":malloc(sizeof(%d)",len+1);
				dst->ai_canonname = NULL;
			}else
				strcpy(dst->ai_canonname,src->ai_canonname);
		}
	}

	dst->ai_next = NULL;

	return dst;
}

IPFAddr
IPFAddrByAddrInfo(
	IPFContext		ctx,
	const struct addrinfo	*ai
)
{
	IPFAddr	addr = _IPFAddrAlloc(ctx);
	struct addrinfo	**aip;

	if(!addr)
		return NULL;

	addr->ai_free = 1;
	aip = &addr->ai;

	while(ai){
		*aip = _IPFCopyAddrRec(ctx,ai);
		if(!*aip){
			IPFAddrFree(addr);
			return NULL;
		}
		aip = &(*aip)->ai_next;
		ai = ai->ai_next;
	}

	return addr;
}

IPFAddr
IPFAddrBySockFD(
	IPFContext	ctx,
	int		fd
)
{
	IPFAddr	addr = _IPFAddrAlloc(ctx);

	if(!addr)
		return NULL;

	addr->fd_user = 1;
	addr->fd = fd;

	return addr;
}

IPFAddr
_IPFAddrCopy(
	IPFAddr		from
	)
{
	IPFAddr		to;
	struct addrinfo	**aip;
	struct addrinfo	*ai;
	
	if(!from)
		return NULL;
	
	if( !(to = _IPFAddrAlloc(from->ctx)))
		return NULL;

	if(from->node_set){
		strncpy(to->node,from->node,sizeof(to->node));
		to->node_set = True;
	}

	if(from->port_set){
		strncpy(to->port,from->port,sizeof(to->port));
		to->port_set = True;
	}

	to->ai_free = 1;
	aip = &to->ai;
	ai = from->ai;

	while(ai){
		*aip = _IPFCopyAddrRec(from->ctx,ai);
		if(!*aip){
			IPFAddrFree(to);
			return NULL;
		}
		if(ai->ai_addr == from->saddr){
			to->saddr = (*aip)->ai_addr;
			to->saddrlen = (*aip)->ai_addrlen;
		}

		aip = &(*aip)->ai_next;
		ai = ai->ai_next;
	}

	to->fd = from->fd;

	if(to->fd > -1)
		to->fd_user = True;

	return to;
}

int
IPFAddrFD(
	IPFAddr	addr
	)
{
	if(!addr || (addr->fd < 0))
		return -1;

	return addr->fd;
}

socklen_t
IPFAddrSockLen(
	IPFAddr	addr
	)
{
	if(!addr || !addr->saddr)
		return 0;

	return addr->saddrlen;
}

/*
 * Function:	IPFGetContext
 *
 * Description:	
 * 	Returns the context pointer that was referenced when the
 * 	given control connection was created.
 *
 * In Args:	
 *
 * Out Args:	
 *
 * Scope:	
 * Returns:	
 * Side Effect:	
 */
IPFContext
IPFGetContext(
	IPFControl	cntrl
	)
{
	return cntrl->ctx;
}

/*
 * Function:	IPFGetMode
 *
 * Description:	
 * 	Returns the "mode" of the control connection.
 *
 * In Args:	
 *
 * Out Args:	
 *
 * Scope:	
 * Returns:	
 * Side Effect:	
 */
IPFSessionMode
IPFGetMode(
	IPFControl	cntrl
	)
{
	return cntrl->mode;
}

/*
 * Function:	IPFControlFD
 *
 * Description:	
 *
 * In Args:	
 *
 * Out Args:	
 *
 * Scope:	
 * Returns:	
 * Side Effect:	
 */
int
IPFControlFD(
	IPFControl	cntrl
	)
{
	return cntrl->sockfd;
}

/*
 * Function:	IPFGetRTTBound
 *
 * Description:	Returns a very rough estimate of the upper-bound rtt to
 * 		the server.
 *
 * In Args:	
 *
 * Out Args:	
 *
 * Scope:	
 * Returns:	
 * 		bound or 0 if unavailable
 * Side Effect:	
 */
IPFNum64
IPFGetRTTBound(
	IPFControl	cntrl
	)
{
	return cntrl->rtt_bound;
}

/*
 * Function:	_IPFFailControlSession
 *
 * Description:	
 * 	Simple convienience to set the state and return the failure at
 * 	the same time.
 *
 * In Args:	
 *
 * Out Args:	
 *
 * Scope:	
 * Returns:	
 * Side Effect:	
 */
IPFErrSeverity
_IPFFailControlSession(
	IPFControl	cntrl,
	int		level
	)
{
	cntrl->state = _IPFStateInvalid;
	return (IPFErrSeverity)level;
}

/*
 * Function:	_IPFTestSessionAlloc
 *
 * Description:	
 * 	This function is used to allocate/initialize the memory record used
 * 	to maintain state information about a "configured" test.
 *
 * In Args:	
 *
 * Out Args:	
 *
 * Scope:	
 * Returns:	
 * Side Effect:	
 */
IPFTestSession
_IPFTestSessionAlloc(
	IPFControl	cntrl,
	IPFAddr		sender,
	IPFBoolean	conf_sender,
	IPFAddr		receiver,
	IPFBoolean	conf_receiver,
	IPFTestSpec	*test_spec
)
{
	IPFTestSession	test;

	/*
	 * Address records must exist.
	 */
	if(!sender || ! receiver){
		IPFError(cntrl->ctx,IPFErrFATAL,IPFErrINVALID,
				"_IPFTestSessionAlloc:Invalid Addr arg");
		return NULL;
	}

	if(!(test = calloc(1,sizeof(IPFTestSessionRec)))){
		IPFError(cntrl->ctx,IPFErrFATAL,IPFErrUNKNOWN,
				"calloc(1,IPFTestSessionRec): %M");
		return NULL;
	}

	/*
	 * Initialize address records and test description record fields.
	 */
	test->cntrl = cntrl;
	test->sender = sender;
	test->conf_sender = conf_sender;
	test->receiver = receiver;
	test->conf_receiver = conf_receiver;
	memcpy(&test->test_spec,test_spec,sizeof(IPFTestSpec));

	return test;
}

/*
 * Function:	_IPFTestSessionFree
 *
 * Description:	
 * 	This function is used to free the memory associated with a "configured"
 * 	test session.
 *
 * In Args:	
 *
 * Out Args:	
 *
 * Scope:	
 * Returns:	
 * Side Effect:	
 */
IPFErrSeverity
_IPFTestSessionFree(
	IPFTestSession	tsession,
	IPFAcceptType	aval
)
{
	IPFTestSession	*sptr;
	IPFErrSeverity	err=IPFErrOK;

	if(!tsession){
		return IPFErrOK;
	}

	/*
	 * remove this tsession from the cntrl->tests lists.
	 */
	for(sptr = &tsession->cntrl->tests;*sptr;sptr = &(*sptr)->next){
		if(*sptr == tsession){
			*sptr = tsession->next;
			break;
		}
	}

	if(tsession->endpoint){
		(void)_IPFEndpointStop(tsession->endpoint,aval,&err);
	}

	if(tsession->closure){
		_IPFCallTestComplete(tsession,aval);
	}

	IPFAddrFree(tsession->sender);
	IPFAddrFree(tsession->receiver);

	if(tsession->sctx){
		IPFScheduleContextFree(tsession->sctx);
	}

	free(tsession);

	return err;
}


/*
 * Function:	_IPFCreateSID
 *
 * Description:	
 * 	Generate a "unique" SID from addr(4)/time(8)/random(4) values.
 *
 * In Args:	
 *
 * Out Args:	
 *
 * Scope:	
 * Returns:	
 * 	0 on success
 * Side Effect:	
 */
int
_IPFCreateSID(
	IPFTestSession	tsession
	)
{
	IPFTimeStamp	tstamp;
	u_int8_t	*aptr;

#ifdef	AF_INET6
	if(tsession->receiver->saddr->sa_family == AF_INET6){
		struct sockaddr_in6	*s6;

		s6 = (struct sockaddr_in6*)tsession->receiver->saddr;
		/* point at last 4 bytes of addr */
		aptr = &s6->sin6_addr.s6_addr[12];
	}else
#endif
	if(tsession->receiver->saddr->sa_family == AF_INET){
		struct sockaddr_in	*s4;

		s4 = (struct sockaddr_in*)tsession->receiver->saddr;
		aptr = (u_int8_t*)&s4->sin_addr;
	}
	else{
		IPFError(tsession->cntrl->ctx,IPFErrFATAL,IPFErrUNSUPPORTED,
				"_IPFCreateSID:Unknown address family");
		return 1;
	}

	memcpy(&tsession->sid[0],aptr,4);

	(void)IPFGetTimeOfDay(&tstamp);
	_IPFEncodeTimeStamp(&tsession->sid[4],&tstamp);

	if(I2RandomBytes(tsession->cntrl->ctx->rand_src,&tsession->sid[12],4)
									!= 0){
		return 1;
	}

	return 0;
}

IPFErrSeverity
IPFStopSession(
	IPFControl	cntrl,
	int		*retn_on_intr,
	IPFAcceptType	*acceptval_ret	/* in/out	*/
		)
{
	IPFErrSeverity	err,err2=IPFErrOK;
	IPFRequestType	msgtype;
	IPFAcceptType	aval=IPF_CNTRL_ACCEPT;
	IPFAcceptType	*acceptval=&aval;
	int		ival=0;
	int		*intr=&ival;

	if(acceptval_ret){
		acceptval = acceptval_ret;
	}

	if(retn_on_intr){
		intr = retn_on_intr;
	}

	/*
	 * TODO: v6 - fetch "last" sequence sent/received for encoding
	 * in StopSession message.
	 * (To do this - this loop needs to call "stop" on each endpoint,
	 * but not free the structures. Somehow "stop" needs to fetch the
	 * last sequence number from the endpoint when it exits. Receive
	 * is easy... Send it not as simple. Should I create a socketpair
	 * before forking off sender endpoints so the last seq number
	 * can be sent up the pipe?)
	 */

	while(cntrl->tests){
		err = _IPFTestSessionFree(cntrl->tests,*acceptval);
		err2 = MIN(err,err2);
	}

	/*
	 * If acceptval would have been "success", but stopping of local
	 * endpoints failed, send failure acceptval instead and return error.
	 * (The endpoint_stop_func should have reported the error.)
	 */
	if(!*acceptval && (err2 < IPFErrWARNING)){
		*acceptval = IPF_CNTRL_FAILURE;
	}

	err = (IPFErrSeverity)_IPFWriteStopSession(cntrl,intr,*acceptval);
	if(err < IPFErrWARNING)
		return _IPFFailControlSession(cntrl,IPFErrFATAL);
	err2 = MIN(err,err2);

	msgtype = IPFReadRequestType(cntrl,intr);
	if(msgtype == IPFReqSockClose){
		IPFError(cntrl->ctx,IPFErrFATAL,errno,
				"IPFStopSession:Control socket closed: %M");
		return _IPFFailControlSession(cntrl,IPFErrFATAL);
	}
	if(msgtype != IPFReqStopSession){
		IPFError(cntrl->ctx,IPFErrFATAL,IPFErrINVALID,
				"Invalid protocol message received...");
		return _IPFFailControlSession(cntrl,IPFErrFATAL);
	}

	err = _IPFReadStopSession(cntrl,acceptval,intr);

	/*
	 * TODO: v6 - use "last seq number" messages from
	 * in StopSession message to remove "missing" packets from the
	 * end of session files. The "last seq number" in the file should
	 * be MIN(last seq number sent,last seq number in file{missing or not}).
	 */

	cntrl->state &= ~_IPFStateTest;

	return MIN(err,err2);
}

IPFPacketSizeT
IPFTestPayloadSize(
		IPFSessionMode	mode, 
		u_int32_t	padding
		)
{
	IPFPacketSizeT msg_size;

	switch (mode) {
	case IPF_MODE_OPEN:
		msg_size = 14;
		break;
	case IPF_MODE_AUTHENTICATED:
	case IPF_MODE_ENCRYPTED:
		msg_size = 32;
		break;
	default:
		return 0;
		/* UNREACHED */
	}

	return msg_size + padding;
}

/* These lengths assume no IP options. */
#define IPF_IP4_HDR_SIZE	20	/* rfc 791 */
#define IPF_IP6_HDR_SIZE	40	/* rfc 2460 */
#define IPF_UDP_HDR_SIZE	8	/* rfc 768 */

/*
** Given the protocol family, OWAMP mode and packet padding,
** compute the size of resulting full IP packet.
*/
IPFPacketSizeT
IPFTestPacketSize(
		int		af,    /* AF_INET, AF_INET6 */
		IPFSessionMode	mode, 
		u_int32_t	padding
		)
{
	IPFPacketSizeT payload_size, header_size;

	switch (af) {
	case AF_INET:
		header_size = IPF_IP4_HDR_SIZE + IPF_UDP_HDR_SIZE;
		break;
	case AF_INET6:
		header_size = IPF_IP6_HDR_SIZE + IPF_UDP_HDR_SIZE;
		break;
	default:
		return 0;
		/* UNREACHED */
	}

	if(!(payload_size = IPFTestPayloadSize(mode,padding)))
			return 0;

	return payload_size + header_size;
}

/*
 * Function:	IPFSessionStatus
 *
 * Description:	
 * 	This function returns the "status" of the test session identified
 * 	by the sid. "send" indicates which "side" of the test to retrieve
 * 	information about.
 *
 * In Args:	
 *
 * Out Args:	
 *
 * Scope:	
 * Returns:	True if status was available, False otherwise.
 * 		aval contains the actual "status":
 * 			<0	Test is not yet complete
 * 			>=0	Valid IPFAcceptType - see enum for meaning.
 * Side Effect:	
 */
IPFBoolean
IPFSessionStatus(
		IPFControl	cntrl,
		IPFSID		sid,
		IPFAcceptType	*aval
		)
{
	IPFTestSession	tsession;
	IPFErrSeverity	err;

	/*
	 * First find the tsession record for this test.
	 */
	for(tsession=cntrl->tests;tsession;tsession=tsession->next)
		if(memcmp(sid,tsession->sid,sizeof(IPFSID)) == 0)
			goto found;

	return False;

found:
	if(tsession->endpoint){
		return _IPFEndpointStatus(tsession->endpoint,aval,&err);
	}

	return False;
}

int
IPFSessionsActive(
		IPFControl	cntrl,
		IPFAcceptType	*aval
		)
{
	IPFTestSession	tsession;
	IPFAcceptType	laval;
	IPFAcceptType	raval = 0;
	int		n=0;
	IPFErrSeverity	err;

	for(tsession = cntrl->tests;tsession;tsession = tsession->next){
		if((tsession->endpoint) &&
				_IPFEndpointStatus(tsession->endpoint,
								&laval,&err)){
			if(laval < 0)
				n++;
			else
				raval = MAX(laval,raval);
		}
	}

	if(aval)
		*aval = raval;

	return n;
}

int
IPFStopSessionWait(
	IPFControl	cntrl,
	IPFNum64	*wake,
	int		*retn_on_intr,
	IPFAcceptType	*acceptval_ret,
	IPFErrSeverity	*err_ret
	)
{
	struct timeval	currtime;
	struct timeval	reltime;
	struct timeval	*waittime = NULL;
	fd_set		readfds;
	fd_set		exceptfds;
	int		rc;
	int		msgtype;
	IPFErrSeverity	err2=IPFErrOK;
	IPFAcceptType	aval;
	IPFAcceptType	*acceptval=&aval;
	int		ival=0;
	int		*intr=&ival;

	*err_ret = IPFErrOK;
	if(acceptval_ret){
		acceptval = acceptval_ret;
	}
	*acceptval = IPF_CNTRL_ACCEPT;

	if(retn_on_intr){
		intr = retn_on_intr;
	}

	if(!cntrl || cntrl->sockfd < 0){
		*err_ret = IPFErrFATAL;
		return -1;
	}

	/*
	 * If there are no active sessions, get the status and return.
	 */
	if(!IPFSessionsActive(cntrl,acceptval) || (*acceptval)){
		/*
		 * Sessions are complete - send StopSession message.
		 */
		*err_ret = IPFStopSession(cntrl,intr,acceptval);
		return 0;
	}

	if(wake){
		IPFTimeStamp	wakestamp;

		/*
		 * convert abs wake time to timeval
		 */
		wakestamp.ipftime = *wake;
		IPFTimestampToTimeval(&reltime,&wakestamp);

		/*
		 * get current time.
		 */
		if(gettimeofday(&currtime,NULL) != 0){
			IPFError(cntrl->ctx,IPFErrFATAL,IPFErrUNKNOWN,
					"gettimeofday():%M");
			return -1;
		}

		/*
		 * compute relative wake time from current time and abs wake.
		 */
		if(tvalcmp(&currtime,&reltime,<)){
			tvalsub(&reltime,&currtime);
		}
		else{
			tvalclear(&reltime);
		}

		waittime = &reltime;
	}


	FD_ZERO(&readfds);
	FD_SET(cntrl->sockfd,&readfds);
	FD_ZERO(&exceptfds);
	FD_SET(cntrl->sockfd,&exceptfds);
AGAIN:
	rc = select(cntrl->sockfd+1,&readfds,NULL,&exceptfds,waittime);

	if(rc < 0){
		if(errno != EINTR){
			IPFError(cntrl->ctx,IPFErrFATAL,IPFErrUNKNOWN,
					"select():%M");
			*err_ret = IPFErrFATAL;
			return -1;
		}
		if(waittime || *intr){
			return 2;
		}

		/*
		 * If there are tests still happening, and no tests have
		 * ended in error - go back to select and wait for the
		 * rest of the tests to complete.
		 */
		if(IPFSessionsActive(cntrl,acceptval) && !*acceptval){
			goto AGAIN;
		}

		/*
		 * Sessions are complete - send StopSession message.
		 */
		*err_ret = IPFStopSession(cntrl,intr,acceptval);

		return 0;
	}
	if(rc == 0)
		return 1;

	if(!FD_ISSET(cntrl->sockfd,&readfds) &&
					!FD_ISSET(cntrl->sockfd,&exceptfds)){
		IPFError(cntrl->ctx,IPFErrFATAL,IPFErrUNKNOWN,
					"select():cntrl fd not ready?:%M");
		*err_ret = _IPFFailControlSession(cntrl,IPFErrFATAL);
		return -1;
	}

	msgtype = IPFReadRequestType(cntrl,intr);
	if(msgtype == 0){
		IPFError(cntrl->ctx,IPFErrFATAL,errno,
			"IPFStopSessionWait: Control socket closed: %M");
		*err_ret = _IPFFailControlSession(cntrl,IPFErrFATAL);
		return -1;
	}
	if(msgtype != 3){
		IPFError(cntrl->ctx,IPFErrFATAL,IPFErrINVALID,
				"Invalid protocol message received...");
		*err_ret = _IPFFailControlSession(cntrl,IPFErrFATAL);
		return -1;
	}

	*err_ret = _IPFReadStopSession(cntrl,intr,acceptval);
	if(*err_ret != IPFErrOK){
		cntrl->state = _IPFStateInvalid;
		return -1;
	}

	while(cntrl->tests){
		err2 = _IPFTestSessionFree(cntrl->tests,*acceptval);
		*err_ret = MIN(*err_ret,err2);
	}

	if(*err_ret < IPFErrWARNING){
		*acceptval = IPF_CNTRL_FAILURE;
	}

	err2 = _IPFWriteStopSession(cntrl,intr,*acceptval);
	cntrl->state &= ~_IPFStateTest;

	*err_ret = MIN(*err_ret, err2);
	return 0;
}

/*
 * Function:	IPFAddrNodeName
 *
 * Description:	
 * 	This function gets a char* node name for a given IPFAddr.
 * 	The len parameter is an in/out parameter.
 *
 * In Args:	
 *
 * Out Args:	
 *
 * Scope:	
 * Returns:	
 * Side Effect:	
 */
void
IPFAddrNodeName(
	IPFAddr	addr,
	char	*buf,
	size_t	*len
	)
{
	assert(buf);
	assert(len);
	assert(*len > 0);

	if(!addr){
		goto bail;
	}

	if(!addr->node_set && addr->saddr &&
			getnameinfo(addr->saddr,addr->saddrlen,
				addr->node,sizeof(addr->node),
				addr->port,sizeof(addr->port),
				NI_NUMERICHOST|NI_NUMERICSERV) == 0){
		addr->node_set = 1;
		addr->port_set = 1;
	}

	if(addr->node_set){
		*len = MIN(*len,sizeof(addr->node));
		strncpy(buf,addr->node,*len);
		return;
	}

bail:
	*len = 0;
	buf[0] = '\0';
	return;
}

/*
 * Function:	IPFAddrNodeService
 *
 * Description:	
 * 	This function gets a char* service name for a given IPFAddr.
 * 	The len parameter is an in/out parameter.
 *
 * In Args:	
 *
 * Out Args:	
 *
 * Scope:	
 * Returns:	
 * Side Effect:	
 */
void
IPFAddrNodeService(
	IPFAddr	addr,
	char	*buf,
	size_t	*len
	)
{
	assert(buf);
	assert(len);
	assert(*len > 0);

	if(!addr){
		goto bail;
	}

	if(!addr->port_set && addr->saddr &&
			getnameinfo(addr->saddr,addr->saddrlen,
				addr->node,sizeof(addr->node),
				addr->port,sizeof(addr->port),
				NI_NUMERICHOST|NI_NUMERICSERV) == 0){
		addr->node_set = 1;
		addr->port_set = 1;
	}

	if(addr->port_set){
		*len = MIN(*len,sizeof(addr->port));
		strncpy(buf,addr->port,*len);
		return;
	}

bail:
	*len = 0;
	buf[0] = '\0';
	return;
}
