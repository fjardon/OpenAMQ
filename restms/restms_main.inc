/*===========================================================================

    Main body for RestMS server

    This file is defined as an include file so that it can be invoked
    directly from the main, setting up static variables that are used
    at different points in the initialisation process.  The separation
    of this code from the main code is intended to ease the creation
    of customised main programs without duplication of this code.

    Copyright (c) 1996-2008 iMatix Corporation

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or (at
    your option) any later version.

    This program is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    General Public License for more details.

    For information on alternative licensing for OEMs, please contact
    iMatix Corporation.
 *===========================================================================*/

#include "icl.h"
#include "ipr.h"
#include "smt.h"
#include "http.h"
#include "restms_amqp.h"
#include "restms_config.h"
#include "version.h"

static Bool
    s_opt_silent = FALSE,               //  -q means suppress messages
    s_opt_bkground = FALSE,             //  -s means run in background
    s_opt_showinfo = FALSE;             //  -i means show information
static char
    *s_opt_workdir = NULL,              //  -w specifies working directory
    *s_opt_config = NULL;               //  -s specifies configuration file
static int
    s_argc;
static char
    **s_argv;
static http_server_t
    *s_http_server;                     //  HTTP server instance

#define NOWARRANTY \
"This is free software; see the source for copying conditions.  There is NO\n" \
"warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n" \
    "\n"

#define USAGE                                                                \
    "RestMS is the iMatix REST Messaging Service.  This speaks HTTP to Web\n"\
    "clients on one side, and AMQP to an OpenAMQ broker at the other.  See\n"\
    "www.openamq.org for full information on using RestMS.\n"                \
    "\n"                                                                     \
    "Syntax: %s [options...]\n"                                              \
    "   Starts the RestMS server\n"                                          \
    "\n"                                                                     \
    "General options:\n"                                                     \
    "  -w directory     Working directory for server (current)\n"            \
    "  -s filename      Load custom settings from file (restms.cfg)\n"       \
    "  -X comment       Comment\n"                                           \
    "  -q               Quiet mode: no messages (no)\n"                      \
    "  -b               Run as background server process (no)\n"             \
    "  -f               Run as foreground console process (yes)\n"           \
    "  -v               Show version information\n"                          \
    "  -h               Show summary of command-line options\n"              \
    "  --help           Show detailed configuration help\n"                  \
    "\n"                                                                     

//  Parse arguments, return 0 if OK, 1 if error

static void
s_restms_initialise (int argc, char *argv [])
{
    Bool
        arg_error = FALSE;
    int
        argn;                           //  Argument number                  
    char
        *myname,                        //  Executable's name                
        *comment,                       //  We ignore this argument
        **argparm;                      //  Argument parameter to pick-up

    //  Save argument line for later use
    s_argc = argc;
    s_argv = argv;
    
    icl_system_initialise (argc, argv);
    icl_console_mode (ICL_CONSOLE_DATE, TRUE);
    myname  = argv [0];
    argparm = NULL;                     //  Argument parameter to pick-up
    for (argn = 1; argn < argc; argn++) {
        //  If argparm is set, we have to collect an argument parameter
        if (argparm) {
            if (*argv [argn] != '-') {  //  Parameter can't start with '-'
                *argparm = argv [argn];
                argparm = NULL;
            }
            else {
                arg_error = TRUE;
                break;
            }
        }
        else
        if (ipr_str_prefixed (argv [argn], "--"))
            argn++;                     //  Extended argument + value
        else
        if (*argv [argn] == '-') {
            switch (argv [argn][1]) {
                //  These switches take a parameter
                case 'w':
                    argparm = &s_opt_workdir;
                    break;
                case 's':
                    argparm = &s_opt_config;
                    break;
                case 'X':
                    argparm = &comment;
                    break;

                //  These switches have an immediate effect
                case 'i':
                    s_opt_showinfo = TRUE;
                    break;
                case 'q':
                    s_opt_silent = TRUE;
                    break;
                case 'b':
                    s_opt_bkground = TRUE;
                    break;
                case 'f':
                    s_opt_bkground = FALSE;
                    break;
                case 'v':
                    printf (SERVER_NAME "/" VERSION " - revision " SVN_REVISION "\n");
                    printf (BUILDMODEL "\n\n");
                    printf (COPYRIGHT "\n");
                    printf (NOWARRANTY);
                    printf ("Build model:" BUILDMODEL "\n");
                    printf ("Memory model: %s\n", 
                        icl_mem_allocator == ICL_MEM_THIN?   "thin":
                        icl_mem_allocator == ICL_MEM_FAT?    "fat":
                        icl_mem_allocator == ICL_MEM_DIRECT? "direct": "unknown");
#if (defined (BASE_THREADSAFE))
                    printf ("Threading model: multithreaded\n");
#else
                    printf ("Threading model: single threaded\n");
#endif
                    printf ("\nCompiler: " CCOPTS "\n");
                    exit (EXIT_SUCCESS);
                case 'h':
                    printf (SERVER_NAME "/" VERSION "\n");
                    printf (BUILDMODEL "\n\n");
                    printf (COPYRIGHT "\n");
                    printf (NOWARRANTY);
                    printf (USAGE, SERVER_NAME);
                    restms_config_cmdline_help ();
                    exit (EXIT_SUCCESS);

                //  Anything else is an error
                default:
                    arg_error = TRUE;
            }
        }
        else {
            arg_error = TRUE;
            break;
        }
    }
    if (arg_error) {
        icl_console_print ("E: invalid arguments - type '%s -h' for help", myname);
        exit (EXIT_FAILURE);
    }
    else
    if (argparm) {
        icl_console_print ("E: argument missing - type '%s -h' for help", myname);
        exit (EXIT_FAILURE);
    }

    if (s_opt_silent)
        icl_console_mode (ICL_CONSOLE_QUIET, TRUE);
    else {
        printf (SERVER_NAME "/" VERSION "\n");
        printf (BUILDMODEL "\n\n");
        printf (COPYRIGHT "\n");
        printf (NOWARRANTY);
    }
    //  Set server working directory if necessary
    if (s_opt_workdir && apr_filepath_set (s_opt_workdir, icl_global_pool) != APR_SUCCESS) {
        icl_console_print ("E: can't work in '%s' - %s", s_opt_workdir, strerror (errno));
        exit (EXIT_FAILURE);
    }
    //  Load configuration data into the config_table
    if (!s_opt_config)
        s_opt_config = "restms.cfg";
    restms_config = restms_config_new ("restms_base.cfg", s_opt_config, TRUE);
    restms_config_cmdline_parse (restms_config, SERVER_NAME, s_argc, s_argv);

    //  Shadow the http_config from the restms_config and command line so that
    //  we can run the http server properly.
    http_config = http_config_new (NULL, NULL, FALSE);
    http_config_shadow (http_config, "http_base.cfg", s_opt_config, s_argc, s_argv);

    //  Poke RestMS config settings into http_config table
    http_config_set_server_name (http_config, restms_config_server_name (restms_config));

    //  Set system options from HTTP server configuration
    smt_set_polling_threads (restms_config_polling_threads (restms_config));
    smt_set_working_threads (restms_config_working_threads (restms_config));
    smt_set_action_limit    (restms_config_kernel_limit    (restms_config));
    
    //  Move into the background if so desired
    if (s_opt_bkground) {
        char
           *background_args [] = { "-b", NULL };

        icl_console_print ("I: moving into the background...");
        if (ipr_process_server (NULL, NULL, argc, argv, background_args) == 0) {
            FILE
                *logfile;
            icl_shortstr_t
                logfile_name;

            icl_shortstr_fmt (logfile_name, "restms_%s.log", restms_config_port (restms_config));
            logfile = fopen (logfile_name, "a");
            icl_console_redirect (logfile);
            icl_console_print ("I: ************************   SERVER START   ************************");
            icl_console_print ("I: " SERVER_NAME "/" VERSION " - revision " SVN_REVISION);
        }
        else {
            icl_console_print ("E: backgrounding failed - server is halting");
            exit (EXIT_FAILURE);
        }
    }
    smt_initialise ();
    ipr_uid_set_real_user ();

    //  Start a single server instance on the configured HTTP port
    s_http_server = http_server_new ();
}

static void
s_restms_execute (void)
{
    http_portal_t
        *portal;

    //  Register standard plugins in path resolution order
    //  The s_http_server is the portal client
    //
    portal = restms_amqp_http_portal_new (NULL, "/amqp");
    http_server_http_portal_bind (s_http_server, portal);
    http_portal_request_announce (portal, "/amqp");
    http_portal_unlink (&portal);

    portal = http_file_http_portal_new (NULL, "/");
    http_server_http_portal_bind (s_http_server, portal);
    http_portal_request_announce (portal, "/");
    http_portal_unlink (&portal);

    //  Run SMT until finished
    smt_wait (0);
}

static void
s_restms_terminate (void)
{
    http_server_destroy (&s_http_server);
    http_config_destroy (&http_config);
    restms_config_destroy (&restms_config);

    if (s_opt_showinfo)
        icl_stats_dump ();
    icl_console_print ("I: shutdown is complete");
    icl_system_terminate ();
}