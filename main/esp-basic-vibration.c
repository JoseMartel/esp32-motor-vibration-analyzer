/**
 * @file esp-basic-vibration.c
 * @brief Main - Frequency analyzer sensor 
 * @author Jose Martel
 * @date 30/06/2023
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include "hal/gpio_types.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_system.h"

#include "esp_dsp.h"
#include "lis3dh.h"
#include "vibration.h"
#include "i2c-mb.h"

/* Vibration Analysis Parameters */
#define samples             1024 
#define samplingFrequency   1620
#define ampFactor           3.7067873
#define GPIO_INT_DATA_RDY   26
#define GPIO_INT_SHOCK      27

#define detectLen           1024
#define detectWindowLen     16

static const char *TAG = "esp-basic-vibration";

/* Handle */
QueueHandle_t dataQueue;
SemaphoreHandle_t readSemaphore; 
TaskHandle_t vibrationHandle;
TaskHandle_t adquisitionHandle;
TaskHandle_t detectionHandle;

void DetectionTask(void *pvParameter)
{   
    float vRMS[1024];
    float recievedData;
    float normalMean;
    float normalDeviation;
    
    while (1) {

        if(!getEnLearning()){

            for (uint16_t i = 0; i < detectLen; i++) {             
                printf("[#] Detection Task recieved %d sample \n", i);
                xQueueReceive(dataQueue, &recievedData, portMAX_DELAY);
                vRMS[i] = recievedData;
            }

            /* Training finish */
            mean(vRMS, detectLen, &normalMean);
            deviation(vRMS, detectLen, normalMean, &normalDeviation);
            setEnLearning(1);
            printf(" [#] Training finish \n");
            printf(" mean : %.2f \n", normalMean);
            printf(" derivation : %.2f \n", normalDeviation);
        
        } else {
            
            float _mean;

            for (uint16_t i = 0; i < detectWindowLen; i++) {             

                xQueueReceive(dataQueue, &recievedData, portMAX_DELAY);
                vRMS[i] = recievedData;

            }

            /* Evaluate */
            mean(vRMS, detectWindowLen, &_mean);

            if( _mean > (normalMean + 2*normalDeviation) || _mean < (normalMean - 2*normalDeviation)){
                printf(" [#] Detect anomalie | mean : %.2f \n", _mean);
                setAnBehavior(1);
            } else {
                setAnBehavior(0);
            }

            /* Logs */
            printf("============ NORMAL VALUES ============\n");
            printf(" normal mean : %.2f \n", normalMean);
            printf(" normal derivation : %.2f \n", normalDeviation);
            printf(" current mean : %.2f \n", _mean);

        }
    }
}

void VibrationTask(void *pvParameter)
{
    printf("============== Starting Vibration Analysis Task ==============\n");

    // Variables 
    AxesAccel_t    ac;
    static float   ax[samples];
    static float   ay[samples];
    static float   az[samples];
    static float   ve[samples];
    float          Img[samples];

    float          aRMS = 0;
    float          vRMS = 0;
    Peaks_t        peak[5];

    Vibration_t    vibrationData;
    Parameters_t   parametersData;

    ParameterDefault(&parametersData);

    /* Variables */
    uint8_t value = 0x02;
    bool sts = 0;
    unsigned int counter = 0;
    unsigned long long t1, t2 = 0;
    unsigned long long t3, t4 = 0;

    // Peripherals Init
    ESP_ERROR_CHECK(I2C_MASTER_INIT());
    ESP_LOGI(TAG, "I2C initialized successfully");

    // Accelerometer Config
    LIS3DH_SetInt1Pin(LIS3DH_I1_DRDY1_ON_INT1_ENABLE);
    LIS3DH_SetODR(LIS3DH_ODR_1620Hz_LP);
    LIS3DH_SetMode(LIS3DH_LOW_POWER);
    LIS3DH_SetFullScale(LIS3DH_FULLSCALE_4);
    LIS3DH_SetAxis(LIS3DH_X_ENABLE | LIS3DH_Y_ENABLE | LIS3DH_Z_ENABLE);

    // Config Internal High Pass Filter
    LIS3DH_SetMode(LIS3DH_HPM_NORMAL_MODE);
    LIS3DH_SetHPFCutOFF(LIS3DH_HPFCF_3);
    LIS3DH_SetFilterDataSel(0x00);

    sts = LIS3DH_GetWHO_AM_I(&value);
    printf("Read Status: %d\n", sts);
    printf("Who I am : %d\n", value);

    LIS3DH_GetAccAxes(&ac);
    vTaskDelay(1000);

    while (true) 
    {          
        // Get acceleration data in mg and save each axis
        t3 = esp_timer_get_time();
        LIS3DH_GetAccLP(&ac);
        az[counter] = ac.axis_x;
        ay[counter] = ac.axis_y;
        ax[counter] = ac.axis_z;
        t4 = esp_timer_get_time();   

        /* Wait interrupt to read sensor */
        xSemaphoreTake(readSemaphore, 1);

        if (counter >= samples - 1)
        {   
            printf("\n============== Running Vibration Analysis Task ==============\n");
            printf("> Counter value : %d\n", counter);

            /* Verify sensor conection */
            setStatus(LIS3DH_GetWHO_AM_I(&value));

            /* Determine mean time to read each sample */
            printf("> Read mean time : %lld us\n", (t4 - t3));

            /* Determine mean time to get each sample */
            t1 = esp_timer_get_time();
            printf("> Adquisition mean time : %lld us\n", (t1 - t2)/1024);

            // Reset counter   
            counter = 0;

            // Remove DC Component
            removeOffset(ax, samples);
            removeOffset(ay, samples);
            removeOffset(az, samples);

            // Get Acceleration resultant 3 axis
            resVector(ax, ay, az, samples);

            // Get velocity applying numerical integration
            Velocity(ax, ve, samples, 1.0/samplingFrequency);
            // simpson13(ax, ve, samples, samplingFrequency);
            // rungeKutta(ax, ve, samples, samplingFrequency);

            // Get Vibration RMS Values
            RMS_Value(ax, samples, &aRMS);
            RMS_Value(ve, samples, &vRMS);

            // Save vector data on vReal and vImg for FFT
            memset(Img, 0, samples * sizeof(float));

            // FFT
            FFT_Init(ve, Img, samples, samplingFrequency);
            FFT_Windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD); 
            FFT_Compute(FFT_FORWARD);
            FFT_ComplexToMagnitude();

            // Find Peaks
            FFT_MajorPeaks(&peak[0], 1, 5);

            // Update Parameters
            getParameters(&parametersData);

            // Save Data
            vibrationData.aRMS = aRMS;
            vibrationData.vRMS = vRMS;
            vibrationData.Peak1 = peak[0];
            vibrationData.Peak2 = peak[1];
            vibrationData.Peak3 = peak[2];
            vibAnalysis(&vibrationData, &parametersData);
            saveVibration(&vibrationData);

            // Sent to detection task
            if(vibrationData.MotorState)
                xQueueSend(dataQueue, &vRMS, 1);

            // =========== DEBUG LOG ================
            printf("[#] Ac RMS : %.2f mg.\n", aRMS);
            printf("[#] Ve RMS : %.2f mm/s.\n", vRMS);
            printf("[#] 1st Peak -> Freq: %.2f Hz - Amp: %.2f mm/s \n", peak[0].freq, peak[0].amp);
            printf("[#] 2nd Peak -> Freq: %.2f Hz - Amp: %.2f mm/s \n", peak[1].freq, peak[1].amp);
            printf("[#] 3rd Peak -> Freq: %.2f Hz - Amp: %.2f mm/s \n", peak[2].freq, peak[2].amp);
            printf("[#] 4th Peak -> Freq: %.2f Hz - Amp: %.2f mm/s \n", peak[3].freq, peak[3].amp);
            printf("[#] 5th Peak -> Freq: %.2f Hz - Amp: %.2f mm/s \n", peak[4].freq, peak[4].amp);
            printf("[#] Speed : %d RPM.\n", vibrationData.Speed);
            printf("[#] Anomalie : %d \n", vibrationData.Anomalie);
            printf("[#] Motor State : %d \n", vibrationData.MotorState);
            printf("[#] Sensor State : %d \n", vibrationData.SensorState);

            // for (uint i = 920; i < 1025; i += 10) {
            //     printf("[RAW] %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f \n",
            //                 az[i], az[i+1], az[i+2], az[i+3], az[i+4], az[i+5], az[i+6], az[i+7], az[i+8],  az[i+9]);
            //     }

            // for (uint i = 0; i < 1025; i += 10) {
            //     printf("[RAW][%d]  %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f \n", i/10,
            //                 ax[i], ax[i+1], ax[i+2], ax[i+3], ax[i+4], ax[i+5], ax[i+6], ax[i+7], ax[i+8],  ax[i+9]);
            //     }

            // printf("SPEED DATA ===========================================\n");

            // for (uint i = 0; i < 1025; i += 10) {
            //     printf("[RAW][%d] %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f \n", i/10,
            //                 ve[i], ve[i+1], ve[i+2], ve[i+3], ve[i+4], ve[i+5], ve[i+6], ve[i+7], ve[i+8],  ve[i+9]);
            //     }

            vTaskDelay(15000);
            t2 = esp_timer_get_time();
        }
        else 
            counter++;
    }
}

void IRAM_ATTR isr_handler(void *arg)
{   
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(readSemaphore, &xHigherPriorityTaskWoken); // Se despierta la tarea que espera por el semáforo
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR(); // Se cambia a una tarea de mayor prioridad
    }
}

void configure_interrupt(void)
{
    gpio_config_t gpio_cfg = {
        .intr_type = GPIO_INTR_POSEDGE, // Configurar el modo de interrupción como flanco de subida
        .mode = GPIO_MODE_INPUT, // Configurar el modo de pin como entrada
        .pin_bit_mask = (1ULL << GPIO_INT_DATA_RDY) // Configurar el pin que se utilizará como entrada
    };
    gpio_config(&gpio_cfg);

    /* Configurar la rutina de interrupción */ 
    gpio_install_isr_service(ESP_INTR_FLAG_LEVEL3); // Nivel de prioridad de interrupción
    gpio_isr_handler_add(GPIO_INT_DATA_RDY, isr_handler, NULL); // Configurar el manejador de interrupción
}

void app_main(void)
{
    configure_interrupt();
    readSemaphore = xSemaphoreCreateBinary(); // Se crea el semáforo
    dataQueue = xQueueCreate(8, sizeof(float));
    xTaskCreate(&VibrationTask, "xVibrationTask", 24576, NULL, 3, &vibrationHandle);
    xTaskCreatePinnedToCore(&i2c_ReadingTask,"xReadingTask",1024 * 4,NULL,6,&adquisitionHandle,0);
    xTaskCreate(&DetectionTask, "xDetectionTask", 1024 * 8, NULL, 4, &detectionHandle);
}
