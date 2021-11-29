//----- Include files -------------------------------------------------------
#include <assert.h>             // Needed for assert() macro
#include <stdlib.h>             // Needed for exit() and ato*()
#include <math.h>               // Needed for pow()

//----- Constants -----------------------------------------------------------
#define  FALSE          0       // Boolean false
#define  TRUE           1       // Boolean true

//----- Function prototypes -------------------------------------------------
int      zipf(double alpha, int n);  // Returns a Zipf random variable
double   rand_val(int seed);         // Jain's RNG

//===========================================================================
//=  Function to generate Zipf (power law) distributed random variables     =
//=    - Input: alpha and N                                                 =
//=    - Output: Returns with Zipf distributed random variable              =
//===========================================================================
//int zipf(double alpha, int n)
//{
//    static int first = TRUE;      // Static first time flag
//    static double c = 0;          // Normalization constant
//    double z;                     // Uniform random number (0 < z < 1)
//    double sum_prob;              // Sum of probabilities
//    double zipf_value;            // Computed exponential value to be returned
//    int    i;                     // Loop counter
//
//    // Compute normalization constant on first call only
//    if (first == TRUE)
//    {
//        for (i=1; i<=n; i++)
//            c = c + (1.0 / pow((double) i, alpha));
//        c = 1.0 / c;
//        first = FALSE;
//    }
//
//    // Pull a uniform random number (0 < z < 1)
//    do
//    {
//        z = rand_val(0);
//    }
//    while ((z == 0) || (z == 1));
//
//    // Map z to the value
//    sum_prob = 0;
//    for (i=1; i<=n; i++)
//    {
//        sum_prob = sum_prob + c / pow((double) i, alpha);
//        if (sum_prob >= z)
//        {
//            zipf_value = i;
//            break;
//        }
//    }
//
//    // Assert that zipf_value is between 1 and N
//    assert((zipf_value >=1) && (zipf_value <= n));
//
//    return(zipf_value);
//}

// https://stackoverflow.com/questions/9983239/how-to-generate-zipf-distributed-numbers-efficiently
int zipf(double alpha, int n)
{
    static int first = TRUE;      // Static first time flag
    static double c = 0;          // Normalization constant
    static double *sum_probs;     // Pre-calculated sum of probabilities
    double z;                     // Uniform random number (0 < z < 1)
    int zipf_value;               // Computed exponential value to be returned
    int    i;                     // Loop counter
    int low, high, mid;           // Binary-search bounds

    // Compute normalization constant on first call only
    if (first == TRUE)
    {
        for (i=1; i<=n; i++)
            c = c + (1.0 / pow((double) i, alpha));
        c = 1.0 / c;

        sum_probs = static_cast<double*>(malloc((n+1)*sizeof(*sum_probs)));
        sum_probs[0] = 0;
        for (i=1; i<=n; i++) {
            sum_probs[i] = sum_probs[i-1] + c / pow((double) i, alpha);
        }
        first = FALSE;
    }

    // Pull a uniform random number (0 < z < 1)
    do
    {
        z = rand_val(0);
    }
    while ((z == 0) || (z == 1));

    // Map z to the value
    low = 1, high = n, mid;
    do {
        mid = floor((low+high)/2);
        if (sum_probs[mid] >= z && sum_probs[mid-1] < z) {
            zipf_value = mid;
            break;
        } else if (sum_probs[mid] >= z) {
            high = mid-1;
        } else {
            low = mid+1;
        }
    } while (low <= high);

    // Assert that zipf_value is between 1 and N
    assert((zipf_value >=1) && (zipf_value <= n));

    return(zipf_value);
}

//=========================================================================
//= Multiplicative LCG for generating uniform(0.0, 1.0) random numbers    =
//=   - x_n = 7^5*x_(n-1)mod(2^31 - 1)                                    =
//=   - With x seeded to 1 the 10000th x value should be 1043618065       =
//=   - From R. Jain, "The Art of Computer Systems Performance Analysis," =
//=     John Wiley & Sons, 1991. (Page 443, Figure 26.2)                  =
//=========================================================================
double rand_val(int seed)
{
    const long  a =      16807;  // Multiplier
    const long  m = 2147483647;  // Modulus
    const long  q =     127773;  // m div a
    const long  r =       2836;  // m mod a
    static long x;               // Random int value
    long        x_div_q;         // x divided by q
    long        x_mod_q;         // x modulo q
    long        x_new;           // New x value

    // Set the seed if argument is non-zero and then return zero
    if (seed > 0)
    {
        x = seed;
        return(0.0);
    }

    // RNG using integer arithmetic
    x_div_q = x / q;
    x_mod_q = x % q;
    x_new = (a * x_mod_q) - (r * x_div_q);
    if (x_new > 0)
        x = x_new;
    else
        x = x_new + m;

    // Return a random value between 0.0 and 1.0
    return((double) x / m);
}