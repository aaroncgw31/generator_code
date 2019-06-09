#pragma once

#ifndef _SECURITY_INFO_H_
#define _SECURITY_INFO_H_

#include "cme_book.h"
#include <map>
#include <utility>

/*
#include "iceberg_detector.h"
#include "stops_detector.h"
#include "sweeps_detector.h"
*/

struct Trade
{
	int64_t price;
	int quantity;

	Trade()
		: price(0)
		, quantity(0)
	{
	}
};

struct StopsTrade
{
	int64_t exchange_time;

	uint64_t order_id;
	uint32_t size;
	uint32_t traded_size;
	int64_t start_price;
	int64_t highest_price;

	bool is_buy;
};

struct Iceberg
{
	int64_t ts;
	int64_t price;
	int total_traded;
	int show_quantity;

	bool is_bid;

	std::map<uint64_t, int> order_ids;
};

struct bid_side
{
	typedef std::greater<int64_t> PriceCmp;
};

struct ask_side
{
	typedef std::less<int64_t> PriceCmp;
};

template<typename SideType>
struct IcebergInfo
{
	CmeSide implieds;
	CmeSide outrights;

	CmeLevel prevTopLevel;

	Trade highestTrade;

	std::vector<Iceberg> icebergs;
	std::map<int64_t, Iceberg, typename SideType::PriceCmp> open_icebergs;

	bool in_iceberg;
	bool is_buy;

	IcebergInfo(bool is_buy)
		: in_iceberg(false)
		, is_buy(is_buy)
	{
	}

	bool CheckIceberg(int64_t ts, Iceberg* currentIceberg)
	{
		bool is_iceberg = (highestTrade.quantity != 0)
						& (highestTrade.price == prevTopLevel.price)
						& (highestTrade.quantity >= prevTopLevel.quantity)
						&  ((!outrights.levels.empty())
						&& (outrights.levels[0].price == prevTopLevel.price))
			;

		auto found = open_icebergs.find(highestTrade.price);
		if( found != open_icebergs.end() )
		{
		}

		for(auto iter = open_icebergs.begin(); iter != open_icebergs.lower_bound(outrights.levels[0].price);)
		{
			icebergs.push_back(iter->second);
			iter = open_icebergs.erase(iter);
		}

		bool new_iceberg = false;
		if( is_iceberg )
		{
			auto found = open_icebergs.find(outrights.levels[0].price);
			if( found == open_icebergs.end() )
			{
				Iceberg iceberg;
				iceberg.ts = ts;
				iceberg.show_quantity = outrights.levels[0].quantity;
				iceberg.price = outrights.levels[0].price;
				iceberg.total_traded = highestTrade.quantity - (prevTopLevel.quantity - outrights.levels[0].quantity);
				iceberg.is_bid = is_buy;

				open_icebergs.insert( std::make_pair(iceberg.price, iceberg) );
				if( currentIceberg )
					*currentIceberg = iceberg;
				new_iceberg = true;
			}
			else
			{
				Iceberg& iceberg = found->second; 
				iceberg.show_quantity = std::min(outrights.levels[0].quantity, iceberg.show_quantity);
				iceberg.total_traded += highestTrade.quantity - (prevTopLevel.quantity - outrights.levels[0].quantity);
				if( currentIceberg )
					*currentIceberg = iceberg;
			}
		}

		return is_iceberg;
	}

	void ClearTrade()
	{
		highestTrade.quantity = 0;
	}

	void AddTrade(int64_t price, int quantity, bool isbuy)
	{
		if( highestTrade.quantity == 0 || ( (isbuy && price > highestTrade.price) || (!isbuy && price < highestTrade.price) ) )
		{
			highestTrade.price = price;
			highestTrade.quantity = quantity;

			for(int i = 0; i < outrights.levels.size(); ++i)
			{
				if( outrights.levels[i].price == price )
				{
					prevTopLevel = outrights.levels[i];
				}
			}
		}
	}
};

struct StopsInfo
{
	int64_t ts;

	int64_t first_price;
	std::vector<StopsTrade> trades;	
};

struct SweepInfo
{
	int64_t exchangeTime;
	int64_t startTime;

	int64_t startPrice;
	int64_t endPrice;

	int totalVolume;
	bool isBuy;

	bool firstAggressor;
	bool ignoreTrades;

	int64_t priceDivider;
	int64_t tickSize;
	int secId;

	int minDepth;

	SweepInfo()
	{
		Clear();
	}

	void Clear()
	{
		exchangeTime = 0;
		startTime = 0;

		startPrice = 0;
		endPrice = 0;

		totalVolume = 0;
		isBuy = false;

		firstAggressor = true;
		ignoreTrades = false;
	}
};


struct SecurityInfo
{
	CmeBook book;
	bool dirty;
	int32_t sec_id;
	std::string symbol;

	// Stops
	StopsInfo stops_info;
	std::vector<StopsInfo> all_stops;

	// Icebergs
	IcebergInfo<bid_side> buy_icebergs;
	IcebergInfo<ask_side> sell_icebergs;

	SweepInfo sweep_info;

	int64_t tick_size;
	int64_t price_shift;
	bool traded_locally;
	bool inside_change;

	int64_t CleanPrice(int64_t packet_price)
	{
		return packet_price /= price_shift;
	}

	SecurityInfo()
		: dirty(false)
		, traded_locally(false)
		, inside_change(false)
		, buy_icebergs(true)
		, sell_icebergs(false)
	{
	}
};

#endif // _SECURITY_INFO_H_
