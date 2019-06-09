#pragma once

#ifndef _CME_BOOK_H_
#define _CME_BOOK_H_

#include <vector>
#include <functional>

static constexpr const int MAX_LEVELS = 10;

struct CmeLevel
{
	int64_t price;
	int quantity;
	int orders;

	CmeLevel()
		: price(0)
	, 	quantity(0)
	, 	orders(0)
	{
	}
};


struct CmeSide
{
	std::vector<CmeLevel> levels;

	void AddLevel(int index, int64_t price, int quantity, int orders)
	{
		if( index >= levels.size() )
		{
			levels.resize(index + 1);
		}

		CmeLevel level;
		level.price = price;
		level.quantity = quantity;
		level.orders = orders;

		levels.insert( levels.begin() + index, level );
		if( levels.size() > MAX_LEVELS )
			levels.pop_back();
	}

	void UpdateLevel(int index, int64_t price, int quantity, int orders)
	{
		if( index >= levels.size() )
			levels.resize(index + 1);

		CmeLevel& level = levels[index];
		level.price = price;
		level.quantity = quantity;
		level.orders = orders;
	}

	void DeleteLevel(int index)
	{
		if(index < levels.size() )
			levels.erase(levels.begin() + index);
	}

	template<typename PriceOp>
	static void CombineSide(CmeSide& target, const CmeSide& outright, const CmeSide& implieds, const PriceOp& op)
	{
		target.levels.clear();

		int l = 0, r = 0;
		for(; (l < outright.levels.size()) && (r < implieds.levels.size()); )
		{
			if( op(outright.levels[l].price, implieds.levels[r].price) )
			{
				target.levels.push_back(outright.levels[l]);
				++l;
			}
			else if(outright.levels[l].price == implieds.levels[r].price)
			{
				CmeLevel newLevel(outright.levels[r]);
				newLevel.quantity += implieds.levels[r].quantity;
				target.levels.push_back(newLevel);
				++l, ++r;
			}
			else
			{
				target.levels.push_back(implieds.levels[r]);
				++r;
			}
		}

		for(; (l < outright.levels.size()); ++l)
			target.levels.push_back(outright.levels[l]);

		for(; (r < implieds.levels.size()); ++r)
			target.levels.push_back(implieds.levels[r]);

	}

	CmeLevel& operator[](int index ) { return levels[index]; }

	CmeLevel* Find(int64_t price)
	{
		for(CmeLevel& level : levels)
		{
			if( level.price == price )
				return &level;
		}

		return 0;
	}
};

struct CmeBook
{
	CmeSide bids;
	CmeSide impliedBids;
	CmeSide asks;
	CmeSide impliedAsks;

	CmeSide combinedBids
		,   combinedAsks
		;

	void Combine()
	{
		combinedBids.levels.clear();
		combinedAsks.levels.clear();
	}
};

#endif // _CME_BOOK_H_
