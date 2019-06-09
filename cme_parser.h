#pragma once

#ifndef _CME_PARSER_H_
#define _CME_PARSER_H_

#include <stdint.h>

#define PACKED __attribute__((packed))

static constexpr const char LAST_TRADE = 0x01;
static constexpr const char LAST_VOLUME = 0x02;
static constexpr const char LAST_QUOTE = 0x04;
static constexpr const char LAST_STATS = 0x08;
static constexpr const char LAST_IMPLIED = 0x10;
static constexpr const char LAST_MSG = 0x80;


struct CmeMsgHeader
{
    uint32_t seq_num;
    uint64_t send_time;
} PACKED;

struct CmeMessage
{
    uint16_t msg_length;
    uint16_t block_length;
    uint16_t template_id;
    uint16_t schema_id;
    uint16_t version_id;
} PACKED;

struct CmeBookRefresh
{
    uint64_t transact_time;
    char indicator;

	char padding[2];
    
    uint16_t entry_size;
    uint8_t num_in_group;
} PACKED;

struct CmeBookEntry
{
    int64_t price;
    int32_t size;
    int32_t sec_id;
    uint32_t rpt_seq_num;
    int32_t num_orders;
    uint8_t price_level;
	uint8_t action_type;
    char entry_type;
} PACKED;

struct CmeOrderRefresh
{
    uint64_t transact_time;
    char indicator;
    
    uint16_t entry_size;
    uint8_t num_in_group;
} PACKED;

struct CmeBookOrderEntry
{
    uint64_t order_id;
    uint64_t priority;
    int64_t price;
    int32_t qty;    
    int32_t sec_id;
    char update_action;
    char entry_type;
} PACKED;

struct GroupSize
{
    uint16_t entry_size;
    uint8_t num_in_group;
} PACKED;

struct GroupSize8Bytes
{
	uint16_t entry_size;
	char padding[5];
	uint8_t num_in_group;
} PACKED;

struct CmeTradeSummary
{
    uint64_t transact_time;
    char indicator;

	char padding[2];

    uint16_t entry_size;
    uint8_t num_in_group;
} PACKED;

struct CmeTradeEntry
{
    int64_t price;
    int32_t qty;
    int32_t sec_id;
    uint32_t rpt_seq;
    int32_t num_orders;
    char aggressor_side;
    char update_action;
    char entry_type;
    uint32_t entry_id;
} PACKED;

struct CmeOrderEntry
{
    uint64_t order_id;
    int32_t qty;
	int32_t padding;
} PACKED;

template<typename T>
const T* pop_as(const char*& ptr, size_t size = sizeof(T))
{
    const T* ret = (const T*)ptr;
    ptr += size;
    return ret;

}
#endif // _CME_PARSER_H_
