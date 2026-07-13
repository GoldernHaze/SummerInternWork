#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "payload_chunk.h"
#include "ngham.h"

QueueHandle_t xQueue1_RawChunks;
QueueHandle_t xQueue2_OrderedChunks;

typedef struct {
    uint16_t occultation_id;
    uint16_t data_size;
} FakeEvent_t;

static const FakeEvent_t fake_events[] = {
    { .occultation_id = 1000, .data_size = 100 },
    { .occultation_id = 1001, .data_size = 45  },
    { .occultation_id = 1002, .data_size = 70  },
};

#define NUM_FAKE_EVENTS (sizeof(fake_events) / sizeof(fake_events[0]))

/* ---------- TASK 1: Payload Reader / Fragmenter ---------- */
void vTask1_PayloadReader(void *pvParameters)
{
    (void) pvParameters;

    for (int e = 0; e < (int)NUM_FAKE_EVENTS; e++)
    {
        uint16_t occ_id   = fake_events[e].occultation_id;
        uint16_t data_len = fake_events[e].data_size;

        uint8_t fake_payload[256];
        for (int i = 0; i < data_len; i++)
        {
            fake_payload[i] = (uint8_t)(0x10 + (e * 0x30) + i);
        }

        uint8_t total_chunks = (data_len + CHUNK_DATA_SIZE - 1) / CHUNK_DATA_SIZE;

        printf("[Task1-PayloadReader] occultation %d: %d bytes -> %d chunk(s)\n",
               occ_id, data_len, total_chunks);
        fflush(stdout);

        int bytes_remaining = data_len;
        int offset = 0;

        for (uint8_t seq = 1; seq <= total_chunks; seq++)
        {
            PayloadChunk_t chunk;
            memset(&chunk, 0, sizeof(chunk));

            uint8_t this_chunk_size = (bytes_remaining < CHUNK_DATA_SIZE)
                                        ? (uint8_t)bytes_remaining
                                        : CHUNK_DATA_SIZE;

            chunk.occultation_id  = occ_id;
            chunk.sequence_number = seq;
            chunk.total_chunks    = total_chunks;
            chunk.data_size       = this_chunk_size;
            memcpy(chunk.data, &fake_payload[offset], this_chunk_size);

            xQueueSend(xQueue1_RawChunks, &chunk, portMAX_DELAY);

            offset          += this_chunk_size;
            bytes_remaining -= this_chunk_size;

            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }

    printf("[Task1-PayloadReader] all occultations fragmented. Task finished.\n");
    fflush(stdout);

    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}

/* ---------- TASK 2: FIFO Queue Manager ---------- */
void vTask2_FifoManager(void *pvParameters)
{
    (void) pvParameters;
    PayloadChunk_t chunk;
    int total_forwarded = 0;

    printf("[Task2-FifoManager] started.\n");
    fflush(stdout);

    for (;;)
    {
        if (xQueueReceive(xQueue1_RawChunks, &chunk, pdMS_TO_TICKS(3000)) == pdTRUE)
        {
            total_forwarded++;
            printf("[Task2-FifoManager] forwarding  occ=%d  seq=%d/%d  bytes=%d\n",
                   chunk.occultation_id, chunk.sequence_number,
                   chunk.total_chunks, chunk.data_size);
            fflush(stdout);

            xQueueSend(xQueue2_OrderedChunks, &chunk, portMAX_DELAY);
        }
        else
        {
            printf("[Task2-FifoManager] no more chunks arriving. Total forwarded: %d\n",
                   total_forwarded);
            fflush(stdout);
            for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

/* ---------- TASK 3: NGHam Encoder ---------- */
void vTask3_NGHamEncoder(void *pvParameters)
{
    (void) pvParameters;
    PayloadChunk_t chunk;
    int total_encoded = 0;

    printf("[Task3-NGHamEncoder] started.\n");
    fflush(stdout);

    for (;;)
    {
        if (xQueueReceive(xQueue2_OrderedChunks, &chunk, pdMS_TO_TICKS(3000)) == pdTRUE)
        {
            total_encoded++;

            /* Serialize chunk metadata + data into the NGHam payload */
            uint8_t ngham_payload[64];
            int p = 0;
            ngham_payload[p++] = (uint8_t)(chunk.occultation_id >> 8);
            ngham_payload[p++] = (uint8_t)(chunk.occultation_id & 0xFF);
            ngham_payload[p++] = chunk.sequence_number;
            ngham_payload[p++] = chunk.total_chunks;
            ngham_payload[p++] = chunk.data_size;
            memcpy(&ngham_payload[p], chunk.data, chunk.data_size);
            p += chunk.data_size;

            uint8_t ngham_packet[300];
            int packet_len = ngham_build_packet(ngham_payload, (uint8_t)p, ngham_packet);

            if (packet_len < 0)
            {
                printf("[Task3-NGHamEncoder] ERROR: payload too large to encode!\n");
                fflush(stdout);
                continue;
            }

            printf("[Task3-NGHamEncoder] occ=%d seq=%d/%d -> NGHam packet (%d bytes): ",
                   chunk.occultation_id, chunk.sequence_number, chunk.total_chunks, packet_len);
            for (int i = 0; i < packet_len; i++) printf("%02X ", ngham_packet[i]);
            printf("\n");
            fflush(stdout);
        }
        else
        {
            printf("[Task3-NGHamEncoder] no more chunks arriving. Total encoded: %d\n",
                   total_encoded);
            fflush(stdout);
            for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

int main(void)
{
    printf("=== CubeSat Packet Pipeline - Task 1 + 2 + 3 Test ===\n\n");
    fflush(stdout);

    xQueue1_RawChunks     = xQueueCreate(10, sizeof(PayloadChunk_t));
    xQueue2_OrderedChunks = xQueueCreate(10, sizeof(PayloadChunk_t));

    xTaskCreate(vTask1_PayloadReader,  "Task1", configMINIMAL_STACK_SIZE * 2, NULL, 1, NULL);
    xTaskCreate(vTask2_FifoManager,    "Task2", configMINIMAL_STACK_SIZE * 2, NULL, 1, NULL);
    xTaskCreate(vTask3_NGHamEncoder,   "Task3", configMINIMAL_STACK_SIZE * 4, NULL, 1, NULL);

    vTaskStartScheduler();

    for (;;);
    return 0;
}

void vAssertCalled(const char *pcFile, unsigned long ulLine)
{
    printf("ASSERT FAILED: %s, line %lu\n", pcFile, ulLine);
    fflush(stdout);
    for (;;);
}

void vApplicationMallocFailedHook(void)
{
    printf("MALLOC FAILED! Out of heap memory.\n");
    fflush(stdout);
    for (;;);
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void) xTask;
    printf("STACK OVERFLOW in task: %s\n", pcTaskName);
    fflush(stdout);
    for (;;);
}