/*
 * ex: set tabstop=4 ai expandtab softtabstop=4 shiftwidth=4:
 * -*- mode: c-basic-indent: 4; tab-width: 4; indent-tabs-mode: nil -*-
 *      $Id: iperf3.c 537 2012-08-18 20:00:00Z jef $
 */
/*
 *    File:         iperf3.c
 *
 *    Author:       Jef Poskanzer
 *                  LBNL
 *
 *    Date:         Sat Aug 18 20:06:56 UTC 2012
 *
 *    Description:
 *
 *    This file encapsulates the functionality required to run an
 *    iperf throughput tool in bwctl, using libiperf.
 *
 *    License:

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

   *  Redistributions of source code must retain the following copyright notice,
      this list of conditions and the disclaimer below.

                    Copyright (c) 2003-2012, Internet2
                             All rights reserved.

   *  Redistribution in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.

   *  Neither the name of Internet2 nor the names of its contributors may be
      used to endorse or promote products derived from this software without
      explicit prior written permission.

You are under no obligation whatsoever to provide any enhancements to Internet2,
or its contributors.  If you choose to provide your enhancements, or if you
choose to otherwise publish or distribute your enhancement, in source code form
without contemporaneously requiring end users to enter into a separate written
license agreement for such enhancements, then you thereby grant Internet2, its
contributors, and its members a non-exclusive, royalty-free, perpetual license
to copy, display, install, use, modify, prepare derivative works, incorporate
into the software or other computer software, distribute, and sublicense your
enhancements or derivative works thereof, in binary and source code form.

DISCLAIMER - THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
“AS IS” AND WITH ALL FAULTS.  THE UNIVERSITY OF DELAWARE, INTERNET2, ITS CONTRI-
BUTORS, AND ITS MEMBERS DO NOT IN ANY WAY WARRANT, GUARANTEE, OR ASSUME ANY RES-
PONSIBILITY, LIABILITY OR OTHER UNDERTAKING WITH RESPECT TO THE SOFTWARE. ANY E-
XPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRAN-
TIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND NON-INFRINGEMENT
ARE HEREBY DISCLAIMED AND THE ENTIRE RISK OF SATISFACTORY QUALITY, PERFORMANCE,
ACCURACY, AND EFFORT IS WITH THE USER THEREOF.  IN NO EVENT SHALL THE COPYRIGHT
OWNER, CONTRIBUTORS, OR THE UNIVERSITY CORPORATION FOR ADVANCED INTERNET DEVELO-
PMENT, INC. BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTIT-
UTE GOODS OR SERVICES; REMOVAL OR REINSTALLATION LOSS OF USE, DATA, SAVINGS OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILIT-
Y, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHE-
RWISE) ARISING IN ANY WAY OUT OF THE USE OR DISTRUBUTION OF THIS SOFTWARE, EVEN
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 *
 */
#include <bwlib/bwlibP.h>
#include <iperf_api.h>

/*
 * Function:    Iperf3Available
 *
 * Description:
 *              Tests whether iperf3 is available.  Since this is
 *              determined at configure time, the answer is always 'yes'.
 *
 * In Args:
 * Out Args:
 * Scope:
 *
 * Returns:
 *
 * Side Effect:
 *
 */
static BWLBoolean
Iperf3Available(
        BWLContext          ctx,
        BWLToolDefinition   tool
        )
{
    return True;
}

/*
 * Function:    Iperf3PreRunTest
 *
 * Description:
 *              Does all 'prep' work for running an iperf3 test.
 *
 *              Returns a 'closure' pointer. NULL indicates
 *              failure.
 *              This 'closure' pointer is passed on to the Iperf3RunTest.
 *
 *              (closure pointer is just the arg list for the exec)
 * In Args:
 *
 * Out Args:
 *
 * Scope:
 * Returns:
 * Side Effect:
 */
static void *
Iperf3PreRunTest(
        BWLContext          ctx,
        BWLTestSession      tsess
        )
{
    char            recvhost[MAXHOSTNAMELEN];
    char            sendhost[MAXHOSTNAMELEN];
    size_t          hlen;
    struct sockaddr *rsaddr;
    socklen_t       rsaddrlen;
    struct iperf_test *iperf_test;

    if( !(rsaddr = I2AddrSAddr(tsess->test_spec.receiver,&rsaddrlen))){
        BWLError(tsess->cntrl->ctx,BWLErrFATAL,EINVAL,
                "Iperf3PreRunTest(): Invalid receiver I2Addr");
        return NULL;
    }

    hlen = sizeof(recvhost);
    I2AddrNodeName(tsess->test_spec.receiver,recvhost,&hlen);
    if(!hlen){
        BWLError(tsess->cntrl->ctx,BWLErrFATAL,EINVAL,
                "Iperf3PreRunTest(): Invalid receiver I2Addr");
        return NULL;
    }

    hlen = sizeof(sendhost);
    I2AddrNodeName(tsess->test_spec.sender,sendhost,&hlen);
    if(!hlen){
        BWLError(tsess->cntrl->ctx,BWLErrFATAL,EINVAL,
                "Iperf3PreRunTest(): Invalid sender I2Addr");
        return NULL;
    }

    iperf_test = iperf_new_test();

    /* Set defaults. */
    iperf_defaults(iperf_test);

    /* -t test duration in seconds */
    iperf_set_test_duration( iperf_test, tsess->test_spec.duration );

    /* -i reporting interval in seconds */
    if(tsess->test_spec.report_interval){
        iperf_set_test_reporter_interval( iperf_test, tsess->test_spec.report_interval );
        iperf_set_test_stats_interval( iperf_test, tsess->test_spec.report_interval );
    }

    /* Set defaults, if UDP test */
    if(tsess->test_spec.udp){
        set_protocol(iperf_test, Pudp);
        iperf_set_test_blksize( iperf_test, DEFAULT_UDP_BLKSIZE );
        /* iperf_test->new_stream = iperf_new_udp_stream; */
    }

    if (tsess->test_spec.bandwidth) {
        iperf_set_test_rate( iperf_test, tsess->test_spec.bandwidth );
    }

    /* -l block size */
    if(tsess->test_spec.len_buffer){
        iperf_set_test_blksize( iperf_test, tsess->test_spec.len_buffer );
    }

    /* -p server port */
    iperf_set_test_server_port( iperf_test, tsess->tool_port );

    /* -w window size in bytes */
    iperf_set_test_socket_bufsize( iperf_test, tsess->test_spec.window_size );

    /* -m parallel test streams */
    if (tsess->test_spec.parallel_streams == 0) {
        iperf_set_test_num_streams( iperf_test, 1 );
    } else {
        iperf_set_test_num_streams( iperf_test, tsess->test_spec.parallel_streams );
    }

    /* -b (busy wait in UDP test) and -D (DSCP value for TOS
       byte): not used for the moment. */
    /* Multicast options not used too. */

    /*
     * XXX: Perhaps these should be validated earlier, in the CheckTest
     * function chain?
     */
    if(tsess->test_spec.units){
        switch((char)tsess->test_spec.units){
            case 'b':
            case 'B':
            case 'k':
            case 'K':
            case 'm':
            case 'M':
            case 'g':
            case 'G':
            case 'a':
            case 'A':
                break;
            default:
                fprintf(tsess->localfp,
                        "bwctl: tool(iperf): Invalid units (-f) specification %c",
                        (char)tsess->test_spec.units);
                return NULL;
                break;
        }
    }

    if(tsess->test_spec.outformat){
        switch((char)tsess->test_spec.outformat){
            case 'c':
                break;
            default:
                fprintf(tsess->localfp,
                        "bwctl: tool(iperf3): Invalid out format (-y) specification %c",
                        (char)tsess->test_spec.outformat);
                return NULL;
                break;

        }
    }

    /*
     * TODO: Fix this to allow UDP Iperf3 tests.
     */
    if(tsess->test_spec.udp){
        fprintf(stderr,
            "bwctl: There are some known problems with using iperf3 and UDP. Only run this if you're debugging the problem.\n");
        BWLError(ctx,BWLErrDEBUG,BWLErrPOLICY,
            "Iperf3PreRunTest: There are some known problems with using Iperf3 and UDP. Only run this if you're debugging the problem.\n");
    }

    if(tsess->conf_receiver){
        iperf_set_test_role( iperf_test, 's' ); // specify server side
    }else{
        iperf_set_test_role( iperf_test, 'c' ); // specify client side
        char* drh = strdup(recvhost);
        if( !drh ){
            BWLError(ctx,BWLErrFATAL,errno,"Iperf3PreRunTest():strdup(): %M");
            return NULL;
        }
        iperf_set_test_server_hostname( iperf_test, drh );
    }

    return (void *)iperf_test;
}

/*
 * Function:    Iperf3RunTest
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
static BWLBoolean
Iperf3RunTest(
        BWLContext          ctx,
        BWLTestSession      tsess __attribute__((unused)),
        void                *closure
        )
{
    struct iperf_test *iperf_test = closure;

    iperf_set_test_state( iperf_test, TEST_RUNNING );

    // XXX: it would be better to receive the output from iperf3 than this, but
    // this will do for now.
    //dup2(fileno(stderr), STDOUT_FILENO);
    //dup2(fileno(stderr), STDERR_FILENO);

    if (iperf_init_test(iperf_test) < 0) {
        fprintf(stderr, "Problem starting test\n");
        BWLError(ctx,BWLErrFATAL,EINVAL,"Problem starting test");
        exit(-1);
    }

    fprintf(stderr, "Test initialized\n");

    switch (iperf_get_test_role(iperf_test))
    {
        case 's':
            fprintf(stderr, "Running server\n");
            iperf_run_server(iperf_test);
            exit(0);
        case 'c':
            fprintf(stderr, "Running client\n");
            iperf_run_client(iperf_test);
            exit(0);
        default:
            BWLError(ctx,BWLErrFATAL,EINVAL,"invalid iperf test role: %c\n",iperf_get_test_role(iperf_test));
            exit(-1);
    }
}

BWLToolDefinitionRec    BWLToolIperf3 = {
    "iperf3",                /* name             */
    NULL,                    /* def_cmd          */
    NULL,                    /* def_server_cmd   */
    5001,                    /* def_port         */
    _BWLToolGenericParse,    /* parse            */
    Iperf3Available,         /* tool_avail       */
    _BWLToolGenericInitTest, /* init_test        */
    Iperf3PreRunTest,        /* pre_run          */
    Iperf3RunTest            /* run              */
};