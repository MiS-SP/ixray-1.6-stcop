#include "StdAfx.h"
#include "PowerCell.h"
#include "Inventory.h"
#include "InventoryOwner.h"
#include "IPowerManager.h"
#include "PowerBank.h"
#include "UICellItem.h"
#include "../xrEngine/string_table.h"

void PowerCell::Load(const char* section)
{
    CInventoryItemObject::Load(section);

    m_power_cell_data.section = section;
    m_power_cell_data.max_power = READ_IF_EXISTS(pSettings, r_float, section, "power_cell_max_energy_value", 0.0f);
    m_power_cell_data.current_power = READ_IF_EXISTS(pSettings, r_float, section, "power_cell_current_energy_value", 0.0f);
    m_flags.set(FUsingCondition, true);
}

void PowerCell::save(NET_Packet& output_packet)
{
    CInventoryItemObject::save(output_packet);

    output_packet.w_stringZ(m_power_cell_data.section);
    output_packet.w_float(m_power_cell_data.max_power);
    output_packet.w_float(m_power_cell_data.current_power);
}

void PowerCell::load(IReader& input_packet)
{
    CInventoryItemObject::load(input_packet);

    input_packet.r_stringZ(m_power_cell_data.section);
    m_power_cell_data.max_power = input_packet.r_float();
    m_power_cell_data.current_power = input_packet.r_float();
}

bool PowerCell::FillUseActions(
	CUIPropertiesBox* Box,
	const UseActionContext& Context
)
{
	const static bool IsPowerCellContextMenuEnabled =
		EngineExternal()[EEngineExternalGame::EnablePowerCellContextMenu];

	if (!IsPowerCellContextMenuEnabled ||
		Box == nullptr ||
		Context.Inventory == nullptr ||
		Context.Owner == nullptr ||
		Context.Item == nullptr ||
		Context.Item->parent_id() != Context.Owner->object_id())
	{
		return false;
	}

	bool HasAddedActions = false;
	xr_map<shared_str, PIItem> TargetsBySection;
	const PowerCellData PowerCellInfo = GetPowerCellData();

	for (PIItem Target : Context.Inventory->m_all)
	{
		if (Target == nullptr ||
			Target == Context.Item ||
			Target->parent_id() != Context.Owner->object_id())
		{
			continue;
		}

		bool CanInstall = false;

		if (IPowerManager* PowerManager =
				smart_cast<IPowerManager*>(Target))
		{
			// IstallPowerCell accepts only an empty device. Such a device
			// has 0% charge and therefore is strictly below 100%.
			CanInstall =
				PowerManager->GetUsePowerCell() &&
				!PowerManager->IsPowerCellInstalled() &&
				PowerManager->IsPowerCellInWhiteList(
					PowerCellInfo.section
				);
		}
		else if (PowerBank* PowerBankItem = smart_cast<PowerBank*>(Target))
		{
			CanInstall =
				PowerBankItem->m_power_cells.size() <
					PowerBankItem->m_max_count_power_cells &&
				PowerBankItem->GetCalculatedCondition() < 1.0f &&
				PowerBankItem->IsPowerCellInWhiteList(
					PowerCellInfo.section
				);
		}

		if (!CanInstall)
		{
			continue;
		}

		TargetsBySection.emplace(
			Target->m_section_id,
			Target
		);
	}

	for (const auto& TargetBySection : TargetsBySection)
	{
		PIItem Target = TargetBySection.second;
		shared_str Text =
			g_pStringTable->translate("st_install_power_cell");

		Text.printf(
			"%s %s",
			Text.c_str(),
			Target->m_name.c_str()
		);

		Box->AddItem(
			Text.c_str(),
			Target,
			ATTACH_POWER_CELL
		);

		HasAddedActions = true;
	}

	return HasAddedActions;
}

using namespace luabind;
#pragma optimize("s",on)
void PowerCell::script_register(lua_State* L)
{
    module(L)
        [
            class_<PowerCell, CGameObject>("PowerCell")
                .def(constructor<>())
        ];
}
