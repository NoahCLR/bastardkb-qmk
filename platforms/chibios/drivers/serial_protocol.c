// Copyright 2022 Stefan Kerkmann
// SPDX-License-Identifier: GPL-2.0-or-later

#include <ch.h>

#include "serial.h"
#include "serial_protocol.h"
#include "synchronization_util.h"

static inline bool initiate_transaction(uint8_t transaction_id);
static inline bool react_to_transaction(void);

#ifdef SPLIT_TRANSPORT_CRC
#    include <string.h>
#    include "crc.h"

/* Every data frame carries a trailing CRC8 over the transaction id and its
 * bytes, so a garbled frame is refused and cannot pass as another
 * transaction's. The receiver stages the frame and copies it into shared
 * memory only once it checks.
 *
 * CRC builds answer the handshake with SPLIT_CRC_HANDSHAKE_BIT set, so a half
 * without the CRC fails at the handshake. The slave sets SPLIT_CRC_DROP_BIT
 * in its answer when the previous transaction's write failed its CRC; the
 * master credits the drop to its last write. No valid id reaches either
 * bit. The drop bit itself is unchecked: a bit error that sets it only
 * causes one unneeded resend. */
#    define SPLIT_CRC_HANDSHAKE_BIT 0x40
#    define SPLIT_CRC_DROP_BIT 0x80
_Static_assert(NUM_TOTAL_TRANSACTIONS <= 0x20, "split handshake bits overlap transaction ids");

/* One frame at a time on either half: [id, payload, crc8]. transactions.c
 * checks every table entry against SPLIT_TRANSPORT_CRC_MAX_FRAME at compile
 * time; the runtime check covers RPC lengths, which arrive over the wire. */
static uint8_t crc_frame[1 + SPLIT_TRANSPORT_CRC_MAX_FRAME + 1];

/* Checksum polls carry no CRC: their content is a checksum of the data read
 * that follows a change, and that read carries one. */
static inline bool frame_has_crc(uint8_t transaction_id) {
    switch (transaction_id) {
        case GET_SLAVE_MATRIX_CHECKSUM:
#    ifdef ENCODER_ENABLE
        case GET_ENCODERS_CHECKSUM:
#    endif
#    if defined(POINTING_DEVICE_ENABLE) && defined(SPLIT_POINTING_ENABLE)
        case GET_POINTING_CHECKSUM:
#    endif
            return false;
        default:
            return true;
    }
}
#else
#    define SPLIT_CRC_HANDSHAKE_BIT 0
#    define SPLIT_CRC_DROP_BIT 0

static inline bool frame_has_crc(uint8_t transaction_id) {
    (void)transaction_id;
    return false;
}
#endif

typedef enum { FRAME_OK, FRAME_FAILED, FRAME_BAD_CRC } frame_result_t;

static bool send_frame(uint8_t transaction_id, const uint8_t* source, size_t size) {
#ifdef SPLIT_TRANSPORT_CRC
    if (frame_has_crc(transaction_id)) {
        if (unlikely(size > SPLIT_TRANSPORT_CRC_MAX_FRAME)) {
            return false;
        }
        crc_frame[0] = transaction_id;
        memcpy(&crc_frame[1], source, size);
        crc_frame[1 + size] = crc8(crc_frame, 1 + size);
        return serial_transport_send(&crc_frame[1], size + 1);
    }
#endif
    return serial_transport_send(source, size);
}

static frame_result_t receive_frame(uint8_t transaction_id, uint8_t* destination, size_t size) {
#ifdef SPLIT_TRANSPORT_CRC
    if (frame_has_crc(transaction_id)) {
        crc_frame[0] = transaction_id;
        if (unlikely(size > SPLIT_TRANSPORT_CRC_MAX_FRAME || !serial_transport_receive(&crc_frame[1], size + 1))) {
            return FRAME_FAILED;
        }
        if (unlikely(crc8(crc_frame, 1 + size) != crc_frame[1 + size])) {
            return FRAME_BAD_CRC;
        }
        memcpy(destination, &crc_frame[1], size);
        return FRAME_OK;
    }
#endif
    return serial_transport_receive(destination, size) ? FRAME_OK : FRAME_FAILED;
}

static inline uint8_t wire_size(uint8_t transaction_id, uint8_t size) {
    return size && frame_has_crc(transaction_id) ? size + 1 : size;
}

/**
 * @brief This thread runs on the slave and responds to transactions initiated
 * by the master.
 */
static THD_WORKING_AREA(waSlaveThread, 1024);
static THD_FUNCTION(SlaveThread, arg) {
    (void)arg;
    chRegSetThreadName("split_protocol_tx_rx");

    while (true) {
        if (unlikely(!react_to_transaction())) {
            /* Clear the receive queue, to start with a clean slate.
             * Parts of failed transactions or spurious bytes could still be in it. */
            serial_transport_driver_clear();
        }
    }
}

/**
 * @brief Slave specific initializations.
 */
void soft_serial_target_init(void) {
    serial_transport_driver_slave_init();

    /* Start transport thread. */
    chThdCreateStatic(waSlaveThread, sizeof(waSlaveThread), HIGHPRIO, SlaveThread, NULL);
}

/**
 * @brief Master specific initializations.
 */
void soft_serial_initiator_init(void) {
    serial_transport_driver_master_init();
}

/**
 * @brief React to transactions started by the master.
 */
static inline bool react_to_transaction(void) {
    uint8_t transaction_id = 0;
    /* Wait until there is a transaction for us. */
    if (unlikely(!serial_transport_receive_blocking(&transaction_id, sizeof(transaction_id)))) {
        return false;
    }

    /* Sanity check that we are actually responding to a valid transaction. */
    if (unlikely(transaction_id >= NUM_TOTAL_TRANSACTIONS)) {
        return false;
    }

    split_shared_memory_lock_autounlock();

    split_transaction_desc_t* transaction = &split_transaction_table[transaction_id];

    /* Only the slave thread touches this: whether the previous write failed
     * its CRC. */
    static bool previous_write_dropped = false;

    /* Send back the handshake which is XORed as a simple checksum,
     to signal that the slave is ready to receive possible transaction buffers.
     With the frame CRC it also reports whether the previous write was dropped. */
    uint8_t handshake      = transaction_id ^ NUM_TOTAL_TRANSACTIONS ^ SPLIT_CRC_HANDSHAKE_BIT ^ (previous_write_dropped ? SPLIT_CRC_DROP_BIT : 0);
    previous_write_dropped = false;
    if (unlikely(!serial_transport_send(&handshake, sizeof(handshake)))) {
        return false;
    }

    /* Receive transaction buffer from the master. If this transaction requires it.
     * A frame that fails its CRC leaves shared memory untouched and skips the
     * callback. It arrived whole, so the link is still in step: returning true
     * keeps the receive queue, which may already hold the master's next
     * transaction id. A transaction that also returns data would then time out
     * on the master; none does. */
    if (transaction->initiator2target_buffer_size) {
        frame_result_t result = receive_frame(transaction_id, split_trans_initiator2target_buffer(transaction), transaction->initiator2target_buffer_size);
        if (unlikely(result != FRAME_OK)) {
            previous_write_dropped = result == FRAME_BAD_CRC;
            return result == FRAME_BAD_CRC;
        }
    }

    /* Allow any slave processing to occur. */
    if (transaction->slave_callback) {
        transaction->slave_callback(transaction->initiator2target_buffer_size, split_trans_initiator2target_buffer(transaction), transaction->initiator2target_buffer_size, split_trans_target2initiator_buffer(transaction));
    }

    /* Send transaction buffer to the master. If this transaction requires it. */
    if (transaction->target2initiator_buffer_size) {
        if (unlikely(!send_frame(transaction_id, split_trans_target2initiator_buffer(transaction), transaction->target2initiator_buffer_size))) {
            return false;
        }
    }

    return true;
}

/**
 * @brief Start transaction from the master half to the slave half.
 *
 * @param index Transaction Table index of the transaction to start.
 * @return bool Indicates success of transaction.
 */
bool soft_serial_transaction(int index) {
    /* Clear the receive queue, to start with a clean slate.
     * Parts of failed transactions or spurious bytes could still be in it. */
    serial_transport_driver_clear();

#ifdef SPLIT_TRANSACTION_DIAGNOSTICS
    if (index < 0 || index >= NUM_TOTAL_TRANSACTIONS) return false;
    uint32_t started = chSysGetRealtimeCounterX();
    bool success = initiate_transaction((uint8_t)index);
    split_transaction_desc_t *trans = &split_transaction_table[index];
    split_transaction_diagnostic((uint8_t)index, wire_size((uint8_t)index, trans->initiator2target_buffer_size), wire_size((uint8_t)index, trans->target2initiator_buffer_size),
                                 chSysGetRealtimeCounterX() - started, success);
    return success;
#else
    return initiate_transaction((uint8_t)index);
#endif
}

/**
 * @brief Initiate transaction to slave half.
 */
static inline bool initiate_transaction(uint8_t transaction_id) {
    /* Sanity check that we are actually starting a valid transaction. */
    if (unlikely(transaction_id >= NUM_TOTAL_TRANSACTIONS)) {
        serial_dprintf("SPLIT: illegal transaction id\n");
        return false;
    }

    split_shared_memory_lock_autounlock();

    split_transaction_desc_t* transaction = &split_transaction_table[transaction_id];

    /* Send transaction table index to the slave, which doubles as basic handshake token. */
    if (unlikely(!serial_transport_send(&transaction_id, sizeof(transaction_id)))) {
        serial_dprintf("SPLIT: sending handshake failed\n");
        return false;
    }

    /* Only the master's main loop touches this: the last write the slave
     * handshook, as id + 1 (0 for none), which a drop report in the next
     * handshake refers to. */
    static uint8_t last_write = 0;
    uint8_t        dropped    = last_write;
    last_write                = 0;

    uint8_t transaction_id_shake = 0xFF;

    /* Which we always read back first so that we can error out correctly.
     *   - due to the half duplex limitations on return codes, we always have to read *something*.
     *   - without the read, write only transactions *always* succeed, even during the boot process where the slave is not ready.
     */
    if (unlikely(!serial_transport_receive(&transaction_id_shake, sizeof(transaction_id_shake)) || ((transaction_id_shake & (uint8_t)~SPLIT_CRC_DROP_BIT) != (transaction_id ^ NUM_TOTAL_TRANSACTIONS ^ SPLIT_CRC_HANDSHAKE_BIT)))) {
        serial_dprintf("SPLIT: receiving handshake failed\n");
        return false;
    }
#ifdef SPLIT_TRANSPORT_CRC
    if ((transaction_id_shake & SPLIT_CRC_DROP_BIT) && dropped) {
        split_transaction_crc_dropped(dropped - 1);
    }
#else
    (void)dropped;
#endif

    /* Send transaction buffer to the slave. If this transaction requires it. */
    if (transaction->initiator2target_buffer_size) {
        if (unlikely(!send_frame(transaction_id, split_trans_initiator2target_buffer(transaction), transaction->initiator2target_buffer_size))) {
            serial_dprintf("SPLIT: sending buffer failed\n");
            return false;
        }
        if (frame_has_crc(transaction_id)) {
            last_write = transaction_id + 1;
        }
    }

    /* Receive transaction buffer from the slave. If this transaction requires it. */
    if (transaction->target2initiator_buffer_size) {
        frame_result_t result = receive_frame(transaction_id, split_trans_target2initiator_buffer(transaction), transaction->target2initiator_buffer_size);
        if (unlikely(result != FRAME_OK)) {
#if defined(SPLIT_TRANSPORT_CRC) && defined(SPLIT_TRANSACTION_DIAGNOSTICS)
            if (result == FRAME_BAD_CRC) {
                split_transaction_diagnostic_crc(transaction_id);
            }
#endif
            serial_dprintf("SPLIT: receiving buffer failed\n");
            return false;
        }
    }

    return true;
}
