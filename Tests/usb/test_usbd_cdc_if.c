#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "usbd_cdc_if.h"

USBD_HandleTypeDef hUsbDeviceHS;
static USBD_CDC_HandleTypeDef cdc;
static uint32_t interrupt_mask;
static uint32_t submitted_packets;
static uint32_t received_packets;
static uint8_t set_tx_result;
static uint8_t transmit_result;
static uint8_t disconnect_before_lock;

uint32_t __get_PRIMASK(void)
{
  return interrupt_mask;
}

void __disable_irq(void)
{
  /* Model a disconnect interrupt immediately before the lock takes effect. */
  if ((interrupt_mask == 0U) && (disconnect_before_lock != 0U))
  {
    hUsbDeviceHS.pClassDataCmsit[0] = NULL;
    disconnect_before_lock = 0U;
  }
  interrupt_mask = 1U;
}

void __set_PRIMASK(uint32_t mask)
{
  interrupt_mask = mask;
}

uint8_t USBD_CDC_SetTxBuffer(USBD_HandleTypeDef *device, uint8_t *buffer, uint32_t length)
{
  USBD_CDC_HandleTypeDef *handle = device->pClassDataCmsit[device->classId];
  if ((handle == NULL) || (set_tx_result != USBD_OK))
  {
    return USBD_FAIL;
  }
  handle->TxBuffer = buffer;
  handle->TxLength = length;
  return USBD_OK;
}

uint8_t USBD_CDC_SetRxBuffer(USBD_HandleTypeDef *device, uint8_t *buffer)
{
  USBD_CDC_HandleTypeDef *handle = device->pClassDataCmsit[device->classId];
  if (handle == NULL)
  {
    return USBD_FAIL;
  }
  handle->RxBuffer = buffer;
  return USBD_OK;
}

uint8_t USBD_CDC_TransmitPacket(USBD_HandleTypeDef *device)
{
  USBD_CDC_HandleTypeDef *handle = device->pClassDataCmsit[device->classId];
  assert(interrupt_mask == 1U);
  assert(handle != NULL);
  assert(handle->TxState == 0U);
  if (transmit_result != USBD_OK)
  {
    return transmit_result;
  }
  handle->TxState = 1U;
  submitted_packets++;
  return USBD_OK;
}

uint8_t USBD_CDC_ReceivePacket(USBD_HandleTypeDef *device)
{
  assert(device->pClassDataCmsit[device->classId] != NULL);
  assert(cdc.RxBuffer != NULL);
  received_packets++;
  return USBD_OK;
}

static void reconnect(void)
{
  memset(&cdc, 0, sizeof(cdc));
  memset(&hUsbDeviceHS, 0, sizeof(hUsbDeviceHS));
  hUsbDeviceHS.pClassDataCmsit[0] = &cdc;
  hUsbDeviceHS.dev_state = USBD_STATE_CONFIGURED;
  interrupt_mask = 0U;
  set_tx_result = USBD_OK;
  transmit_result = USBD_OK;
  disconnect_before_lock = 0U;
  submitted_packets = 0U;
  received_packets = 0U;
  assert(USBD_Interface_fops_HS.Init() == USBD_OK);
}

static void test_transfer_ownership_and_backpressure(void)
{
  uint8_t payload[APP_TX_DATA_SIZE];
  uint8_t next[] = {1U, 2U, 3U};
  reconnect();
  memset(payload, 0xA5, sizeof(payload));
  assert(CDC_Transmit_HS(payload, sizeof(payload)) == USBD_OK);
  assert(interrupt_mask == 0U);
  assert(cdc.TxBuffer != payload);
  assert(cdc.TxLength == sizeof(payload));
  memset(payload, 0xFF, sizeof(payload));
  for (size_t i = 0U; i < sizeof(payload); i++)
  {
    assert(cdc.TxBuffer[i] == 0xA5U);
  }

  assert(CDC_Transmit_HS(next, sizeof(next)) == USBD_BUSY);
  assert(submitted_packets == 1U);
  assert(cdc.TxLength == sizeof(payload));
  assert(cdc.TxBuffer[0] == 0xA5U);
  assert(interrupt_mask == 0U);

  cdc.TxState = 0U;
  assert(CDC_Transmit_HS(next, sizeof(next)) == USBD_OK);
  assert(submitted_packets == 2U);
  assert(memcmp(cdc.TxBuffer, next, sizeof(next)) == 0);
}

static void test_invalid_and_disconnected_transfers(void)
{
  uint8_t payload[APP_TX_DATA_SIZE + 1U] = {0U};
  reconnect();
  assert(CDC_Transmit_HS(NULL, 1U) == USBD_FAIL);
  assert(CDC_Transmit_HS(payload, sizeof(payload)) == USBD_FAIL);
  assert(submitted_packets == 0U);
  assert(cdc.TxState == 0U);

  hUsbDeviceHS.pClassDataCmsit[0] = NULL;
  assert(CDC_Transmit_HS(payload, 1U) == USBD_FAIL);
  assert(interrupt_mask == 0U);
  hUsbDeviceHS.pClassDataCmsit[0] = &cdc;
  hUsbDeviceHS.dev_state = USBD_STATE_DEFAULT;
  assert(CDC_Transmit_HS(payload, 1U) == USBD_FAIL);
  hUsbDeviceHS.dev_state = USBD_STATE_SUSPENDED;
  assert(CDC_Transmit_HS(payload, 1U) == USBD_FAIL);

  reconnect();
  disconnect_before_lock = 1U;
  assert(CDC_Transmit_HS(payload, 1U) == USBD_FAIL);
  assert(interrupt_mask == 0U);
  assert(submitted_packets == 0U);

  reconnect();
  assert(CDC_Transmit_HS(NULL, 0U) == USBD_OK);
  assert(cdc.TxLength == 0U);
  assert(submitted_packets == 1U);
}

static void test_interrupt_mask_and_submission_failures(void)
{
  uint8_t payload[] = {0x7FU};
  reconnect();
  interrupt_mask = 1U;
  set_tx_result = USBD_FAIL;
  assert(CDC_Transmit_HS(payload, sizeof(payload)) == USBD_FAIL);
  assert(interrupt_mask == 1U);
  assert(submitted_packets == 0U);
  set_tx_result = USBD_OK;
  transmit_result = USBD_FAIL;
  assert(CDC_Transmit_HS(payload, sizeof(payload)) == USBD_FAIL);
  assert(interrupt_mask == 1U);
  assert(submitted_packets == 0U);
  transmit_result = USBD_OK;
  assert(CDC_Transmit_HS(payload, sizeof(payload)) == USBD_OK);
  assert(interrupt_mask == 1U);
  assert(CDC_Transmit_HS(payload, sizeof(payload)) == USBD_BUSY);
  assert(interrupt_mask == 1U);
  hUsbDeviceHS.pClassDataCmsit[0] = NULL;
  assert(CDC_Transmit_HS(payload, sizeof(payload)) == USBD_FAIL);
  assert(interrupt_mask == 1U);
}

static void test_line_coding_and_receive(void)
{
  uint8_t default_coding[] = {0x00U, 0xC2U, 0x01U, 0x00U, 0U, 0U, 8U};
  uint8_t new_coding[] = {0x80U, 0x25U, 0x00U, 0x00U, 2U, 2U, 7U};
  uint8_t coding[8] = {0U};
  uint8_t *rx_buffer;
  reconnect();
  assert(USBD_Interface_fops_HS.Control(CDC_GET_LINE_CODING, coding, 7U) == USBD_OK);
  assert(memcmp(coding, default_coding, 7U) == 0);
  assert(USBD_Interface_fops_HS.Control(CDC_SET_LINE_CODING, new_coding, 7U) == USBD_OK);
  memset(coding, 0xFF, sizeof(coding));
  assert(USBD_Interface_fops_HS.Control(CDC_GET_LINE_CODING, coding, 8U) == USBD_OK);
  assert(memcmp(coding, new_coding, 7U) == 0);
  assert(coding[7] == 0xFFU);
  assert(USBD_Interface_fops_HS.Control(CDC_SET_LINE_CODING, default_coding, 6U) == USBD_FAIL);
  assert(USBD_Interface_fops_HS.Control(CDC_SET_LINE_CODING, NULL, 7U) == USBD_FAIL);
  assert(USBD_Interface_fops_HS.Control(CDC_GET_LINE_CODING, NULL, 7U) == USBD_FAIL);
  memset(coding, 0xFF, sizeof(coding));
  assert(USBD_Interface_fops_HS.Control(CDC_GET_LINE_CODING, coding, 3U) == USBD_OK);
  assert(memcmp(coding, new_coding, 3U) == 0);
  assert(coding[3] == 0xFFU);

  rx_buffer = cdc.RxBuffer;
  assert(USBD_Interface_fops_HS.Receive(NULL, NULL) == USBD_OK);
  assert(cdc.RxBuffer == rx_buffer);
  assert(received_packets == 1U);
  hUsbDeviceHS.pClassDataCmsit[0] = NULL;
  assert(USBD_Interface_fops_HS.Receive(NULL, NULL) == USBD_FAIL);
  assert(received_packets == 1U);
}

int main(void)
{
  test_transfer_ownership_and_backpressure();
  test_invalid_and_disconnected_transfers();
  test_interrupt_mask_and_submission_failures();
  test_line_coding_and_receive();
  puts("USB CDC tests passed");
  return 0;
}
