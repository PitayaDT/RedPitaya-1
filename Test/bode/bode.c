/**
 * $Id: lcr2.2.c 1246  $
 *
 * @brief Red Pitaya lcr algorythm 
 *
 * @Author1 Martin Cimerman   <cim.martin@gmail.com>
 * @Author2 Zumret Topcacic   <zumret_topcagic@hotmail.co
 *
 * This part of code is written in C programming language.
 * Please visit http://en.wikipedia.org/wiki/C_(programming_language)
 * for more details on the language used herein.
 */

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "fpga_awg.h"
#include "version.h"
#include <unistd.h>
#include <getopt.h>
#include <sys/param.h>
#include "main_osc.h"
#include "fpga_osc.h"

#include <complex.h>    /* Standart Library of Complex Numbers */
#define M_PI 3.14159265358979323846

/**
 * GENERAL DESCRIPTION:
 *
 * The code below defines bode analyzer
 * 
 * It was built on acquire and generate programs defined in /Test/ folder
 * data analasys returns: frequency, phase, amplitude
 *
 */

/** Maximal signal frequency [Hz] */
const double c_max_frequency = 62.5e6;

/** Minimal signal frequency [Hz] */
const double c_min_frequency = 0;

/** Maximal signal amplitude [Vpp] */
const double c_max_amplitude = 1.0;

/** AWG buffer length [samples]*/
#define n (16*1024)

/** AWG data buffer */
int32_t data[n];

/** Program name */
const char *g_argv0 = NULL;

/** Signal types */
typedef enum {
    eSignalSine,         ///< Sinusoidal waveform.
    eSignalSquare,       ///< Square waveform.
    eSignalTriangle,     ///< Triangular waveform.
    eSignalSweep         ///< Sinusoidal frequency sweep.
} signal_e;

/** AWG FPGA parameters */
typedef struct {
    int32_t  offsgain;   ///< AWG offset & gain.
    uint32_t wrap;       ///< AWG buffer wrap value.
    uint32_t step;       ///< AWG step interval.
} awg_param_t;

/** Oscilloscope module parameters as defined in main module
 * @see rp_main_params
 */
float t_params[PARAMS_NUM] = { 0, 1e6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

/** Max decimation index */
#define DEC_MAX 6

/** Decimation translation table */
static int g_dec[DEC_MAX] = { 1,  8,  64,  1024,  8192,  65536 };

/* Forward declarations */
void synthesize_signal(double ampl, double freq, signal_e type, double endfreq,
                       int32_t *data,
                       awg_param_t *params);
void write_data_fpga(uint32_t ch,
                     const int32_t *data,
                     const awg_param_t *awg);

int acquire_data(float **s , 
                uint32_t size);

int bode_data_analasys(float **s ,
                        uint32_t size,
                        uint32_t DC_bias,
                        float *Amplitude,
                        float *Phase,
                        double w_out,
                        int f);
/** Print usage information */
void usage() {

    const char *format =
        "%s version %s-%s\n"
        "\n"
        "Usage: %s   [channel] "
                    "[amplitude] "
                    "[DC_bias] "
                    "[averaging] "
                    "[steps] "
                    "[start frequnecy] "
                    "[stop frequency]"
                    "[scale type]"
                    "\n"
        "\n"
        "\tchannel              Channel to generate signal on [1, 2].\n"
        "\tamplitude            Peak-to-peak signal amplitude in V [0.0 - %1.1f].\n"
        "\tDC bias              for electrolit capacitors default = 0.\n"
        "\taveraging            number of averaging the measurements [1 - 10].\n"
        "\tsteps                steps made between frequency limits [1 - 1000].\n"
        "\tstart frequency      Signal frequency in Hz [%2.1f - %2.1e].\n"
        "\tstop frequency       Signal frequency in Hz [%2.1f - %2.1e].\n"
        "\tscale type           x scale 0 - linear 1 -log\n"
        "\n";

    fprintf( stderr, format, g_argv0, VERSION_STR, REVISION_STR,
             g_argv0, c_max_amplitude, c_min_frequency, c_max_frequency);
}

/** Gain string (lv/hv) to number (0/1) transformation */
int get_gain(int *gain, const char *str)
{
    if ( (strncmp(str, "lv", 2) == 0) || (strncmp(str, "LV", 2) == 0) ) {
        *gain = 0;
        return 0;
    }
    if ( (strncmp(str, "hv", 2) == 0) || (strncmp(str, "HV", 2) == 0) ) {
        *gain = 1;
        return 0;
    }

    fprintf(stderr, "Unknown gain: %s\n", str);
    return -1;
}

/* Allocates a memory with size num_of_el, memory has 1 dimension */
float *create_table_size(int num_of_el) {
    float *new_table = (float *)malloc( num_of_el * sizeof(float));
    return new_table;
}

/** Allocates a memory with size of: num_of_cols x num_of_rows */
float **create_2D_table_size(int num_of_rows, int num_of_cols) {
    float **new_table = (float **)malloc( num_of_rows * sizeof(float*));
    int i;
        for(i = 0; i < num_of_rows; i++) {
            new_table[i] = create_table_size(num_of_cols);
        }
    return new_table;
}

float max_array(float *arrayptr, int numofelements) {
  int i = 0;
  float max = -100000;//seting the minimum value possible

  for(i = 0; i < numofelements; i++)
  {
    if(max < arrayptr[i])
    {
      max = arrayptr[i];
    }
  }
  return max;
}

/* Trapezoidal method for integration interpolation, had to be defined */
float trapz(float *arrayptr, float T, int size1) {
  float result = 0;
  int i;
  //printf("size = %d\n", size);
  for (i =0; i < size1 - 1 ; i++) {
    result +=  ( arrayptr[i] + arrayptr[ i+1 ]  );
   
  }
    result = (T / (float)2) * result;
    return result;
}

/** Finds a mean value from the memmory arrayptr points to */
float mean_array(float *arrayptr, int numofelements) {
  int i = 1;
  float mean = 0;

  for(i = 0; i < numofelements; i++)
  {
    mean += arrayptr[i];
  }

  mean = mean / numofelements;
  return mean;
}

/* Finds a mean value from the memmory, acquiring values from rows */
float mean_array_column(float **arrayptr, int length, int column) {
    float result = 0;
    int i;

    for(i = 0; i < length; i++) {
        result = result + arrayptr[i][column];
    }
    result = result / length;
    return result;
}

/** Signal generator main */
int main(int argc, char *argv[])
{
    /* argument check */
    g_argv0 = argv[0];    
    
    if ( argc < 9 ) {
        usage();
        return -1;
    }
    
    /* Channel argument parsing */
    uint32_t ch = atoi(argv[1]) - 1; /* Zero based internally */
    if (ch > 1) {
        fprintf(stderr, "Invalid channel: %s\n", argv[1]);
        usage();
        return -1;
    }

    /* Signal amplitude argument parsing */
    double ampl = strtod(argv[2], NULL);
    if ( (ampl < 0.0) || (ampl > c_max_amplitude) ) {
        fprintf(stderr, "Invalid amplitude: %s\n", argv[2]);
        usage();
        return -1;
    }

    uint32_t DC_bias = strtod(argv[3], NULL);
    if ( (DC_bias < -2.0) || (DC_bias > 1.1) ) {
        fprintf(stderr, "Invalid DC bias:  %s\n", argv[3]);
        usage();
        return -1;
    }

    /* Number of measurments made and are later averaged */
    uint32_t averaging_num = strtod(argv[4], NULL);
    if ( (averaging_num < 1) || (averaging_num > 10) ) {
        fprintf(stderr, "Invalid averaging_num:  %s\n", argv[4]);
        usage();
        return -1;
    }

    /* Number of steps argument parsing - steps betwen start and end frequency*/
    double steps = strtod(argv[5], NULL);
    if ( (steps < 1) || (steps > 1000) ) {
        fprintf(stderr, "Invalid umber of steps:  %s\n", argv[5]);
        usage();
        return -1;
    }

    /* Start frequency argument parsing */
    double start_frequency = strtod(argv[6], NULL);
    if ( (start_frequency < c_min_frequency) || (start_frequency > c_max_frequency) ) {
        fprintf(stderr, "Invalid start frequency:  %s\n", argv[6]);
        usage();
        return -1;
    }

    /* Stop frequency argument parsing */
    double end_frequency = strtod(argv[7], NULL);
    if ( (end_frequency < c_min_frequency) || (end_frequency > c_max_frequency) ) {
        fprintf(stderr, "Invalid end frequency: %s\n", argv[7]);
        usage();
        return -1;
    }

    /* Scale type argument prsing [1] - logaritmic frequency steps [0] - linear steps */
    int scale_type = strtod(argv[8], NULL);
    if ( (scale_type < 0) || (scale_type > 1) ) {
        fprintf(stderr, "Invalid decidion:scale type %s\n", argv[8]);
        usage();
        return -1;
    }

    double frequency_step;
    double a,b,c;

    if(scale_type) { //if logaritmic scale required start and end frequency are transformed
        b = log10f( end_frequency );
        a = log10f( start_frequency );
        c = ( b - a ) /( steps - 1);
    }

    else {
    frequency_step = (end_frequency - start_frequency ) /( steps - 1);
    }

    /* end frequency must always be greather than start frequency */
    if ( end_frequency < start_frequency ) {
        fprintf(stderr, "End frequency has to be greater than the start frequency! \n");
        usage();
        return -1;
    }

    /* Signal type set to type sine. */
    signal_e type = eSignalSine;

    double    endfreq = 0; // endfreq set for inbulild sweep (generate)
    double    k;
    double    w_out; //angular velocity used in the algorythm
    uint32_t  min_periodes = 10; // max 20
    uint32_t  size; // nmber of samples varies with number of periodes
    int       f = 0; // used in for lop, seting the decimation
    int       i1, fr; // iterators in for loops
    int       equal = 0; //parameter initialized for generator functionality
    int       shaping = 0; //parameter initialized for generator functionality
    int       first_delay = 0;//delay required before first acquire
    float     **s = create_2D_table_size(SIGNALS_NUM, SIGNAL_LENGTH); // raw data saved to this location

    float *Amplitude                = (float *)malloc( (averaging_num + 1) * sizeof(float));
    float *Phase                    = (float *)malloc( (averaging_num + 1) * sizeof(float));
    float **data_for_avreaging      = create_2D_table_size((averaging_num + 1), 2 );
    float *measured_data_amplitude  = (float *)malloc((2) * sizeof(float) );
    float *measured_data_phase      = (float *)malloc((2) * sizeof(float) );
    float *frequency                = (float *)malloc((steps + 1) * sizeof(float) );
    
    /* Initialization of Oscilloscope application */
    if(rp_app_init() < 0) {
        fprintf(stderr, "rp_app_init() failed!\n");
    return -1;
    }


    for ( fr = 0; fr < steps; fr++ ) {
        
        /* scale type dictates frequency used in for iterations */
        if ( scale_type ) { // log scle
            k = powf( 10, ( c * (float)fr ) + a );
            frequency[ fr ] =  k ;
        }
        else { // lin scale
            frequency[ fr ] = start_frequency + ( frequency_step * fr );
        }

        w_out = frequency[ fr ] * 2 * M_PI; // omega - angular velocity

        /* Signal generator generates first signal before measuring proces begins
        *  this has to be set because first results are inaccurate otherwise
        */
        awg_param_t params;
        /* Prepare data buffer (calculate from input arguments) */
        synthesize_signal(ampl, frequency[fr], type, endfreq, data, &params);
        /* Write the data to the FPGA and set FPGA AWG state machine */
        write_data_fpga(ch, data, &params);

        for ( i1 = 0; i1 < averaging_num; i1++ ) {

            /* decimation changes depending on frequency */
            if      (frequency[fr] >= 160000){      f=0;    }
            else if (frequency[fr] >= 20000) {      f=1;    }    
            else if (frequency[fr] >= 2500)  {      f=2;    }    
            else if (frequency[fr] >= 160)   {      f=3;    }    
            else if (frequency[fr] >= 20)    {      f=4;    }     
            else if (frequency[fr] >= 2.5)   {      f=5;    }

            /* setting decimtion */
            if (f != DEC_MAX) {
                t_params[TIME_RANGE_PARAM] = f;
            } else {
                fprintf(stderr, "Invalid decimation DEC\n");
                usage();
                return -1;
            }
            
            /* calculating num of samples */
            size = round( ( min_periodes * 125e6 ) / ( frequency[fr] * g_dec[f] ) );

            /* Filter parameters for signal Acqusition */
            t_params[EQUAL_FILT_PARAM] = equal;
            t_params[SHAPE_FILT_PARAM] = shaping;

            /* Setting of parameters in Oscilloscope main module for signal Acqusition */
            if(rp_set_params((float *)&t_params, PARAMS_NUM) < 0) {
                fprintf(stderr, "rp_set_params() failed!\n");
                return -1;
            }

            if (first_delay == 0)
            {
                usleep(71754);
                first_delay = 1;
            }

            /* ADC Data acqusition - saved to s */
            if (acquire_data( s, size ) < 0) {
                printf("error acquiring data @ acquire_data\n");
                return -1;
            }

            /* data manipulation - returnes Z (complex impedance) */
            if( bode_data_analasys( s, size, DC_bias, Amplitude, Phase, w_out, f) < 0) {
                printf("error data analysis bode_data_analasys\n");
                return -1;
            }

            /* Saving data */
            data_for_avreaging[ i1 ][ 1 ] = *Amplitude;
            data_for_avreaging[ i1 ][ 2 ] = *Phase;
        } // avearging loop end

        /* Calculating and saving mean values */
        measured_data_amplitude[ 1 ] = mean_array_column( data_for_avreaging, averaging_num, 1 );
        measured_data_phase[ 1 ]     = mean_array_column( data_for_avreaging, averaging_num, 2 );

        printf(" %.0f    %.5f    %.5f\n", frequency[fr], measured_data_phase[ 1 ], measured_data_amplitude[ 1 ]);
    } // end of frequency sweep loop
   
    return 0;
}

/**
 * Synthesize a desired signal.
 *
 * Generates/synthesized  a signal, based on three pre-defined signal
 * types/shapes, signal amplitude & frequency. The data[] vector of 
 * samples at 125 MHz is generated to be re-played by the FPGA AWG module.
 *
 * @param ampl  Signal amplitude [Vpp].
 * @param freq  Signal frequency [Hz].
 * @param type  Signal type/shape [Sine, Square, Triangle].
 * @param data  Returned synthesized AWG data vector.
 * @param awg   Returned AWG parameters.
 *
 */
void synthesize_signal(double ampl, double freq, signal_e type, double endfreq,
                       int32_t *data,
                       awg_param_t *awg) {

    uint32_t i;

    /* Various locally used constants - HW specific parameters */
    const int dcoffs = -155;
    const int trans0 = 30;
    const int trans1 = 300;
    const double tt2 = 0.249;

    /* This is where frequency is used... */
    awg->offsgain = (dcoffs << 16) + 0x1fff;
    awg->step = round(65536 * freq/c_awg_smpl_freq * n);
    awg->wrap = round(65536 * (n-1));

    int trans = freq / 1e6 * trans1; /* 300 samples at 1 MHz */
    uint32_t amp = ampl * 4000.0;    /* 1 Vpp ==> 4000 DAC counts */
    if (amp > 8191) {
        /* Truncate to max value if needed */
        amp = 8191;
    }

    if (trans <= 10) {
        trans = trans0;
    }

    /* Fill data[] with appropriate buffer samples */
    for(i = 0; i < n; i++) {
        
        /* Sine */
        if (type == eSignalSine) {
            data[i] = round(amp * cos(2*M_PI*(double)i/(double)n));
        }
 
        /* Square */
        if (type == eSignalSquare) {
            data[i] = round(amp * cos(2*M_PI*(double)i/(double)n));
            if (data[i] > 0)
                data[i] = amp;
            else 
                data[i] = -amp;

            /* Soft linear transitions */
            double mm, qq, xx, xm;
            double x1, x2, y1, y2;    

            xx = i;       
            xm = n;
            mm = -2.0*(double)amp/(double)trans; 
            qq = (double)amp * (2 + xm/(2.0*(double)trans));
            
            x1 = xm * tt2;
            x2 = xm * tt2 + (double)trans;
            
            if ( (xx > x1) && (xx <= x2) ) {  
                
                y1 = (double)amp;
                y2 = -(double)amp;
                
                mm = (y2 - y1) / (x2 - x1);
                qq = y1 - mm * x1;

                data[i] = round(mm * xx + qq); 
            }
            
            x1 = xm * 0.75;
            x2 = xm * 0.75 + trans;
            
            if ( (xx > x1) && (xx <= x2)) {  
                    
                y1 = -(double)amp;
                y2 = (double)amp;
                
                mm = (y2 - y1) / (x2 - x1);
                qq = y1 - mm * x1;
                
                data[i] = round(mm * xx + qq); 
            }
        }
        
        /* Triangle */
        if (type == eSignalTriangle) {
            data[i] = round(-1.0*(double)amp*(acos(cos(2*M_PI*(double)i/(double)n))/M_PI*2-1));
        }

        /* Sweep */
        /* Loops from i = 0 to n = 16*1024. Generates a sine wave signal that
           changes in frequency as the buffer is filled. */
        double start = 2 * M_PI * freq;
        double end = 2 * M_PI * endfreq;
        if (type == eSignalSweep) {
            double sampFreq = c_awg_smpl_freq; // 125 MHz
            double t = i / sampFreq; // This particular sample
            double T = n / sampFreq; // Wave period = # samples / sample frequency
            /* Actual formula. Frequency changes from start to end. */
            data[i] = round(amp * (sin((start*T)/log(end/start) * ((exp(t*log(end/start)/T)-1)))));
        }
        
        /* TODO: Remove, not necessary in C/C++. */
        if(data[i] < 0)
            data[i] += (1 << 14);
    }
}

/**
 * Write synthesized data[] to FPGA buffer.
 *
 * @param ch    Channel number [0, 1].
 * @param data  AWG data to write to FPGA.
 * @param awg   AWG paramters to write to FPGA.
 */
void write_data_fpga(uint32_t ch,
                     const int32_t *data,
                     const awg_param_t *awg) {

    uint32_t i;

    fpga_awg_init();

    if(ch == 0) {
        /* Channel A */
        g_awg_reg->state_machine_conf = 0x000041;
        g_awg_reg->cha_scale_off      = awg->offsgain;
        g_awg_reg->cha_count_wrap     = awg->wrap;
        g_awg_reg->cha_count_step     = awg->step;
        g_awg_reg->cha_start_off      = 0;

        for(i = 0; i < n; i++) {
            g_awg_cha_mem[i] = data[i];
        }
    } else {
        /* Channel B */
        g_awg_reg->state_machine_conf = 0x410000;
        g_awg_reg->chb_scale_off      = awg->offsgain;
        g_awg_reg->chb_count_wrap     = awg->wrap;
        g_awg_reg->chb_count_step     = awg->step;
        g_awg_reg->chb_start_off      = 0;

        for(i = 0; i < n; i++) {
            g_awg_chb_mem[i] = data[i];
        }
    }

    /* Enable both channels */
    /* TODO: Should this only happen for the specified channel?
     *       Otherwise, the not-to-be-affected channel is restarted as well
     *       causing unwanted disturbances on that channel.
     */
    g_awg_reg->state_machine_conf = 0x110011;

    fpga_awg_exit();
}

/**
 * acquire data from FPGA to memory (s)
 *
 * @param **s   points to a mmemory where data is saved
 * @param size  return data size
 */
int acquire_data(float **s , 
                uint32_t size) {
    int retries = 150000;
    int j, sig_num, sig_len;
    int ret_val;

    while(retries >= 0) {
        if((ret_val = rp_get_signals(&s, &sig_num, &sig_len)) >= 0) {
            /* Signals acquired in s[][]:
             * s[0][i] - TODO
             * s[1][i] - Channel ADC1 raw signal
             * s[2][i] - Channel ADC2 raw signal
             */
            for(j = 0; j < MIN(size, sig_len); j++) {
                //printf("%7d, %7d\n",(int)s[1][j], (int)s[2][j]);
            }
            break;
        }
        if(retries-- == 0) {
            fprintf(stderr, "Signal scquisition was not triggered!\n");
            break;
        }
        usleep(2000);
    }
    usleep(30000); // delay for pitaya to operate correctly
    return 1;
}

/**
 * Acquired data analasys function.
 * function returnes phase and frequency.
 *
 * @param s        points to a mmemory where data is read from
 * @param size     size o data s
 * @param DC_bias  parameter for electrolytic capacitor data manipulation
 * @param R_shunt  shunt resistor's value ( check the front end circuit in manual )
 * @param Z        returned impedance data, in complex form
 * @param w_out    angualr velocity
 * @param f        decimation selector
 */
int bode_data_analasys(float **s ,
                        uint32_t size,
                        uint32_t DC_bias,
                        float *Amplitude,
                        float *Phase,
                        double w_out,
                        int f) {
    int i2, i3;
    float **U_acq = create_2D_table_size(SIGNALS_NUM, SIGNAL_LENGTH);
    /* Signals multiplied by the reference signal (sin) */
    float *U1_sampled_X = (float *) malloc( size * sizeof( float ) );
    float *U1_sampled_Y = (float *) malloc( size * sizeof( float ) );
    float *U2_sampled_X = (float *) malloc( size * sizeof( float ) );
    float *U2_sampled_Y = (float *) malloc( size * sizeof( float ) );
    /* Signals return by trapezoidal method in complex */
    float *X_component_lock_in_1 = (float *) malloc( size * sizeof( float ) );
    float *X_component_lock_in_2 = (float *) malloc( size * sizeof( float ) );
    float *Y_component_lock_in_1 = (float *) malloc( size * sizeof( float ) );
    float *Y_component_lock_in_2 = (float *) malloc( size * sizeof( float ) );
    /* Voltage, current and their phases calculated */
    float U1_amp;
    float Phase_U1_amp;
    float U2_amp;
    float Phase_U2_amp;
    float Phase_internal;
    //float Z_phase_deg_imag;  // may cuse errors because not complex
    float T; // Sampling time in seconds
    float *t = create_table_size(16384);

    T = ( g_dec[f] / 125e6 );
    //printf("T = %f;\n",T );

    for(i2 = 0; i2 < (size - 1); i2++) {
        t[i2] = i2;
    }
    //printf("size1 = %d\n", size);

    /* Transform signals from  AD - 14 bit to voltage [ ( s / 2^14 ) * 2 ] */
    for (i2 = 0; i2 < SIGNALS_NUM; i2++) { // only the 1 and 2 are used for i2
        for(i3 = 0; i3 < size; i3 ++ ) { 
            U_acq[i2][i3] = ( ( s[i2][i3] ) * (float)( 2 - DC_bias ) ) / (float)16384 ; //division comes after multiplication, this way no accuracy is lost
            //printf("data(%d,%d) = %f;\n",(i2+1), (i3+1), U_acq[i2][i3] );
        }
    }

    /* Acquired signals must be multiplied by the reference signals, used for lock in metod */
    float ang;
    for( i2 = 0; i2 < size; i2++) {
        ang = (i2 * T * w_out);
        //printf("ang(%d) = %f \n", (i2+1), ang);
        U1_sampled_X[i2] = U_acq[1][i2] * sin( ang );
        U1_sampled_Y[i2] = U_acq[1][i2] * sin( ang+ (M_PI/2) );

        U2_sampled_X[i2] = U_acq[2][i2] * sin( ang );
        U2_sampled_Y[i2] = U_acq[2][i2] * sin( ang +(M_PI/2) );
    }

    /* Trapezoidal method for calculating the approximation of an integral */
    X_component_lock_in_1[1] = trapz( U1_sampled_X, (float)T, size );
    Y_component_lock_in_1[1] = trapz( U1_sampled_Y, (float)T, size );

    X_component_lock_in_2[1] = trapz( U2_sampled_X, (float)T, size );
    Y_component_lock_in_2[1] = trapz( U2_sampled_Y, (float)T, size );
    
    /* Calculating voltage amplitude and phase */
    U1_amp = (float)2 * (sqrtf( powf( X_component_lock_in_1[ 1 ] , (float)2 ) + powf( Y_component_lock_in_1[ 1 ] , (float)2 )));
    Phase_U1_amp = atan2f( Y_component_lock_in_1[ 1 ], X_component_lock_in_1[ 1 ] );

    /* Calculating current amplitude and phase */
    U2_amp = (float)2 * (sqrtf( powf( X_component_lock_in_2[ 1 ], (float)2 ) + powf( Y_component_lock_in_2[ 1 ] , (float)2 ) ) );
    Phase_U2_amp = atan2f( Y_component_lock_in_2[1], X_component_lock_in_2[1] );
    
    Phase_internal = Phase_U2_amp - Phase_U1_amp ;

    if (Phase_internal <=  (-M_PI) )
    {
        Phase_internal = Phase_internal +(2*M_PI);
    }
    else if ( Phase_internal >= M_PI )
    {
        Phase_internal = Phase_internal -(2*M_PI) ;
    }
    else 
    {
        Phase_internal = Phase_internal;
    } 
 
   
    *Amplitude = 10*log( U2_amp / U1_amp );;
    *Phase = Phase_internal * ( 180/M_PI );


    //TODO free allocatec memmory
    return 1;
}
