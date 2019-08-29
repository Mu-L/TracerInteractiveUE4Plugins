/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * Copyright by the Board of Trustees of the University of Illinois.         *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of HDF5.  The full HDF5 copyright notice, including     *
 * terms governing use, modification, and redistribution, is contained in    *
 * the files COPYING and Copyright.html.  COPYING can be found at the root   *
 * of the source code distribution tree; Copyright.html can be found at the  *
 * root level of an installed copy of the electronic HDF5 document set and   *
 * is linked from the top-level documents page.  It can also be found at     *
 * http://hdfgroup.org/HDF5/doc/Copyright.html.  If you do not have          *
 * access to either file, you may request a copy from help@hdfgroup.org.     *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include <stdlib.h>
#include <string.h>
#include "H5private.h"
#include "h5diff.h"
#include "h5diff_common.h"
#include "h5tools.h"
#include "h5tools_utils.h"

static int check_n_input( const char* );
static int check_p_input( const char* );
static int check_d_input( const char* );

/*
 * Command-line options: The user can specify short or long-named
 * parameters.
 */
static const char *s_opts = "hVrv:qn:d:p:Nc";
static struct long_options l_opts[] = {
    { "help", no_arg, 'h' },
    { "version", no_arg, 'V' },
    { "report", no_arg, 'r' },
    { "verbose", optional_arg, 'v' },
    { "quiet", no_arg, 'q' },
    { "count", require_arg, 'n' },
    { "delta", require_arg, 'd' },
    { "relative", require_arg, 'p' },
    { "nan", no_arg, 'N' },
    { "compare", no_arg, 'c' },
    { "use-system-epsilon", no_arg, 'e' },
    { "follow-symlinks", no_arg, 'l' },
    { "no-dangling-links", no_arg, 'x' },
    { "exclude-path", require_arg, 'E' },
    { NULL, 0, '\0' }
};

/*-------------------------------------------------------------------------
 * Function: check_options
 *
 * Purpose: parse command line input
 *
 *-------------------------------------------------------------------------
 */
static void check_options(diff_opt_t* options)
{
    /*--------------------------------------------------------------
     * check for mutually exclusive options 
     *--------------------------------------------------------------*/

    /* check between -d , -p, --use-system-epsilon.
     * These options are mutually exclusive.
     */
    if ((options->d + options->p + options->use_system_epsilon) > 1)
    {
        printf("%s error: -d, -p and --use-system-epsilon options are mutually-exclusive;\n", PROGRAMNAME);
        printf("use no more than one.\n");
        printf("Try '-h' or '--help' option for more information or see the %s entry in the 'HDF5 Reference Manual'.\n", PROGRAMNAME);
        h5diff_exit(EXIT_FAILURE);
    }
}


/*-------------------------------------------------------------------------
 * Function: parse_command_line
 *
 * Purpose: parse command line input
 *
 *-------------------------------------------------------------------------
 */

void parse_command_line(int argc,
                        const char* argv[],
                        const char** fname1,
                        const char** fname2,
                        const char** objname1,
                        const char** objname2,
                        diff_opt_t* options)
{
    int i;
    int opt;
    struct exclude_path_list *exclude_head, *exclude_prev, *exclude_node;

    /* process the command-line */
    memset(options, 0, sizeof (diff_opt_t));

    /* assume equal contents initially */
    options->contents = 1;

    /* NaNs are handled by default */
    options->do_nans = 1;

    /* not Listing objects that are not comparable */
    options->m_list_not_cmp = 0;

    /* initially no not-comparable. */
    /**this is bad in mixing option with results**/
    options->not_cmp=0;

    /* init for exclude-path option */
    exclude_head = NULL;

    /* parse command line options */
    while ((opt = get_option(argc, argv, s_opts, l_opts)) != EOF)
    {
        switch ((char)opt)
        {
        default:
            usage();
            h5diff_exit(EXIT_FAILURE);
        case 'h':
            usage();
            h5diff_exit(EXIT_SUCCESS);
        case 'V':
            print_version(h5tools_getprogname());
            h5diff_exit(EXIT_SUCCESS);
        case 'v':
            options->m_verbose = 1;
            /* This for loop is for handling style like 
             * -v, -v1, --verbose, --verbose=1.
             */
            for (i = 1; i < argc; i++)
            {                
                /* 
                 * short opt 
                 */
                if (!strcmp (argv[i], "-v"))  /* no arg */
            {
                    opt_ind--;
                    options->m_verbose_level = 0;
                    break;
            }
                else if (!strncmp (argv[i], "-v", (size_t)2))
            {
                    options->m_verbose_level = atoi(&argv[i][2]);
                    break;
            }    

                /* 
                 * long opt 
                 */
                if (!strcmp (argv[i], "--verbose"))  /* no arg */
            {
                    options->m_verbose_level = 0;
                    break;
            }
            else if ( !strncmp (argv[i], "--verbose", (size_t)9) && argv[i][9]=='=')
            {
                    options->m_verbose_level = atoi(&argv[i][10]);
                    break;
                }
            }
            break;
        case 'q':
            /* use quiet mode; supress the message "0 differences found" */
            options->m_quiet = 1;
            break;
        case 'r':
            options->m_report = 1;
            break;
        case 'l':
            options->follow_links = TRUE;
            break;
        case 'x':
            options->no_dangle_links = 1;
            break;
        case 'E':
            options->exclude_path = 1;
            
            /* create linked list of excluding objects */
            if( (exclude_node = (struct exclude_path_list*) HDmalloc(sizeof(struct exclude_path_list))) == NULL)
            {
                printf("Error: lack of memory!\n");
                h5diff_exit(EXIT_FAILURE);
            }

            /* init */
            exclude_node->obj_path = (char*)opt_arg;
            exclude_node->obj_type = H5TRAV_TYPE_UNKNOWN;
            exclude_prev = exclude_head;
            
            if (NULL == exclude_head)            
            {
                exclude_head = exclude_node;
                exclude_head->next = NULL;
            }
            else
            {
                while(NULL != exclude_prev->next)
                    exclude_prev=exclude_prev->next;

                exclude_node->next = NULL;
                exclude_prev->next = exclude_node;
            }            
            break;
        case 'd':
            options->d=1;

            if ( check_d_input( opt_arg )==-1)
            {
                printf("<-d %s> is not a valid option\n", opt_arg );
                usage();
                h5diff_exit(EXIT_FAILURE);
            }
            options->delta = atof( opt_arg );

            /* -d 0 is the same as default */
            if (options->delta == 0)
            options->d=0;

            break;

        case 'p':

            options->p=1;
            if ( check_p_input( opt_arg )==-1)
            {
                printf("<-p %s> is not a valid option\n", opt_arg );
                usage();
                h5diff_exit(EXIT_FAILURE);
            }
            options->percent = atof( opt_arg );

            /* -p 0 is the same as default */
            if (options->percent == 0)
            options->p = 0;

            break;

        case 'n':

            options->n=1;
            if ( check_n_input( opt_arg )==-1)
            {
                printf("<-n %s> is not a valid option\n", opt_arg );
                usage();
                h5diff_exit(EXIT_FAILURE);
            }
            options->count = atol( opt_arg );

            break;

        case 'N':
            options->do_nans = 0;
            break;
        case 'c':
            options->m_list_not_cmp = 1;
            break;
        case 'e':
            options->use_system_epsilon = 1;
            break;
        }
    }

    /* check options */
    check_options(options);

    /* if exclude-path option is used, keep the exclude path list */
    if (options->exclude_path)
        options->exclude = exclude_head;

    /* check for file names to be processed */
    if (argc <= opt_ind || argv[ opt_ind + 1 ] == NULL)
    {
        error_msg("missing file names\n");
        usage();
        h5diff_exit(EXIT_FAILURE);
    }

    *fname1 = argv[ opt_ind ];
    *fname2 = argv[ opt_ind + 1 ];
    *objname1 = argv[ opt_ind + 2 ];

    if ( *objname1 == NULL )
    {
        *objname2 = NULL;
        return;
    }

    if ( argv[ opt_ind + 3 ] != NULL)
    {
        *objname2 = argv[ opt_ind + 3 ];
    }
    else
    {
        *objname2 = *objname1;
    }


}


/*-------------------------------------------------------------------------
 * Function: print_info
 *
 * Purpose: print several information messages after h5diff call
 *
 *-------------------------------------------------------------------------
 */

 void  print_info(diff_opt_t* options)
 {
     if (options->m_quiet || options->err_stat )
         return;

     if (options->cmn_objs==0)
     {
         printf("No common objects found. Files are not comparable.\n");
         if (!options->m_verbose)
             printf("Use -v for a list of objects.\n");
     }

     if (options->not_cmp==1)
     {
         if ( options->m_list_not_cmp == 0 )
         {
             printf("--------------------------------\n");
             printf("Some objects are not comparable\n");
             printf("--------------------------------\n");
             if (options->m_verbose)
                 printf("Use -c for a list of objects without details of differences.\n");
             else
                 printf("Use -c for a list of objects.\n");
         }


     }

 }

/*-------------------------------------------------------------------------
 * Function: check_n_input
 *
 * Purpose: check for valid input
 *
 * Return: 1 for ok, -1 for fail
 *
 * Programmer: Pedro Vicente, pvn@ncsa.uiuc.edu
 *
 * Date: May 9, 2003
 *
 * Comments:
 *
 * Modifications:
 *
 *-------------------------------------------------------------------------
 */
static int
check_n_input( const char *str )
{
    unsigned i;
    char c;

    for ( i = 0; i < strlen(str); i++)
    {
        c = str[i];
        if ( i==0 )
        {
            if ( c < 49 || c > 57  ) /* ascii values between 1 and 9 */
                return -1;
        }
        else
            if ( c < 48 || c > 57  ) /* 0 also */
                return -1;
    }
    return 1;
}

/*-------------------------------------------------------------------------
 * Function: check_p_input
 *
 * Purpose: check for a valid p option input
 *
 * Return: 1 for ok, -1 for fail
 *
 * Date: May 9, 2003
 *
 * Comments:
 *
 * Modifications:
 *
 *-------------------------------------------------------------------------
 */
static int
check_p_input( const char *str )
{
    double x;

    /*
    the atof return value on a hexadecimal input is different
    on some systems; we do a character check for this
    */
    if (strlen(str)>2 && str[0]=='0' && str[1]=='x')
        return -1;

    x=atof(str);
    if (x<0)
        return -1;

    return 1;
}

/*-------------------------------------------------------------------------
 * Function: check_d_input
 *
 * Purpose: check for a valid d option input
 *
 * Return: 1 for ok, -1 for fail
 *
 * Date: November 11, 2007
 *
 * Comments:
 *
 * Modifications:
 *
 *-------------------------------------------------------------------------
 */
static int
check_d_input( const char *str )
{
    double x;

    /*
    the atof return value on a hexadecimal input is different
    on some systems; we do a character check for this
    */
    if (strlen(str)>2 && str[0]=='0' && str[1]=='x')
        return -1;

    x=atof(str);
    if (x <0)
        return -1;

    return 1;
}

/*-------------------------------------------------------------------------
 * Function: usage
 *
 * Purpose: print a usage message
 *
 * Return: void
 *
 *-------------------------------------------------------------------------
 */

void usage(void)
{
 printf("usage: h5diff [OPTIONS] file1 file2 [obj1[ obj2]] \n");
 printf("  file1             File name of the first HDF5 file\n");
 printf("  file2             File name of the second HDF5 file\n");
 printf("  [obj1]            Name of an HDF5 object, in absolute path\n");
 printf("  [obj2]            Name of an HDF5 object, in absolute path\n");
 printf("\n");
 printf("  OPTIONS\n");
 printf("   -h, --help\n");
 printf("         Print a usage message and exit.\n");
 printf("   -V, --version\n");
 printf("         Print version number and exit.\n");
 printf("   -r, --report\n");
 printf("         Report mode. Print differences.\n");
 printf("   -v --verbose\n");
 printf("         Verbose mode. Print differences information and list of objects.\n");
 printf("   -vN --verbose=N\n");
 printf("         Verbose mode with level. Print differences and list of objects.\n");
 printf("         Level of detail depends on value of N:\n");
 printf("          0 : Identical to '-v' or '--verbose'.\n");
 printf("          1 : All level 0 information plus one-line attribute\n");
 printf("              status summary.\n");
 printf("          2 : All level 1 information plus extended attribute\n");
 printf("              status report.\n");
 printf("   -q, --quiet\n");
 printf("         Quiet mode. Do not produce output.\n");
 printf("   --follow-symlinks\n");
 printf("         Follow symbolic links (soft links and external links and compare the)\n");
 printf("         links' target objects.\n");
 printf("         If symbolic link(s) with the same name exist in the files being\n");
 printf("         compared, then determine whether the target of each link is an existing\n");
 printf("         object (dataset, group, or named datatype) or the link is a dangling\n");
 printf("         link (a soft or external link pointing to a target object that does\n");
 printf("         not yet exist).\n");
 printf("         - If both symbolic links are dangling links, they are treated as being\n");
 printf("           the same; by default, h5diff returns an exit code of 0.\n");
 printf("           If, however, --no-dangling-links is used with --follow-symlinks,\n");
 printf("           this situation is treated as an error and h5diff returns an\n");
 printf("           exit code of 2.\n");
 printf("         - If only one of the two links is a dangling link,they are treated as\n");
 printf("           being different and h5diff returns an exit code of 1.\n");
 printf("           If, however, --no-dangling-links is used with --follow-symlinks,\n");
 printf("           this situation is treated as an error and h5diff returns an\n");
 printf("           exit code of 2.\n");
 printf("         - If both symbolic links point to existing objects, h5diff compares the\n");
 printf("           two objects.\n");
 printf("         If any symbolic link specified in the call to h5diff does not exist,\n");
 printf("         h5diff treats it as an error and returns an exit code of 2.\n");
 printf("   --no-dangling-links\n");
 printf("         Must be used with --follow-symlinks option; otherwise, h5diff shows\n");
 printf("         error message and returns an exit code of 2.\n");
 printf("         Check for any symbolic links (soft links or external links) that do not\n");
 printf("         resolve to an existing object (dataset, group, or named datatype).\n");
 printf("         If any dangling link is found, this situation is treated as an error\n");
 printf("         and h5diff returns an exit code of 2.\n");
 printf("   -c, --compare\n");
 printf("         List objects that are not comparable\n");
 printf("   -N, --nan\n");
 printf("         Avoid NaNs detection\n");
 printf("   -n C, --count=C\n");
 printf("         Print differences up to C. C must be a positive integer.\n");
 printf("   -d D, --delta=D\n");
 printf("         Print difference if (|a-b| > D). D must be a positive number.\n");
 printf("         Can not use with '-p' or '--use-system-epsilon'.\n");
 printf("   -p R, --relative=R\n");
 printf("         Print difference if (|(a-b)/b| > R). R must be a positive number.\n");
 printf("         Can not use with '-d' or '--use-system-epsilon'.\n");
 printf("   --use-system-epsilon\n");
 printf("         Print difference if (|a-b| > EPSILON), EPSILON is system defined value.\n");
 printf("         If the system epsilon is not defined,one of the following predefined\n");
 printf("         values will be used:\n");
 printf("           FLT_EPSILON = 1.19209E-07 for floating-point type\n");
 printf("           DBL_EPSILON = 2.22045E-16 for double precision type\n");
 printf("         Can not use with '-p' or '-d'.\n");
 printf("   --exclude-path \"path\" \n");
 printf("         Exclude the specified path to an object when comparing files or groups.\n");
 printf("         If a group is excluded, all member objects will also be excluded.\n");
 printf("         The specified path is excluded wherever it occurs.\n");
 printf("         This flexibility enables the same option to exclude either objects that\n");
 printf("         exist only in one file or common objects that are known to differ.\n");
 printf("\n");
 printf("         When comparing files, \"path\" is the absolute path to the excluded;\n");
 printf("         object; when comparing groups, \"path\" is similar to the relative\n");
 printf("         path from the group to the excluded object. This \"path\" can be\n");
 printf("         taken from the first section of the output of the --verbose option.\n");
 printf("         For example, if you are comparing the group /groupA in two files and\n");
 printf("         you want to exclude /groupA/groupB/groupC in both files, the exclude\n");
 printf("         option would read as follows:\n");
 printf("           --exclude-path \"/groupB/groupC\"\n");
 printf("\n");
 printf("         If there are multiple paths to an object, only the specified path(s)\n");
 printf("         will be excluded; the comparison will include any path not explicitly\n");
 printf("         excluded.\n");
 printf("         This option can be used repeatedly to exclude multiple paths.\n");
 printf("\n");

 printf(" Modes of output:\n");
 printf("  Default mode: print the number of differences found and where they occured\n");
 printf("  -r Report mode: print the above plus the differences\n");
 printf("  -v Verbose mode: print the above plus a list of objects and warnings\n");
 printf("  -q Quiet mode: do not print output\n");

 printf("\n");

 printf(" File comparison:\n");
 printf("  If no objects [obj1[ obj2]] are specified, the h5diff comparison proceeds as\n");
 printf("  a comparison of the two files' root groups.  That is, h5diff first compares\n");
 printf("  the names of root group members, generates a report of root group objects\n");
 printf("  that appear in only one file or in both files, and recursively compares\n");
 printf("  common objects.\n");
 printf("\n");

 printf(" Object comparison:\n");
 printf("  1) Groups \n");
 printf("      First compares the names of member objects (relative path, from the\n");
 printf("      specified group) and generates a report of objects that appear in only\n");
 printf("      one group or in both groups. Common objects are then compared recursively.\n");
 printf("  2) Datasets \n");
 printf("      Array rank and dimensions, datatypes, and data values are compared.\n");
 printf("  3) Datatypes \n");
 printf("      The comparison is based on the return value of H5Tequal.\n");
 printf("  4) Symbolic links \n");
 printf("      The paths to the target objects are compared.\n");
 printf("      (The option --follow-symlinks overrides the default behavior when\n");
 printf("       symbolic links are compared.).\n");
 printf("\n");

 printf(" Exit code:\n");
 printf("  0 if no differences, 1 if differences found, 2 if error\n");
 printf("\n");
 printf(" Examples of use:\n");
 printf(" 1) h5diff file1 file2 /g1/dset1 /g1/dset2\n");
 printf("    Compares object '/g1/dset1' in file1 with '/g1/dset2' in file2\n");
 printf("\n");
 printf(" 2) h5diff file1 file2 /g1/dset1\n");
 printf("    Compares object '/g1/dset1' in both files\n");
 printf("\n");
 printf(" 3) h5diff file1 file2\n");
 printf("    Compares all objects in both files\n");
 printf("\n");
 printf(" Notes:\n");
 printf("  file1 and file2 can be the same file.\n");
 printf("  Use h5diff file1 file1 /g1/dset1 /g1/dset2 to compare\n");
 printf("  '/g1/dset1' and '/g1/dset2' in the same file\n");
 printf("\n");
}
