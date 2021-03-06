#include "app_sniffer.h"
#include "nrf.h"
#include "nrf_error.h"
#include "nrf_esb.h"
#include "nrf_esb_error_codes.h"

#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

static uint32_t esb_default_init(void);

static app_sniffer_callback_t   m_callback = 0;
static app_sniffer_event_t      m_event;
nrf_esb_payload_t rx_payload;

static bool                     m_esb_initialized = false;
static uint32_t                 m_timer_overflow_counter;

static void reset_timestamp_timer(void)
{
    SNF_RX_TIMESTAMP_TIMER->TASKS_STOP = 1;
    SNF_RX_TIMESTAMP_TIMER->TASKS_CLEAR = 1;
    m_timer_overflow_counter = 0;
    SNF_RX_TIMESTAMP_TIMER->TASKS_START = 1;
}

uint32_t app_sniffer_init(app_sniffer_config_t *config)
{
    uint32_t err_code = NRF_SUCCESS;
    
    m_callback = config->event_handler;

    SNF_RX_TIMESTAMP_TIMER->TASKS_STOP = 1;
    SNF_RX_TIMESTAMP_TIMER->PRESCALER = 4;
    SNF_RX_TIMESTAMP_TIMER->BITMODE = TIMER_BITMODE_BITMODE_32Bit;
    SNF_RX_TIMESTAMP_TIMER->CC[0] = SNF_RX_TIMESTAMP_RELOAD_INTERVAL;
    SNF_RX_TIMESTAMP_TIMER->INTENSET = TIMER_INTENSET_COMPARE0_Msk;
    
    NRF_PPI->CH[SNF_PPI_RX_TIMESTAMP_CH].EEP = (uint32_t)&NRF_RADIO->EVENTS_ADDRESS;
    NRF_PPI->CH[SNF_PPI_RX_TIMESTAMP_CH].TEP = (uint32_t)&SNF_RX_TIMESTAMP_TIMER->TASKS_CAPTURE[1];
    NRF_PPI->CHENSET = 1 << SNF_PPI_RX_TIMESTAMP_CH;
    
    NVIC_SetPriority(SNF_RX_TIMESTAMP_TIMER_IRQn, 2);
    NVIC_EnableIRQ(SNF_RX_TIMESTAMP_TIMER_IRQn);

    err_code = esb_default_init();
    return err_code;
}

static void nrf_esb_event_handler(nrf_esb_evt_t const * p_event)
{
    switch (p_event->evt_id)
    {
        case NRF_ESB_EVENT_TX_SUCCESS:
            NRF_LOG_DEBUG("TX SUCCESS EVENT");
            break;
        case NRF_ESB_EVENT_TX_FAILED:
            NRF_LOG_DEBUG("TX FAILED EVENT");
            break;
        case NRF_ESB_EVENT_RX_RECEIVED:
            NRF_LOG_DEBUG("RX RECEIVED EVENT");
            if (nrf_esb_read_rx_payload(&rx_payload) == NRF_SUCCESS)
            {
                NRF_LOG_DEBUG("Receiving packet: %02x", rx_payload.data[1]);
                m_event.type = APP_SNIFFER_EVT_TYPE_RX_PACKET_RECEIVED;
                m_event.time_stamp = m_timer_overflow_counter * SNF_RX_TIMESTAMP_RELOAD_INTERVAL + SNF_RX_TIMESTAMP_TIMER->CC[1];
                m_event.rf_payload = &rx_payload;
                if(m_callback) m_callback(&m_event);
            }
            break;
    }
}

static uint32_t esb_default_init(void)
{
    uint32_t err_code;
    uint8_t base_addr_0[4] = {0xE7, 0xE7, 0xE7, 0xE7};
    uint8_t base_addr_1[4] = {0xC2, 0xC2, 0xC2, 0xC2};
    uint8_t addr_prefix[8] = {0xE7, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8 };
    nrf_esb_config_t nrf_esb_config         = NRF_ESB_DEFAULT_CONFIG;
    nrf_esb_config.payload_length           = 8;
    nrf_esb_config.protocol                 = NRF_ESB_PROTOCOL_ESB_DPL;
    nrf_esb_config.bitrate                  = NRF_ESB_BITRATE_2MBPS;
    nrf_esb_config.mode                     = NRF_ESB_MODE_PRX;
    nrf_esb_config.event_handler            = nrf_esb_event_handler;
    nrf_esb_config.selective_auto_ack       = false;

    err_code = nrf_esb_init(&nrf_esb_config);
    VERIFY_SUCCESS(err_code);

    err_code = nrf_esb_set_base_address_0(base_addr_0);
    VERIFY_SUCCESS(err_code);

    err_code = nrf_esb_set_base_address_1(base_addr_1);
    VERIFY_SUCCESS(err_code);

    err_code = nrf_esb_set_prefixes(addr_prefix, 8);
    VERIFY_SUCCESS(err_code);

    m_esb_initialized = true;

    return err_code;
}

uint32_t app_sniffer_configure(snf_trans_sniffer_configuration_t *config)
{
    uint32_t err_code;
    config->esb_config.mode                 = NRF_ESB_MODE_PRX;
    config->esb_config.event_handler        = nrf_esb_event_handler;
    config->esb_config.selective_auto_ack   = false;
    config->esb_config.radio_irq_priority   = 1;
    config->esb_config.event_irq_priority   = 2; 
    config->esb_config.retransmit_delay     = 250;
    config->esb_config.retransmit_count     = 1;

    if(m_esb_initialized)
    {
        err_code = nrf_esb_disable();
        VERIFY_SUCCESS(err_code);
    }

    err_code = nrf_esb_init(&config->esb_config);
    VERIFY_SUCCESS(err_code);

    err_code = nrf_esb_set_rf_channel(config->rf_channel);
    VERIFY_SUCCESS(err_code);

    err_code = nrf_esb_set_base_address_0(&config->address[1]);
    VERIFY_SUCCESS(err_code);

    err_code = nrf_esb_set_prefixes(&config->address[0], 1);
    VERIFY_SUCCESS(err_code);

    m_esb_initialized = true;

    return err_code;
}

uint32_t app_sniffer_start_rx(void)
{
    reset_timestamp_timer();
    uint32_t err_code = nrf_esb_start_rx();
    return err_code;
}

uint32_t app_sniffer_stop_rx(void)
{
    uint32_t err_code = nrf_esb_stop_rx();
    return err_code;
}

void SNF_RX_TIMESTAMP_TIMER_IRQHandler(void)
{
    if(SNF_RX_TIMESTAMP_TIMER->EVENTS_COMPARE[0])
    {
        SNF_RX_TIMESTAMP_TIMER->EVENTS_COMPARE[0] = 0;

        m_timer_overflow_counter++;
    }
}