#pragma once

#include "../../xrCore/_stl_extensions.h"

#include <algorithm>
#include <cmath>

namespace SVGRendering
{
constexpr u32 MaxRasterDimension = 2048;

struct PixelSize
{
	u32 Width = 0;
	u32 Height = 0;

	bool IsValid() const { return Width != 0 && Height != 0; }
	bool operator==(const PixelSize&) const = default;
};

inline PixelSize RasterSize(float Width, float Height, float Scale, u32 Limit)
{
	if (!std::isfinite(Width) || !std::isfinite(Height) || Width <= 0.f || Height <= 0.f)
	{
		return {};
	}
	const double UserScale = std::isfinite(Scale) && Scale > 0.f ? Scale : 1.0;
	double W = static_cast<double>(Width) * UserScale;
	double H = static_cast<double>(Height) * UserScale;
	const double Cap = std::clamp(Limit, 1u, MaxRasterDimension);
	const double Reduction = std::min(1.0, Cap / std::max(W, H));
	W *= Reduction;
	H *= Reduction;
	return {static_cast<u32>(std::clamp(std::ceil(W), 1.0, Cap)), static_cast<u32>(std::clamp(std::ceil(H), 1.0, Cap))};
}

// Resources own their GPU references. Eviction releases only the cache's ownership;
// shaders already copied by widgets keep their textures alive until their last use.
template <typename TKey, typename TValue, typename THash = std::hash<TKey>>
class RasterCache final
{
public:
	struct Entry
	{
		TKey Key;
		TValue Value;
		size_t Cost;
		u64 LastAccess;
	};

	using EntryList = xr_list<Entry>;

	RasterCache(size_t CountLimit, size_t CostLimit) : CountLimit(CountLimit), CostLimit(CostLimit) {}
	RasterCache(const RasterCache&) = delete;
	RasterCache& operator=(const RasterCache&) = delete;

	TValue* Find(const TKey& Key)
	{
		const auto Found = Lookup.find(Key);
		if (Found == Lookup.end())
		{
			return nullptr;
		}
		Entries.splice(Entries.end(), Entries, Found->second);
		Found->second->LastAccess = ++AccessSequence;
		return &Found->second->Value;
	}

	TValue* Insert(TKey Key, TValue Value, size_t Cost)
	{
		if (CountLimit == 0 || Cost > CostLimit)
		{
			return nullptr;
		}
		const auto Existing = Lookup.find(Key);
		if (Existing != Lookup.end())
		{
			Erase(Existing->second);
		}
		while (!Entries.empty() && (Entries.size() >= CountLimit || TotalCost > CostLimit - Cost))
		{
			Erase(Entries.begin());
		}
		Entries.push_back({std::move(Key), std::move(Value), Cost, ++AccessSequence});
		const auto Added = std::prev(Entries.end());
		Lookup.emplace(Added->Key, Added);
		TotalCost += Cost;
		return &Added->Value;
	}

	template <typename TPredicate>
	void EraseIf(TPredicate Predicate)
	{
		for (auto It = Entries.begin(); It != Entries.end();)
		{
			const auto Current = It++;
			if (Predicate(Current->Key))
			{
				Erase(Current);
			}
		}
	}

	void Clear()
	{
		Lookup.clear();
		Entries.clear();
		TotalCost = 0;
	}

	size_t GetCost() const { return TotalCost; }
	size_t GetSize() const { return Entries.size(); }
	const EntryList& GetEntries() const { return Entries; }

private:
	void Erase(typename EntryList::iterator It)
	{
		TotalCost -= It->Cost;
		Lookup.erase(It->Key);
		Entries.erase(It);
	}

	size_t CountLimit;
	size_t CostLimit;
	size_t TotalCost = 0;
	u64 AccessSequence = 0;
	EntryList Entries;
	// xr_hash_map fixes std::hash<TKey>; this cache also supports explicit hashers.
	std::unordered_map<TKey, typename EntryList::iterator, THash, std::equal_to<TKey>, xalloc<std::pair<const TKey, typename EntryList::iterator>>> Lookup;
};
} // namespace SVGRendering
