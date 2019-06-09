#include <net/ethernet.h>
#include <netinet/ip.h>
#include <netinet/udp.h>

#include "cme_parser.h"

#include <stdio.h>
#include <time.h>

#include <iostream>
#include <fstream>
#include <functional>
#include <algorithm>
#include <map>

#include "cme_book.h"
#include "security_info.h"

static constexpr const char* SWEEPS_HEADERS = "ts,symbol,start_price,end_price,total_traded,aggr_side";
static constexpr const char* ICEBERGS_HEADERS = "ts,symbol,price,show_size,traded_size,side";
static constexpr const char* STOPS_HEADERS = "ts,exchange_ts,symbol,order_id,trigger_price,order_size,traded_size,side";

struct PcapFileHeader
{
    uint32_t magic_number;
    uint16_t version_major;
    uint16_t version_minor;
    int32_t gmt_correction;
    uint32_t sigfigs;
    uint32_t snaplen;
    uint32_t network;
} PACKED;

struct PcapPacketHeader
{
    uint32_t ts_sec;
    uint32_t ts_nsec;
    uint32_t incl_len;
    uint32_t orig_len;
} PACKED;

struct ErfPacketHeader
{
    uint32_t ts_nanos;
    uint32_t ts_seconds;
    char type;
    char flags;
    uint16_t rlen;
    uint16_t color;
    uint16_t wlen;
} PACKED;

struct IpHeader
{
    ether_header eth;
    iphdr ip;
    udphdr udp;
} PACKED;

using namespace std;

std::ofstream sweeps_file;
std::ofstream icebergs_file;
std::ofstream stops_file;

struct SymbolInfo
{
	std::string symbol;
	int64_t tick_size;
	int64_t price_shift;
};

std::map<int, SymbolInfo> symbol_map;
std::map<int, SecurityInfo*> info_map;

std::vector<SecurityInfo*> packet_infos;

void LoadSecInfo()
{
	std::ifstream id_file("cme_ids.txt");

	string line;
	while(getline(id_file, line))
	{
		string::size_type symbol_idx = line.find(',');
		string::size_type exchange_id_idx = line.find(',', symbol_idx + 1);
		string::size_type tick_size_idx = line.find(',', exchange_id_idx + 1);

		if( symbol_idx == string::npos || exchange_id_idx == string::npos || tick_size_idx == string::npos )
			continue;

		int exchange_id = stoi(line.substr(symbol_idx + 1, exchange_id_idx - symbol_idx));
		int64_t price_shift = stoll(line.substr(exchange_id_idx + 1, tick_size_idx - exchange_id_idx));
		int64_t tick_size = stoll(line.substr(tick_size_idx + 1));

		SymbolInfo info;
		info.symbol = line.substr(0, symbol_idx);
		info.tick_size = tick_size;
		info.price_shift = price_shift;

		symbol_map.insert( make_pair(exchange_id, info) );
	}
}

SecurityInfo* GetInfo(int32_t sec_id)
{
	auto found = info_map.find(sec_id);
	if( found == info_map.end() )
	{
		auto symbol_found = symbol_map.find(sec_id);
		if( symbol_found == symbol_map.end() )
			return 0;
		SecurityInfo* new_info = new SecurityInfo();
		new_info->tick_size = symbol_found->second.tick_size;
		new_info->price_shift = symbol_found->second.price_shift;
		new_info->symbol = symbol_found->second.symbol;
		new_info->sec_id = sec_id;

		found = info_map.insert( make_pair(sec_id, new_info) ).first;
	}

	if( !found->second->dirty )
	{
		found->second->dirty = true;
		packet_infos.push_back(found->second);
	}

	return found->second;
}

std::string time_to_str(int64_t ts)
{
	int64_t seconds = ts/1000000000LL;
	char timeBuffer[256];

	tm t;
	localtime_r(&seconds, &t);

	strftime(timeBuffer, sizeof(timeBuffer), "%Y-%m-%d %H:%M:%S", &t);

	char ret[256];
	snprintf(ret, sizeof(ret), "%s.%09lld", timeBuffer, ts % 1000000000LL);
	return ret;
}

void CmeSideUpdate(CmeSide& side, const CmeBookEntry* entry, const char* sideName)
{
	switch(entry->action_type)
	{
	case 0: side.AddLevel(entry->price_level - 1, entry->price, entry->size, entry->num_orders); break;
	case 1: side.UpdateLevel(entry->price_level - 1, entry->price, entry->size, entry->num_orders); break;
	case 2: side.DeleteLevel(entry->price_level - 1); break;
	case 3: 
		for(int i = 0; i < entry->price_level; ++i)
		{
			side.DeleteLevel(0);
		}
		break;
	case 4:
		for(int i = entry->price_level - 1; i < side.levels.size(); ++i)
		{
			side.DeleteLevel(entry->price_level);
		}
		break;
	default:
		break;
	}
}

char parse_32(int64_t ts, const char* buffer)
{
    const CmeBookRefresh* refresh = pop_as<CmeBookRefresh>(buffer);

    for(uint8_t i = 0; i < refresh->num_in_group; ++i)
    {
        const CmeBookEntry* entry = pop_as<CmeBookEntry>(buffer, refresh->entry_size);

		SecurityInfo* sec_info = GetInfo(entry->sec_id);
		if( !sec_info )
			continue;

		const char* action = "";
		switch(entry->action_type)
		{
		case 0: action = "Add"; break;
		case 1: action = "Update"; break;
		case 2: action = "Delete"; break;
		case 3: action = "DeleteThru"; break;
		case 4: action = "DeleteFrom"; break;
		default: break;
		}

		const char* side = "";
		switch(entry->entry_type)
		{
		case '0': 
			CmeSideUpdate(sec_info->buy_icebergs.outrights, entry, "Bid");
			side = "Bid";
			break;
		case '1':
			CmeSideUpdate(sec_info->sell_icebergs.outrights, entry, "Ask");
			side = "Ask";
			break;
		case 'E':
			CmeSideUpdate(sec_info->buy_icebergs.implieds, entry, "Bid");
			side = "ImpliedBid";
			break;
		case 'F':
			CmeSideUpdate(sec_info->sell_icebergs.implieds, entry, "Ask");
			side = "ImpliedAsk";
			break;
		default:
			break;
		} 

		sec_info->inside_change |= entry->price_level == 1;
		if( entry->action_type == 0 && sec_info->stops_info.trades.size() > 1 )
		{
			for(int i = 0; i < sec_info->stops_info.trades.size(); ++i)
			{
				if( entry->price == sec_info->stops_info.trades[i].highest_price )
				{
					const StopsTrade& trade = sec_info->stops_info.trades[i];
					if( (trade.is_buy && entry->entry_type == '0')
					 || (!trade.is_buy && entry->entry_type == '1')
					 )
					{
						sec_info->stops_info.trades[i].size += entry->size;
						break;
					}
				}
			}
		}
    }

	return refresh->indicator;

}

void print_sweep(std::ostream& out, const std::string& symbol, const SweepInfo& info)
{
	sweeps_file  << time_to_str(info.startTime)
		 << ',' << symbol
		 << ',' << info.startPrice
		 << ',' << info.endPrice
		 << ',' << info.totalVolume
		 << ',' << info.isBuy
		 << endl;

}

char parse_42(int64_t packetTs, const char* buffer)
{
	const CmeTradeSummary* refresh = pop_as<CmeTradeSummary>(buffer);

	bool securityFound = false;
	bool tradedLocally = false;
	bool is_buy = false;

	int64_t firstPrice = 0;
	int64_t lastPrice = 0;

	for(uint8_t i = 0; i < refresh->num_in_group; ++i)
	{
		const CmeTradeEntry* entry = pop_as<CmeTradeEntry>(buffer, refresh->entry_size);
		SecurityInfo* sec_info = GetInfo(entry->sec_id);

		if( !sec_info )
			continue;

		securityFound = true;

		const char* aggressor;
		switch(entry->aggressor_side)
		{
		case 0: aggressor = "None"; break;
		case 1: aggressor = "Buy"; break;
		case 2: aggressor = "Sell"; break;
		default: aggressor = "Uknown"; break;
		}

		/*
		cout << time_to_str(packetTs) 
			 << " Trade" 
			 << " " << sec_info->symbol
			 << " price:" << sec_info->CleanPrice(entry->price)
			 << " qty:" << entry->qty 
			 << " aggr_side:" << aggressor
			 << endl;
			 */

		sec_info->inside_change = true;

		if( entry->aggressor_side == 0 )
		{
			sec_info->sweep_info.ignoreTrades = true;
		}

		sec_info->traded_locally = true;
		int64_t price = sec_info->CleanPrice(entry->price);

		if( sec_info->sweep_info.firstAggressor )
		{
			sec_info->sweep_info.startTime = packetTs;
			sec_info->sweep_info.exchangeTime = refresh->transact_time;
			sec_info->sweep_info.startTime = packetTs;
			sec_info->sweep_info.startPrice = price;
			sec_info->sweep_info.firstAggressor = false;
			sec_info->sweep_info.isBuy = entry->aggressor_side == 1;
		}

		if( sec_info->stops_info.first_price == 0 )
			sec_info->stops_info.first_price = price;

		sec_info->sweep_info.totalVolume += entry->qty;
		sec_info->sweep_info.endPrice = price;

		switch(entry->aggressor_side)
		{
		case 1:
			sec_info->sell_icebergs.AddTrade(entry->price, entry->qty, true);
			is_buy = true;
			break;
		case 2:
			sec_info->buy_icebergs.AddTrade(entry->price, entry->qty, false);
			is_buy = false;
			break;
		}
		lastPrice = sec_info->CleanPrice(entry->price);
	}

	if( !packet_infos.empty() )
	{
		SecurityInfo* sec_info = packet_infos.back();
		const GroupSize8Bytes* numOrders = pop_as<GroupSize8Bytes>(buffer);

		int32_t total_qty = 0;

		uint32_t order_total = 0;
		if( sec_info->traded_locally )
		{
			for(int i = 0; i < numOrders->num_in_group; ++i)
			{
				const CmeOrderEntry* orders = pop_as<CmeOrderEntry>(buffer);
				/*
				if( i == 0 )
					cout << "\tAggressor id " << orders->order_id << " " << orders->qty << "\n";
				else
					cout << "\tPassive id " << orders->order_id << " " << orders->qty << "\n";
					*/
				if( orders->qty > order_total )
				{
					if( sec_info->stops_info.trades.empty() || (
								sec_info->stops_info.trades.back().order_id != orders->order_id
							&&  sec_info->stops_info.trades[0].order_id > orders->order_id
							)
								)
					{
						if( sec_info->stops_info.trades.empty() )
						{
							sec_info->stops_info.ts = packetTs;
						}

						sec_info->stops_info.trades.emplace_back();
						StopsTrade& trade = sec_info->stops_info.trades.back();

						trade.start_price = sec_info->stops_info.first_price;
						trade.order_id = orders->order_id;
						trade.size = 0;
						trade.traded_size = 0;
					}

					StopsTrade& stops_trade = sec_info->stops_info.trades.back();
					stops_trade.exchange_time = refresh->transact_time;
					stops_trade.size += orders->qty;
					stops_trade.traded_size += orders->qty;
					stops_trade.is_buy = is_buy;
					stops_trade.highest_price = lastPrice;

					order_total = orders->qty;
				}
				else
				{
					order_total -= orders->qty;
				}

				total_qty += orders->qty;
			}
		}
	}

	return refresh->indicator;
}

char parse_43(int64_t ts, const char* buffer)
{
	return 0;
}

void parse_packet(int64_t pktts, const char* buffer, int length)
{
    const IpHeader* pkt_header = (const IpHeader*)buffer;

    if( pkt_header->eth.ether_type != 8 )
        return;

    buffer += sizeof(IpHeader);
    const char* buffer_end = buffer + be16toh(pkt_header->udp.len) - sizeof(pkt_header->udp);

    const CmeMsgHeader* msg_header = (const CmeMsgHeader*)buffer;
    buffer += sizeof(*msg_header);

    const CmeMessage* msg = (const CmeMessage*)buffer;

    for(; buffer < buffer_end; buffer += msg->msg_length, msg = (const CmeMessage*)buffer)
    {
		char indicator = 0;
        switch(msg->template_id)
        {
        case 32: indicator = parse_32(pktts, buffer + sizeof(*msg)); break;
        case 42: indicator = parse_42(pktts, buffer + sizeof(*msg)); break;
        case 43: indicator = parse_43(pktts, buffer + sizeof(*msg)); break;
        case 12: break;
        default: break;
        }

		if( indicator & LAST_TRADE )
		{
			for(SecurityInfo* sec_info : packet_infos)
			{
				if( (sec_info->sweep_info.isBuy && sec_info->sweep_info.endPrice - sec_info->sweep_info.startPrice > sec_info->sweep_info.minDepth)
						||  (!sec_info->sweep_info.isBuy && sec_info->sweep_info.startPrice - sec_info->sweep_info.endPrice > sec_info->sweep_info.minDepth) )
				{
					print_sweep(sweeps_file, sec_info->symbol, sec_info->sweep_info);
				}

				sec_info->sweep_info.Clear();

				if( sec_info->stops_info.trades.size() > 1 )
				{
					sec_info->all_stops.push_back(sec_info->stops_info);
				}
				sec_info->stops_info.trades.clear();
				sec_info->stops_info.ts = 0;
			}
			//cout << "END OF TRADES\n";
		}

		if( indicator & LAST_QUOTE )
		{
			bool using_quote = false;
			for(SecurityInfo* sec_info : packet_infos)
			{
				Iceberg sell_iceberg, buy_iceberg;
				bool is_sell_iceberg = sec_info->sell_icebergs.CheckIceberg(pktts, &sell_iceberg);
				bool is_buy_iceberg = sec_info->buy_icebergs.CheckIceberg(pktts, &buy_iceberg);

				if( (sec_info->inside_change || is_sell_iceberg || is_buy_iceberg)
				 && !sec_info->buy_icebergs.outrights.levels.empty()
				 && !sec_info->sell_icebergs.outrights.levels.empty()
				
				 )
				{
					/*
					cout << time_to_str(pktts) << " Book update bids " << sec_info->CleanPrice(sec_info->buy_icebergs.outrights[0].price)
										   << ":" << sec_info->buy_icebergs.outrights[0].quantity
										   << " x offers " << sec_info->CleanPrice(sec_info->sell_icebergs.outrights[0].price)
										   << ":" << sec_info->sell_icebergs.outrights[0].quantity
										   << "\n";
										   */
				}

				using_quote |= sec_info->inside_change;
				sec_info->inside_change = false;

				if( is_sell_iceberg )
				{
					cout << time_to_str(pktts) << " SELL ICEBERG ==> ";
					cout << "price:" << sec_info->CleanPrice(sell_iceberg.price) << " show_size:" << sell_iceberg.show_quantity << " total_traded:" << sell_iceberg.total_traded << endl;
				}

				if( is_buy_iceberg )
				{
					cout << time_to_str(pktts) << " BUY ICEBERG ==> ";
					cout << "price:" << sec_info->CleanPrice(buy_iceberg.price) << " show_size:" << buy_iceberg.show_quantity << " total_traded:" << buy_iceberg.total_traded << endl;
				}

				sec_info->sell_icebergs.ClearTrade();
				sec_info->buy_icebergs.ClearTrade();
			}

			/*
			if( using_quote )
				cout << "END OF QUOTES\n";
				*/
		}

		if( indicator & LAST_MSG )
		{

			for(SecurityInfo* info : packet_infos)
				info->dirty = false;
			packet_infos.clear();
		}
    }
}

int main(int, char** argv)
{
	LoadSecInfo();

	sweeps_file.open(argv[2]);
	icebergs_file.open(argv[3]);
	stops_file.open(argv[4]);

	sweeps_file << SWEEPS_HEADERS << "\n";
	icebergs_file << ICEBERGS_HEADERS << "\n";
	stops_file << STOPS_HEADERS << "\n";

    FILE* f = fopen(argv[1], "rb");

    ErfPacketHeader pkt_header;
    int ret = fread(&pkt_header, sizeof(pkt_header), 1, f);

    while(ret == 1)
    {
        int packet_length = be16toh(pkt_header.rlen) - sizeof(pkt_header);
        char packet[2048];
        int ret = fread(&packet, packet_length, 1, f);
        if( ret < 1 )
            break;
		int64_t ts = ((int64_t)pkt_header.ts_seconds * 1000000000LL) + (int64_t)pkt_header.ts_nanos;;
        parse_packet(ts, packet + 2, packet_length - 2);
        ret = fread(&pkt_header, sizeof(pkt_header), 1, f);
    }

	for(auto it : info_map)
	{
		for(const StopsInfo& stop : it.second->all_stops)
		{
			for(int i = 1; i < stop.trades.size(); ++i)
			{
				const StopsTrade& trade = stop.trades[i];
				stops_file << time_to_str(stop.ts)
						   << ',' << time_to_str(trade.exchange_time)
						   << ',' << it.second->symbol
						   << ',' << trade.order_id
						   << ',' << stop.trades[0].start_price
						   << ',' << trade.highest_price
						   << ',' << trade.size
						   << ',' << trade.traded_size
						   << ',' << trade.is_buy ? 'B' : 'S'
						   << '\n';
			}
		}

		std::vector<Iceberg> icebergs(it.second->buy_icebergs.icebergs);

		icebergs.insert(icebergs.end(), it.second->sell_icebergs.icebergs.begin(), it.second->sell_icebergs.icebergs.end());

		std::sort(icebergs.begin(), icebergs.end(), [](const Iceberg& lhs, const Iceberg& rhs){ return lhs.ts < rhs.ts; });
		for(const Iceberg& iceberg : icebergs)
		{
			if( iceberg.total_traded > iceberg.show_quantity )
			{
				icebergs_file << time_to_str(iceberg.ts)
							  << ',' << it.second->symbol
							  << ',' << it.second->CleanPrice(iceberg.price)
							  << ',' << iceberg.show_quantity
							  << ',' << iceberg.total_traded
							  << ',' << iceberg.is_bid ? 'B' : 'S'
							  << '\n'
							  ;
			}
		}
	}

    return 0;
}
