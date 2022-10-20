#ifndef MACROS_H_
#define MACROS_H_

/* Macros for tasks */
#define TASKSTACKSIZE               1024

/* Macros for i2c read/write operations */
#define SLAVE_ADDRESS               0x13

/* Macros for UART read/write operations */


/* Macros for oximeter initialization */
#define COMMAND_REGISTER            0x80
#define CMD_MEASUREMENT_DISABLE     0x00
#define CMD_MEASUREMENT_ENABLE      0x01
#define BIO_MEASUREMENT_ENABLE      0x02
#define BIO_RATE_250                0x07
#define BIO_MOD_SET                 0x65
#define BIO_MODULATOR_TIMING        0x8F
#define BIOSENSOR_RATE              0x82
#define BIO_RES_HIGH                0x87
#define BIO_RES_LOW                 0x88

/*Macros for heart-rate estimation */
#define SAMPLE_SIZE                 4
#define MEASURE_SIZE                10
#define THRESHOLD                    3
#define THRESHOLD_FINGER             10000
#define BPM_LOW_VALUE               30
#define BPM_HIGH_VALUE              270
#define MINUTE_IN_MS                60000
#define AVERAGING_TIME_IN_MS        50



#endif /* MACROS_H_ */
