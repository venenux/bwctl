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
**	File:		context.c
**
**	Author:		Jeff W. Boote
**
**	Date:		Tue Sep 16 14:25:42 MDT 2003
**
**	Description:	
*/
#include <assert.h>
#include <signal.h>

#include "ipcntrlP.h"

/*
 * Function:	IPFContextCreate
 *
 * Description:	
 * 	This function is used to initialize a "context" for the ipcntrl
 * 	library. The context is used to define how error reporting
 * 	and other semi-global state should be defined.
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
IPFContextCreate(
	I2ErrHandle	eh
)
{
	struct sigaction	act;
	I2LogImmediateAttr	ia;
	IPFContext		ctx = calloc(1,sizeof(IPFContextRec));

	if(!ctx){
		IPFError(eh,
			IPFErrFATAL,ENOMEM,":calloc(1,%d):%M",
						sizeof(IPFContextRec));
		return NULL;
	}

	if(!eh){
		ctx->lib_eh = True;
		ia.line_info = (I2NAME|I2MSG);
		ia.fp = stderr;
		ctx->eh = I2ErrOpen("libipcntrl",I2ErrLogImmediate,&ia,
				NULL,NULL);
		if(!ctx->eh){
			IPFError(NULL,IPFErrFATAL,IPFErrUNKNOWN,
					"Cannot init error module");
			free(ctx);
			return NULL;
		}
	}
	else{
		ctx->lib_eh = False;
		ctx->eh = eh;
	}

	if( !(ctx->table = I2HashInit(ctx->eh,_IPF_CONTEXT_TABLE_SIZE,
								NULL,NULL))){
		IPFContextFree(ctx);
		return NULL;
	}

	if( !(ctx->rand_src = I2RandomSourceInit(ctx->eh,I2RAND_DEV,NULL))){
		IPFError(ctx,IPFErrFATAL,IPFErrUNKNOWN,
			     "Failed to initialize randomness sources");
		IPFContextFree(ctx);
		return NULL;
	}

	/*
	 * Do NOT exit on SIGPIPE. To defeat this in the least intrusive
	 * way only set SIG_IGN if SIGPIPE is currently set to SIG_DFL.
	 * Presumably if someone actually set a SIGPIPE handler, they
	 * knew what they were doing...
	 */
	sigemptyset(&act.sa_mask);
	act.sa_handler = SIG_DFL;
	act.sa_flags = 0;
	if(sigaction(SIGPIPE,NULL,&act) != 0){
		IPFError(ctx,IPFErrFATAL,IPFErrUNKNOWN,"sigaction(): %M");
		IPFContextFree(ctx);
		return NULL;
	}
	if(act.sa_handler == SIG_DFL){
		act.sa_handler = SIG_IGN;
		if(sigaction(SIGPIPE,&act,NULL) != 0){
			IPFError(ctx,IPFErrFATAL,IPFErrUNKNOWN,
					"sigaction(): %M");
			IPFContextFree(ctx);
			return NULL;
		}
	}

	return ctx;
}

/*
 * Function:	IPFContextGetErrHandle
 *
 * Description:	
 * 	Returns the ErrHandle that was set for this context upon creation.
 *
 * In Args:	
 *
 * Out Args:	
 *
 * Scope:	
 * Returns:	
 * Side Effect:	
 */
extern I2ErrHandle
IPFContextGetErrHandle(
	IPFContext	ctx
	)
{
	return ctx->eh;
}

struct _IPFContextHashRecord{
	char	key[_IPF_CONTEXT_MAX_KEYLEN+1];
	void	*value;
};

struct _IPFFreeHashRecord{
	IPFContext	ctx;
	I2Table		table;
};

static I2Boolean
free_hash_entries(
	I2Datum	key,
	I2Datum	value,
	void	*app_data
	)
{
	struct _IPFFreeHashRecord	*frec =
					(struct _IPFFreeHashRecord*)app_data;

	/*
	 * Delete hash so key.dptr will not be referenced again.
	 * (key.dptr is part of value.dptr alloc)
	 */
	if(I2HashDelete(frec->table,key) != 0){
		IPFError(frec->ctx,IPFErrFATAL,IPFErrUNKNOWN,
				"Unable to clean out Context hash?");
		return False;
	}

	free(value.dptr);

	return True;
}


void
IPFContextFree(
	IPFContext	ctx
)
{
	struct _IPFFreeHashRecord	frec; 

	while(ctx->cntrl_list){
		IPFControlClose(ctx->cntrl_list);
	}

	frec.ctx = ctx;
	frec.table = ctx->table;

	if(ctx->table){
		I2HashIterate(ctx->table,free_hash_entries,(void*)&frec);
		I2HashClose(ctx->table);
		ctx->table = NULL;
	}

	if(ctx->rand_src){
		I2RandomSourceClose(ctx->rand_src);
		ctx->rand_src = NULL;
	}

	if(ctx->lib_eh && ctx->eh){
		I2ErrClose(ctx->eh);
		ctx->eh = NULL;
	}

	free(ctx);

	return;
}

IPFErrSeverity
IPFControlClose(IPFControl cntrl)
{
	IPFErrSeverity			err = IPFErrOK;
	IPFErrSeverity			lerr = IPFErrOK;
	struct _IPFFreeHashRecord	frec; 
	IPFControl			*list = &cntrl->ctx->cntrl_list;

	/*
	 * remove all test sessions
	 */
	while(cntrl->tests){
		lerr = _IPFTestSessionFree(cntrl->tests,IPF_CNTRL_FAILURE);
		err = MIN(err,lerr);
	}

	frec.ctx = cntrl->ctx;
	frec.table = cntrl->table;

	if(cntrl->table){
		I2HashIterate(cntrl->table,free_hash_entries,(void*)&frec);
		I2HashClose(cntrl->table);
	}

	/*
	 * Remove cntrl from ctx list.
	 */
	while(*list && (*list != cntrl))
		list = &(*list)->next;
	if(*list == cntrl)
		*list = cntrl->next;

	/*
	 * these functions will close the control socket if it is open.
	 */
	lerr = IPFAddrFree(cntrl->remote_addr);
	err = MIN(err,lerr);
	lerr = IPFAddrFree(cntrl->local_addr);
	err = MIN(err,lerr);

	free(cntrl);

	return err;
}

IPFControl
_IPFControlAlloc(
	IPFContext		ctx,
	IPFErrSeverity		*err_ret
)
{
	IPFControl	cntrl;
	
	if( !(cntrl = calloc(1,sizeof(IPFControlRec)))){
		IPFError(ctx,IPFErrFATAL,errno,
				":calloc(1,%d)",sizeof(IPFControlRec));
		*err_ret = IPFErrFATAL;
		return NULL;
	}

	/*
	 * Init state fields
	 */
	cntrl->ctx = ctx;

	/*
	 * Initialize control policy state hash.
	 */
	if( !(cntrl->table = I2HashInit(ctx->eh,_IPF_CONTEXT_TABLE_SIZE,
								NULL,NULL))){
		*err_ret = IPFErrFATAL;
		free(cntrl);
		return NULL;
	}

	/*
	 * Init addr fields
	 */
	cntrl->sockfd = -1;

	/*
	 * Init encryption fields
	 */
	memset(cntrl->userid_buffer,'\0',sizeof(cntrl->userid_buffer));

	/*
	 * Put this control record on the ctx list.
	 */
	cntrl->next = ctx->cntrl_list;
	ctx->cntrl_list = cntrl;

	return cntrl;
}

static IPFBoolean
ConfigSet(
	I2Table		table,
	const char	*key,
	void		*value
	)
{
	struct _IPFContextHashRecord	*rec,*trec;
	I2Datum				k,v,t;

	assert(table);
	assert(key);

	if(!(rec = calloc(1,sizeof(struct _IPFContextHashRecord)))){
		return False;
	}
	/* ensure nul byte */
	rec->key[_IPF_CONTEXT_MAX_KEYLEN] = '\0';

	/* set key datum */
	strncpy(rec->key,key,_IPF_CONTEXT_MAX_KEYLEN);
	rec->value = value;

	k.dptr = rec->key;
	k.dsize = strlen(rec->key);

	/* set value datum */
	v.dptr = rec;
	v.dsize = sizeof(rec);

	/*
	 * If there is already a key by this entry - free that record.
	 */
	if(I2HashFetch(table,k,&t)){
		trec = (struct _IPFContextHashRecord*)t.dptr;
		I2HashDelete(table,k);
		free(trec);
	}

	if(I2HashStore(table,k,v) == 0){
		return True;
	}

	free(rec);
	return False;
}

static void *
ConfigGet(
	I2Table		table,
	const char	*key
	)
{
	struct _IPFContextHashRecord	*rec;
	I2Datum				k,v;
	char				kval[_IPF_CONTEXT_MAX_KEYLEN+1];

	assert(key);

	kval[_IPF_CONTEXT_MAX_KEYLEN] = '\0';
	strncpy(kval,key,_IPF_CONTEXT_MAX_KEYLEN);
	k.dptr = kval;
	k.dsize = strlen(kval);

	if(!I2HashFetch(table,k,&v)){
		return NULL;
	}

	rec = (struct _IPFContextHashRecord*)v.dptr;

	return rec->value;
}

static IPFBoolean
ConfigDelete(
	I2Table		table,
	const char	*key
	)
{
	I2Datum	k;
	char	kval[_IPF_CONTEXT_MAX_KEYLEN+1];

	assert(key);

	kval[_IPF_CONTEXT_MAX_KEYLEN] = '\0';
	strncpy(kval,key,_IPF_CONTEXT_MAX_KEYLEN);
	k.dptr = kval;
	k.dsize = strlen(kval);

	if(I2HashDelete(table,k) == 0){
		return True;
	}

	return False;
}

/*
 * Function:	IPFContextSet
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
IPFBoolean
IPFContextConfigSet(
	IPFContext	ctx,
	const char	*key,
	void		*value
	)
{
	assert(ctx);

	return ConfigSet(ctx->table,key,value);
}

void *
IPFContextConfigGet(
	IPFContext	ctx,
	const char	*key
	)
{
	assert(ctx);

	return ConfigGet(ctx->table,key);
}

IPFBoolean
IPFContextConfigDelete(
	IPFContext	ctx,
	const char	*key
	)
{
	assert(ctx);

	return ConfigDelete(ctx->table,key);
}

/*
 * Function:	IPFControlSet
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
IPFBoolean
IPFControlConfigSet(
	IPFControl	cntrl,
	const char	*key,
	void		*value
	)
{
	assert(cntrl);

	return ConfigSet(cntrl->table,key,value);
}

void *
IPFControlConfigGet(
	IPFControl	cntrl,
	const char	*key
	)
{
	assert(cntrl);

	return ConfigGet(cntrl->table,key);
}

IPFBoolean
IPFControlConfigDelete(
	IPFControl	cntrl,
	const char	*key
	)
{
	assert(cntrl);

	return ConfigDelete(cntrl->table,key);
}

/*
 * Function:	_IPFCallGetAESKey
 *
 * Description:
 * 	Calls the get_key function that is defined by the application.
 * 	If the application didn't define the get_key function, then provide
 * 	the default response of False.
 */
IPFBoolean
_IPFCallGetAESKey(
	IPFContext	ctx,		/* library context	*/
	const IPFUserID	userid,		/* identifies key	*/
	u_int8_t	*key_ret,	/* key - return		*/
	IPFErrSeverity	*err_ret	/* error - return	*/
)
{
	IPFGetAESKeyFunc	func;

	*err_ret = IPFErrOK;

	func = (IPFGetAESKeyFunc)IPFContextConfigGet(ctx,IPFGetAESKey);

	/*
	 * Default action is no encryption support.
	 */
	if(!func){
		return False;
	}

	return func(ctx,userid,key_ret,err_ret);
}

/*
 * Function:	_IPFCallCheckControlPolicy
 *
 * Description:
 * 	Calls the check_control_func that is defined by the application.
 * 	If the application didn't define the check_control_func, then provide
 * 	the default response of True(allowed).
 */
IPFBoolean
_IPFCallCheckControlPolicy(
	IPFControl	cntrl,		/* control record		*/
	IPFSessionMode	mode,		/* requested mode       	*/
	const IPFUserID	userid,		/* key identity			*/
	struct sockaddr	*local_sa_addr,	/* local addr or NULL		*/
	struct sockaddr	*remote_sa_addr,/* remote addr			*/
	IPFErrSeverity	*err_ret	/* error - return		*/
)
{
	IPFCheckControlPolicyFunc	func;

	*err_ret = IPFErrOK;

	func = (IPFCheckControlPolicyFunc)IPFContextConfigGet(cntrl->ctx,
							IPFCheckControlPolicy);

	/*
	 * Default action is to allow anything.
	 */
	if(!func){
		return True;
	}
	
	return func(cntrl,mode,userid,local_sa_addr,remote_sa_addr,err_ret);
}

/*
 * Function:	_IPFCallCheckTestPolicy
 *
 * Description:
 * 	Calls the check_test_func that is defined by the application.
 * 	If the application didn't define the check_test_func, then provide
 * 	the default response of True(allowed).
 */
IPFBoolean
_IPFCallCheckTestPolicy(
	IPFControl	cntrl,		/* control handle		*/
	IPFBoolean	local_sender,	/* Is local send or recv	*/
	struct sockaddr	*local,		/* local endpoint		*/
	struct sockaddr	*remote,	/* remote endpoint		*/
	socklen_t	sa_len,		/* saddr lens			*/
	IPFTestSpec	*test_spec,	/* test requested		*/
	void		**closure,
	IPFErrSeverity	*err_ret	/* error - return		*/
)
{
	IPFCheckTestPolicyFunc	func;

	*err_ret = IPFErrOK;

	func = (IPFCheckTestPolicyFunc)IPFContextConfigGet(cntrl->ctx,
							IPFCheckTestPolicy);
	/*
	 * Default action is to allow anything.
	 */
	if(!func){
		return True;
	}

	return func(cntrl,local_sender,local,remote,sa_len,test_spec,
			closure,err_ret);
}

/*
 * Function:	_IPFCallTestComplete
 *
 * Description:
 * 	Calls the "IPFTestComplete" that is defined by the application.
 * 	If the application didn't define the "IPFTestComplete" function, then
 * 	this is a no-op.
 *
 * 	The primary use for this hook is to free memory and other resources
 * 	(bandwidth etc...) allocated on behalf of this test.
 */
void
_IPFCallTestComplete(
	IPFTestSession	tsession,
	IPFAcceptType	aval
)
{
	IPFTestCompleteFunc	func;

	func = (IPFTestCompleteFunc)IPFContextConfigGet(tsession->cntrl->ctx,
							IPFTestComplete);
	/*
	 * Default action is nothing...
	 */
	if(!func){
		return;
	}

	func(tsession->cntrl,tsession->closure,aval);

	return;
}

/*
 * Function:	_IPFCallOpenFile
 *
 * Description:
 * 	Calls the "IPFOpenFile" that is defined by the application.
 * 	If the application didn't define the "IPFOpenFile" function, then
 * 	it won't be able to implement the "FetchSession" functionality or
 * 	run a "receive" endpoint from a "server" point-of-view.
 *
 * 	(This is not needed from the client point-of-view since it passes
 * 	a FILE* into the retrieve/receiver functions directly.)
 *
 * 	(basically - this is a hook to allow relatively simple changes
 * 	to the way ipcntrld saves/fetches session data.)
 *
 * 	The "closure" pointer is a pointer to the value that is returned
 * 	from the CheckTestPolicy function. This is the way resource
 * 	requests/releases can be adjusted based upon actual use.
 * 	(keeping policy separate from function is challenging... I hope
 * 	it is worth it...)
 *
 * 	"closure" will be NULL if this function is being called in the
 * 	"FetchSession" context.
 *
 */
FILE *
_IPFCallOpenFile(
	IPFControl	cntrl,			/* control handle	*/
	void		*closure,		/* null if r/o		*/
	IPFSID		sid,			/* sid			*/
	char		fname_ret[PATH_MAX]	/* return name		*/
)
{
	IPFOpenFileFunc	func;

	func = (IPFOpenFileFunc)IPFContextConfigGet(cntrl->ctx,
							IPFOpenFile);
	/*
	 * Default action is nothing...
	 */
	if(!func){
		return NULL;
	}

	return func(cntrl,closure,sid,fname_ret);
}

/*
 * Function:	_IPFCallCloseFile
 *
 * Description:
 * 	Calls the "IPFCloseFile" that is defined by the application.
 * 	If the application didn't define the "IPFCloseFile" function, then
 * 	fclose will be called on the fp.
 *
 * 	(The primary use for this hook is to implement the delete-on-fetch
 * 	functionality. i.e. once this is called on a file with that policy
 * 	setting, unlink can be called on the file.)
 */
void
_IPFCallCloseFile(
	IPFControl	cntrl,			/* control handle	*/
	void		*closure,
	FILE		*fp,
	IPFAcceptType	aval
)
{
	IPFCloseFileFunc	func;

	func = (IPFCloseFileFunc)IPFContextConfigGet(cntrl->ctx,
							IPFCloseFile);
	/*
	 * Default action is nothing...
	 */
	if(!func){
		int	rc;

		while(((rc = fclose(fp)) != 0) && (errno == EINTR));
		if(rc != 0){
			IPFError(cntrl->ctx,IPFErrFATAL,errno,"fclose(): %M");
		}
		return;
	}

	func(cntrl,closure,fp,aval);

	return;
}
