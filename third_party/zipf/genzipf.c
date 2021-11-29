// https://www.csee.usf.edu/~kchriste/tools/genzipf.c

//==================================================== file = genzipf.c =====
//=  Program to generate Zipf (power law) distributed random variables      =
//===========================================================================
//=  Notes: 1) Writes to a user specified output file                       =
//=         2) Generates user specified number of values                    =
//=         3) Run times is same as an empirical distribution generator     =
//=         4) Implements p(i) = C/i^alpha for i = 1 to N where C is the    =
//=            normalization constant (i.e., sum of p(i) = 1).              =
//=-------------------------------------------------------------------------=
//= Example user input:                                                     =
//=                                                                         =
//=   ---------------------------------------- genzipf.c -----              =
//=   -     Program to generate Zipf random variables        -              =
//=   --------------------------------------------------------              =
//=   Output file name ===================================> output.dat      =
//=   Random number seed =================================> 1               =
//=   Alpha vlaue ========================================> 1.0             =
//=   N value ============================================> 1000            =
//=   Number of values to generate =======================> 5               =
//=   --------------------------------------------------------              =
//=   -  Generating samples to file                          -              =
//=   --------------------------------------------------------              =
//=   --------------------------------------------------------              =
//=   -  Done!                                                              =
//=   --------------------------------------------------------              =
//=-------------------------------------------------------------------------=
//= Example output file ("output.dat" for above):                           =
//=                                                                         =
//=   1                                                                     =
//=   1                                                                     =
//=   161                                                                   =
//=   17                                                                    =
//=   30                                                                    =
//=-------------------------------------------------------------------------=
//=  Build: bcc32 genzipf.c                                                 =
//=-------------------------------------------------------------------------=
//=  Execute: genzipf                                                       =
//=-------------------------------------------------------------------------=
//=  Author: Kenneth J. Christensen                                         =
//=          University of South Florida                                    =
//=          WWW: http://www.csee.usf.edu/~christen                         =
//=          Email: christen@csee.usf.edu                                   =
//=-------------------------------------------------------------------------=
//=  History: KJC (11/16/03) - Genesis (from genexp.c)                      =
//===========================================================================
//----- Include files -------------------------------------------------------
#include <assert.h>             // Needed for assert() macro
#include <stdio.h>              // Needed for printf()
#include <stdlib.h>             // Needed for exit() and ato*()
#include "genzipf.hpp"

//===== Main program ========================================================
int main(void)
{
    FILE   *fp;                   // File pointer to output file
    char   file_name[256];        // Output file name string
    char   temp_string[256];      // Temporary string variable
    double alpha;                 // Alpha parameter
    double n;                     // N parameter
    int    num_values;            // Number of values
    int    zipf_rv;               // Zipf random variable
    int    i;                     // Loop counter

    // Output banner
    printf("---------------------------------------- genzipf.c ----- \n");
    printf("-     Program to generate Zipf random variables        - \n");
    printf("-------------------------------------------------------- \n");

    // Prompt for output filename and then create/open the file
    printf("Output file name ===================================> ");
    scanf("%s", file_name);
    fp = fopen(file_name, "w");
    if (fp == NULL)
    {
        printf("ERROR in creating output file (%s) \n", file_name);
        exit(1);
    }

    // Prompt for random number seed and then use it
    printf("Random number seed (greater than 0) ================> ");
    scanf("%s", temp_string);
    rand_val((int) atoi(temp_string));

    // Prompt for alpha value
    printf("Alpha value ========================================> ");
    scanf("%s", temp_string);
    alpha = atof(temp_string);

    // Prompt for N value
    printf("N value ============================================> ");
    scanf("%s", temp_string);
    n = atoi(temp_string);

    // Prompt for number of values to generate
    printf("Number of values to generate =======================> ");
    scanf("%s", temp_string);
    num_values = atoi(temp_string);

    // Output "generating" message
    printf("-------------------------------------------------------- \n");
    printf("-  Generating samples to file                          - \n");
    printf("-------------------------------------------------------- \n");

    // Generate and output zipf random variables
    for (i=0; i<num_values; i++)
    {
        zipf_rv = zipf(alpha, n);
        fprintf(fp, "%d \n", zipf_rv);
    }

    // Output "done" message and close the output file
    printf("-------------------------------------------------------- \n");
    printf("-  Done! \n");
    printf("-------------------------------------------------------- \n");
    fclose(fp);

    return 0;
}