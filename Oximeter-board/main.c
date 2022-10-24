/* XDCtools Header files */
#include <xdc/std.h>
#include <xdc/runtime/System.h>
#include <xdc/runtime/Timestamp.h>
#include <xdc/runtime/Types.h>

/* BIOS Header files */
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/sysbios/knl/Queue.h>
#include <ti/sysbios/knl/Semaphore.h>
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/sysbios/knl/Semaphore.h>

/* TI-RTOS Header files */
#include <ti/drivers/GPIO.h>
#include <ti/drivers/I2C.h>
#include <ti/drivers/UART.h>

/* Board Header file */
#include "Board.h"

/* User defined includes */
#include <stdlib.h>
#include <stdint.h>
#include "macros.h"
#include "stdbool.h"

/* Extern functions */
//extern void measureFxn(UArg arg0, UArg arg1);
Void initFxn(UArg arg0, UArg arg1);

/* Global variables */
Task_Struct initStruct;
Char initStack[TASKSTACKSIZE];

Task_Struct measureStruct;
Char measureStack[3 * TASKSTACKSIZE];

Task_Struct heartbeatStruct;
Char heartbeatStack[3 * TASKSTACKSIZE];

Task_Struct printStruct;
Char printStack[1 * TASKSTACKSIZE];

Task_Struct diodeStruct;
Char diodeStack[1 * TASKSTACKSIZE];

Semaphore_Struct semMeasureStruct;
Semaphore_Handle semMeasure;

Semaphore_Struct semDiodeStruct;
Semaphore_Handle semDiode;

Semaphore_Struct semBPMStruct;
Semaphore_Handle semBPM;

Queue_Handle queueBPM;
Queue_Handle queueMeasure;

typedef struct BioValue {
    Queue_Elem elem;
    float last;
} BioVal;

typedef struct BeatsPerMinute {
    Queue_Elem elem;
    int bpm;
} BPM;

int beatsPerMinuteDiode;

/* I2C global variables */
I2C_Handle i2c;
I2C_Params i2cParams;

/* UART global variables */
UART_Handle uart;
UART_Params uartParams;

/* === UART helper functions === */
void UARTinitialization(void)
{
        UART_Params_init(&uartParams);
        uartParams.writeDataMode = UART_DATA_BINARY;
        uartParams.readDataMode = UART_DATA_BINARY;
        uartParams.readReturnMode = UART_RETURN_FULL;
        uartParams.readEcho = UART_ECHO_OFF;
        uartParams.baudRate = 9600;
        uart = UART_open(Board_UART0, &uartParams);

        if (uart == NULL) {
            System_abort("Error opening the UART");
        }
}

/* === I2C helper functions === */
void I2Cinitialization(void){
        I2C_Params_init(&i2cParams);
        i2c = I2C_open(Board_I2C1, &i2cParams);
        if(i2c == NULL) {
            System_printf("I2C not opened!\n");
            System_flush();
        }
}

void writeRegister(uint8_t registar, uint8_t value){
        uint8_t txBuffer[2];

        I2C_Transaction i2cTransaction;
        i2cTransaction.slaveAddress = SLAVE_ADDRESS;
        i2cTransaction.writeBuf = txBuffer;
        i2cTransaction.writeCount = 2;
        i2cTransaction.readBuf = NULL;
        i2cTransaction.readCount = 0;

        txBuffer[0] = registar;
        txBuffer[1] = value;

        if (!I2C_transfer(i2c, &i2cTransaction))
                System_abort("Bad I2C transfer\n");
}

void readRegister(uint8_t registar, uint8_t *data){
        uint8_t txBuffer[1];

        I2C_Transaction i2cTransaction;
        i2cTransaction.slaveAddress = SLAVE_ADDRESS;
        i2cTransaction.writeBuf = txBuffer;
        i2cTransaction.writeCount = 1;
        i2cTransaction.readBuf = data;
        i2cTransaction.readCount = 1;

        txBuffer[0] = registar;

        if (!I2C_transfer(i2c, &i2cTransaction))
                System_abort("Bad I2C transfer\n");
}

/* === User-defined functions === */
void constructInitTask(){
    Task_Params taskParamsInit;
    Task_Params_init(&taskParamsInit);
    taskParamsInit.stackSize = TASKSTACKSIZE;
    taskParamsInit.stack = &initStack;
    taskParamsInit.priority = 1;
    Task_construct(&initStruct, (Task_FuncPtr)initFxn, &taskParamsInit, NULL);
}

void semaphoreInitialization(){
    Semaphore_Params semaphoreParams;

    Semaphore_Params_init(&semaphoreParams);
    Semaphore_construct(&semMeasureStruct, 0, &semaphoreParams);
    semMeasure = Semaphore_handle(&semMeasureStruct);

    Semaphore_Params_init(&semaphoreParams);
    Semaphore_construct(&semBPMStruct, 0, &semaphoreParams);
    semBPM = Semaphore_handle(&semBPMStruct);

    Semaphore_Params_init(&semaphoreParams);
    Semaphore_construct(&semDiodeStruct, 0, &semaphoreParams);
    semDiode = Semaphore_handle(&semDiodeStruct);
}

void queueInitialization(){
    queueMeasure = Queue_create(NULL, NULL);
    queueBPM = Queue_create(NULL, NULL);
}

/* ======== measureFxn ======== */
void measureFxn(UArg arg0, UArg arg1){
        uint16_t bioVal;
        uint8_t data;
        int n = 0;
        float bioMeasures[SAMPLE_SIZE];
        int i;
        for(i = 0; i < SAMPLE_SIZE; i++)
                bioMeasures[i] = 0;
        float sumBio = 0;
        int ind = 0;
        float bioVal_est;
        BioVal *bio_val;

        while(1) {
                bioVal_est = 0;
                n = 0;

                /* Averaging bio value during T = 50ms */
                do {
                        readRegister(BIO_RES_HIGH, &data);
                        bioVal = data << 8;
                        readRegister(BIO_RES_LOW, &data);
                        bioVal |= data;
                        bioVal_est += bioVal;
                        n++;
                } while(n < 50);

                bioVal_est = bioVal_est / n;

                /* Averaging SAMPLE_SIZE  measurements to get smoother signal */
                sumBio -= bioMeasures[ind];
                sumBio += bioVal_est;
                bioMeasures[ind] = bioVal_est;

                bio_val = (BioVal *)malloc(sizeof(BioVal));
                bio_val->last  = sumBio / SAMPLE_SIZE;
                Queue_put(queueMeasure, (Queue_Elem *)bio_val);
                Semaphore_post(semMeasure);

                ind++;
                ind %= SAMPLE_SIZE;
        }
}

void heartbeatFxn(UArg arg0, UArg arg1){
        Types_FreqHz freq;
        Timestamp_getFreq(&freq);
        long pom = freq.lo / 1000;

        BioVal *bio_val;
        BPM *beats;
        float lastBio = 0;
        float prevBio = 0;
        int riseCount = 0;
        bool rising = false;
        int delta[MEASURE_SIZE];
        int ind = 0;
        long lastbeat;
        int beatsPerMinute = 0;
        int i;

        while(1){
                Semaphore_pend(semMeasure, BIOS_WAIT_FOREVER);
                bio_val = (BioVal *)Queue_get(queueMeasure);
                lastBio = bio_val->last;
                free(bio_val);

                if(lastBio > THRESHOLD_FINGER) {
                    GPIO_write(Board_LED2, Board_LED_OFF);
                        if(lastBio > prevBio) {
                                riseCount++;
                                /* Check if we detected heart beat */
                                if(!rising && riseCount > THRESHOLD) {
                                    rising = true;

                                    /* Counting time difference between last
                                     * beat and current beat
                                     */
                                    delta[ind] = (Timestamp_get32() / pom) -
                                                 lastbeat;
                                    lastbeat = Timestamp_get32() / pom;

                                    /* Take average of time for couple last
                                     * measurements to get better results. Take
                                     * only good measurements, that are not
                                     * floating more then 10%
                                     */
                                    int sumDelta = 0;
                                    int c = 0;
                                    for(i = 1; i < MEASURE_SIZE; i++) {
                                            if((delta[i] <  delta[i-1] * 1.1) &&
                                               (delta[i] >  delta[i-1] / 1.1)) {
                                                    c++;
                                                    sumDelta += delta[i];
                                            }
                                    }

                                    ind++;
                                    ind %= MEASURE_SIZE;

                                    /* Estimated beats per minute */
                                    beatsPerMinute = MINUTE_IN_MS / (sumDelta / c);

                                    if(beatsPerMinute > BPM_LOW_VALUE &&
                                            beatsPerMinute < BPM_HIGH_VALUE) {

                                            beats = (BPM *)malloc(sizeof(BPM));
                                            beats->bpm = beatsPerMinute;
                                            Queue_put(queueBPM,
                                                     (Queue_Elem *)beats);
                                            Semaphore_post(semBPM);
                                    }
                                }
                        } else {
                                riseCount = 0;
                                rising = false;
                        }
                        prevBio = lastBio;
                } else {
                        GPIO_write(Board_LED2, Board_LED_ON);
                        System_printf("Please put your finger on sensor!\n");
                        System_flush();
                }
        }
}

void printFxn(UArg arg0, UArg arg1){
        int isDiodeFxnStarted = 0;
        while(1)
        {
                BPM *beats;
                Semaphore_pend(semBPM, BIOS_WAIT_FOREVER);
                beats = (BPM *)Queue_get(queueBPM);
                beatsPerMinuteDiode = beats->bpm;
                System_printf("BPM: %d\n", beats->bpm);
                System_flush();
                if(isDiodeFxnStarted == 0){
                    Semaphore_post(semDiode);
                    isDiodeFxnStarted = 1;
                }
                free(beats);
        }
}

void diodeFxn(UArg arg0, UArg arg1)
{
    Semaphore_pend(semDiode, BIOS_WAIT_FOREVER);
    while (1) {
           double beatsOld = beatsPerMinuteDiode - beatsPerMinuteDiode%10;
           double sleepTime = 1000 / ((beatsOld / 60) * 2);
           //int sleepingTime = (int)sleepTime;
           Task_sleep((UInt)sleepTime);
           GPIO_toggle(Board_LED1);
       }
}

void boardInitialization(){
    Board_initGeneral();
    Board_initGPIO();
    Board_initUART();
    Board_initI2C();
}

void initializeOximeter(){
    System_printf("Oximeter initialization\n");
    System_flush();
    writeRegister(COMMAND_REGISTER, CMD_MEASUREMENT_DISABLE);
    writeRegister(BIOSENSOR_RATE, BIO_RATE_250);
    writeRegister(BIO_MODULATOR_TIMING, BIO_MOD_SET);
    writeRegister(COMMAND_REGISTER, CMD_MEASUREMENT_ENABLE |
                                    BIO_MEASUREMENT_ENABLE);
}

void initializeMeasureTask(){
    Task_Params taskParamsMeas;
    Task_Params_init(&taskParamsMeas);
    taskParamsMeas.stackSize = 3* TASKSTACKSIZE;
    taskParamsMeas.stack = &measureStack;
    taskParamsMeas.priority = 1;
    Task_construct(&measureStruct, (Task_FuncPtr)measureFxn, &taskParamsMeas, NULL);
}

void initializeHeartBeatCalculationTask(){
    Task_Params taskParamsHeartbeat;
    Task_Params_init(&taskParamsHeartbeat);
    taskParamsHeartbeat.stackSize = 3 * TASKSTACKSIZE;
    taskParamsHeartbeat.stack = &heartbeatStack;
    taskParamsHeartbeat.priority = 3;
    Task_construct(&heartbeatStruct, (Task_FuncPtr)heartbeatFxn, &taskParamsHeartbeat, NULL);
}

void initializePrintTask(){
    Task_Params taskParamsPrint;
    Task_Params_init(&taskParamsPrint);
    taskParamsPrint.stackSize = 1 *TASKSTACKSIZE;
    taskParamsPrint.stack = &printStack;
    taskParamsPrint.priority = 2;
    Task_construct(&printStruct, (Task_FuncPtr)printFxn, &taskParamsPrint, NULL);
}

void initializeDiodeTask(){
    Task_Params taskParamsDiode;
    Task_Params_init(&taskParamsDiode);
    taskParamsDiode.stackSize = 1 *TASKSTACKSIZE;
    taskParamsDiode.stack = &diodeStack;
    taskParamsDiode.priority = 2;
    Task_construct(&diodeStruct, (Task_FuncPtr)diodeFxn, &taskParamsDiode, NULL);
}


Void initFxn(UArg arg0, UArg arg1) {
    initializeOximeter();
    initializeMeasureTask();
    initializeHeartBeatCalculationTask();
    initializePrintTask();
    initializeDiodeTask();
}

/*
 *  ======== main ========
 */
int main(void){

    boardInitialization();

    constructInitTask();

    semaphoreInitialization();

    queueInitialization();

    I2Cinitialization();
    UARTinitialization();


    /* Start BIOS */
    BIOS_start();

    return (0);
}



//int mainOLD(void){
//
//    boardInitialization();
//
//    /* Init task construction */
//    Task_Params initTaskParams;
//
//
//    /* Construct heartBeat Task  thread */
//    Task_Params_init(&taskParams);
//    taskParams.arg0 = 1000;
//    taskParams.stackSize = TASKSTACKSIZE;
//    taskParams.stack = &task0Stack;
//    Task_construct(&task0Struct, (Task_FuncPtr)heartBeatFxn, &taskParams, NULL);
//
//    /* Turn on user LED */
//    GPIO_write(Board_LED0, Board_LED_ON);
//
//    System_printf("Starting the example\nSystem provider is set to SysMin. "
//                  "Halt the target to view any SysMin contents in ROV.\n");
//    /* SysMin will only print to the console when you call flush or exit */
//    System_flush();
//
//    /* Start BIOS */
//    BIOS_start();
//
//    return (0);
//}

//Void heartBeatFxnOLD(UArg arg0, UArg arg1)
//{
//    while (1) {
//        Task_sleep((UInt)arg0);
//        GPIO_toggle(Board_LED0);
//    }
//}
