#pragma once

class CUIPropertiesBox;
class CInventory;
class CInventoryOwner;
class CInventoryItem;

struct UseActionContext
{
	CInventory* Inventory = nullptr;
	CInventoryOwner* Owner = nullptr;
	CInventoryItem* Item = nullptr;
};

class IInventoryUseActions
{
public:
	virtual ~IInventoryUseActions() = default;

	virtual bool FillUseActions(
		CUIPropertiesBox* Box,
		const UseActionContext& Context
	) = 0;
};
