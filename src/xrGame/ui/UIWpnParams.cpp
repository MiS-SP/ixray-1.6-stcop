#include "StdAfx.h"
#include "pch_script.h"
#include "UIWpnParams.h"
#include "../../xrUI/UIXmlInit.h"
#include "../Level.h"
#include "game_base_space.h"
#include "inventory_item_object.h"
#include "UIInventoryUtilities.h"
#include "Weapon.h"
#include "WeaponMagazinedWGrenade.h"
#include "../../xrUI/UIHelper.h"
#include "../../xrUI/Widgets/UI3dStatic.h"
#include "../../xrUI/Widgets/UIFrameWindow.h"
#include "../../xrUI/Widgets/UILines.h"
#include "../../xrEngine/string_table.h"

using namespace InventoryUtilities;

namespace WeaponUI
{
static CUIStatic* CreateStatic(CUIXml& Xml, const xr_string& Path, CUIWindow* Parent)
{
	return Xml.NavigateToNode(Path.c_str()) ? UIHelper::CreateStatic(Xml, Path.c_str(), Parent) : nullptr;
}

static shared_str AmmoName(const shared_str& Section, bool UseFullName)
{
	const char* Name = READ_IF_EXISTS(pSettings, r_string, Section.c_str(), UseFullName ? "inv_name" : "inv_name_short", nullptr);
	if (!Name || !*Name)
	{
		Name = READ_IF_EXISTS(pSettings, r_string, Section.c_str(), "inv_name_short", Section.c_str());
	}
	if (!Name || !*Name)
	{
		Name = Section.c_str();
	}
	return g_pStringTable->translate(Name);
}

static float ArmorPiercingScale(float Value)
{
	// A coefficient of 0.5 fills half the UI bar; this is not a penetration probability.
	return _valid(Value) && Value > 0.0f ? float(100.0 * double(Value) / (double(Value) + 0.5)) : 0.0f;
}

static int MagazineSize(CWeapon& Weapon, bool UsePrimary)
{
	const CWeaponMagazinedWGrenade* GrenadeWeapon = UsePrimary ? Weapon.cast_weapon_magazined_w_grenade() : nullptr;
	return GrenadeWeapon && GrenadeWeapon->IsGrenadeMode() ? GrenadeWeapon->iMagazineSize2 : Weapon.GetAmmoMagSize();
}
} // namespace WeaponUI

CUIWpnParams::CUIWpnParams()
{
	AttachChild(&m_textAccuracy);
	AttachChild(&m_textDamage);
	AttachChild(&m_textHandling);
	AttachChild(&m_textRPM);
	AttachChild(&m_progressAccuracy);
	AttachChild(&m_progressDamage);
	AttachChild(&m_progressHandling);
	AttachChild(&m_progressRPM);
}

void CUIWpnParams::InitFromXml(CUIXml& Xml)
{
	IsAmmoAware = IsGameTypeSingle() && EngineExternal().UseAmmoAwareWeaponStats() && Xml.NavigateToNode("wpn_params_advanced");
	const xr_string Root = IsAmmoAware ? "wpn_params_advanced" : "wpn_params";
	if (!Xml.NavigateToNode(Root.c_str()))
	{
		return;
	}
	CUIXmlInit::InitWindow(Xml, Root.c_str(), 0, this);
	m_Prop_line = WeaponUI::CreateStatic(Xml, Root + ":prop_line", this);
	m_icon_acc = WeaponUI::CreateStatic(Xml, Root + ":static_accuracy", this);
	m_icon_dam = WeaponUI::CreateStatic(Xml, Root + ":static_damage", this);
	m_icon_han = WeaponUI::CreateStatic(Xml, Root + ":static_handling", this);
	m_icon_rpm = WeaponUI::CreateStatic(Xml, Root + ":static_rpm", this);
	CUIXmlInit::InitStatic(Xml, (Root + ":cap_accuracy").c_str(), 0, &m_textAccuracy);
	CUIXmlInit::InitStatic(Xml, (Root + ":cap_damage").c_str(), 0, &m_textDamage);
	CUIXmlInit::InitStatic(Xml, (Root + ":cap_handling").c_str(), 0, &m_textHandling);
	CUIXmlInit::InitStatic(Xml, (Root + ":cap_rpm").c_str(), 0, &m_textRPM);
	m_progressAccuracy.InitFromXml(Xml, (Root + ":progress_accuracy").c_str());
	m_progressDamage.InitFromXml(Xml, (Root + ":progress_damage").c_str());
	m_progressHandling.InitFromXml(Xml, (Root + ":progress_handling").c_str());
	m_progressRPM.InitFromXml(Xml, (Root + ":progress_rpm").c_str());
	BaseHeight = GetHeight();
	if (!IsGameTypeSingle())
	{
		return;
	}
	m_stAmmo = WeaponUI::CreateStatic(Xml, Root + ":static_ammo", this);
	m_textAmmoCount = WeaponUI::CreateStatic(Xml, Root + ":cap_ammo_count", this);
	m_textAmmoCount2 = WeaponUI::CreateStatic(Xml, Root + ":cap_ammo_count2", this);
	if (IsAmmoAware)
	{
		ArmorPiercingCaption = WeaponUI::CreateStatic(Xml, Root + ":cap_armor_piercing", this);
		const xr_string ProgressPath = Root + ":progress_armor_piercing";
		if (Xml.NavigateToNode(ProgressPath.c_str()))
		{
			ArmorPiercingProgress = new CUIDoubleProgressBar();
			ArmorPiercingProgress->SetAutoDelete(true);
			AttachChild(ArmorPiercingProgress);
			ArmorPiercingProgress->InitFromXml(Xml, ProgressPath.c_str());
			ArmorProgressBottom = CUIXmlInit::GetFRect(Xml, ProgressPath.c_str(), 0).bottom;
		}
		const xr_string FramePath = Root + ":ammo_frame";
		AmmoFrame = UIHelper::CreateFrameWindow(Xml, FramePath.c_str(), this, false);
		if (AmmoFrame)
		{
			AmmoFrame->Enable(false);
			AmmoFramePosition = AmmoFrame->GetWndPos();
			FramePadding = Xml.ReadAttribFlt(FramePath.c_str(), 0, "padding_bottom", 0.0f);
			FrameSpacing = Xml.ReadAttribFlt(FramePath.c_str(), 0, "spacing_top", 0.0f);
			GridGap.set(Xml.ReadAttribFlt(FramePath.c_str(), 0, "icons_gap_x", 0.0f), Xml.ReadAttribFlt(FramePath.c_str(), 0, "icons_gap_y", 0.0f));
			BlockPadding = std::max(0.0f, BaseHeight - AmmoFramePosition.y - AmmoFrame->GetHeight());
			AmmoBackground = WeaponUI::CreateStatic(Xml, FramePath + ":ammo_background", AmmoFrame);
			if (AmmoBackground)
			{
				BackgroundPosition = AmmoBackground->GetWndPos();
				AmmoBackground->SetStretchTexture(true);
				AmmoBackground->Enable(false);
			}
		}
		if (Xml.NavigateToNode((Root + ":ammo_selected").c_str()))
		{
			SelectedAmmoColor = CUIXmlInit::GetColor(Xml, (Root + ":ammo_selected").c_str(), 0, SelectedAmmoColor);
			HasAmmoColors = true;
		}
		if (Xml.NavigateToNode((Root + ":ammo_unselected").c_str()))
		{
			UnselectedAmmoColor = CUIXmlInit::GetColor(Xml, (Root + ":ammo_unselected").c_str(), 0, UnselectedAmmoColor);
			HasAmmoColors = true;
		}
	}
	CUIWindow* AmmoParent = AmmoFrame ? static_cast<CUIWindow*>(AmmoFrame) : this;
	const xr_string AmmoPath = Root + (AmmoFrame ? ":ammo_frame" : "");
	m_textAmmoTypes = WeaponUI::CreateStatic(Xml, AmmoPath + ":cap_ammo_types", AmmoParent);
	m_textAmmoUsedType = WeaponUI::CreateStatic(Xml, AmmoPath + ":cap_ammo_used_type", AmmoParent);
	if (IsAmmoAware && AmmoFrame)
	{
		AmmoSwitchHint = WeaponUI::CreateStatic(Xml, AmmoPath + ":ammo_switch_hint", AmmoParent);
		if (AmmoSwitchHint)
		{
			HintPosition = AmmoSwitchHint->GetWndPos();
			HintSize = AmmoSwitchHint->GetWndSize();
			AmmoSwitchHint->SetStretchTexture(true);
			AmmoSwitchHint->Enable(false);
		}
	}
	const size_t PrototypeCount = IsAmmoAware && AmmoFrame ? 1 : 3;
	AmmoIcons.resize(PrototypeCount);
	for (size_t Index = 0; Index < PrototypeCount; ++Index)
	{
		string32 Name;
		xr_sprintf(Name, ":static_ammo_type%u", u32(Index + 1));
		const xr_string Path = AmmoPath + Name;
		if (!Xml.NavigateToNode(Path.c_str()))
		{
			continue;
		}
		AmmoIcon& Icon = AmmoIcons[Index];
		Icon.Window = UIHelper::Create3dStatic(Xml, Path.c_str(), AmmoParent);
		Icon.Position = Icon.Window->GetWndPos();
		Icon.Size = Icon.Window->GetWndSize();
		Icon.Color = Icon.Window->GetTextureColor();
	}
	if (!IsAmmoAware)
	{
		return;
	}
	const xr_array<CUIStatic*, 3> Texts = {ArmorPiercingCaption, m_textAmmoTypes, m_textAmmoUsedType};
	for (size_t Index = 0; Index < Texts.size(); ++Index)
	{
		if (Texts[Index])
		{
			AmmoTextPositions[Index] = Texts[Index]->GetWndPos();
		}
	}
	float HeaderBottom = AmmoSwitchHint ? HintPosition.y + HintSize.y : 0.0f;
	if (m_textAmmoTypes)
	{
		HeaderBottom = std::max(HeaderBottom, m_textAmmoTypes->GetWndPos().y + m_textAmmoTypes->GetHeight());
	}
	if (m_textAmmoUsedType)
	{
		NameGap = std::max(0.0f, m_textAmmoUsedType->GetWndPos().y - HeaderBottom);
		GridPosition = m_textAmmoUsedType->GetWndPos();
		GridPosition.y += m_textAmmoUsedType->GetHeight();
	}
	if (!AmmoIcons.empty() && AmmoIcons.front().Window)
	{
		GridPosition = AmmoIcons.front().Position;
		IconMaxHeight = AmmoIcons.front().Size.y;
		PrototypeColor = AmmoIcons.front().Color;
	}
	if (m_textAmmoUsedType)
	{
		PreviewIconGap = std::max(0.0f, GridPosition.y - m_textAmmoUsedType->GetWndPos().y - m_textAmmoUsedType->GetHeight());
	}
}

void CUIWpnParams::SetInfo(CInventoryItem* slot_wpn, CInventoryItem& cur_wpn, const shared_str& PreviewAmmo)
{
	CWeapon& Weapon = *cur_wpn.cast_weapon();
	CWeapon* SlotWeapon = slot_wpn && slot_wpn != &cur_wpn ? slot_wpn->cast_weapon() : nullptr;
	const bool IsSinglePlayer = IsGameTypeSingle();
	static const xr_vector<shared_str> NoAmmo;
	PrepareAmmoTypes(IsSinglePlayer ? (IsAmmoAware ? Weapon.getAmmoTypes() : Weapon.m_ammoTypes) : NoAmmo);
	const AmmoInfo* Preview = IsAmmoAware ? FindAmmo(PreviewAmmo) : nullptr;
	const CWeapon::UIStats Stats = IsAmmoAware ? Weapon.GetUIStats(PreviewAmmo, Preview ? &Preview->Params : nullptr) : CWeapon::UIStats();
	const CWeapon::UIStats SlotStats = IsAmmoAware && SlotWeapon ? SlotWeapon->GetUIStats() : Stats;
	const float Damage = IsSinglePlayer ? (IsAmmoAware ? Stats.Damage : Weapon.GetDamage()) : Weapon.GetDamageMP();
	const float SlotDamage = SlotWeapon ? (IsSinglePlayer ? (IsAmmoAware ? SlotStats.Damage : SlotWeapon->GetDamage()) : SlotWeapon->GetDamageMP()) : Damage;
	const auto QuantizeLambda = [](float Value)
	{ return iFloor(Value * 53.0f) / 53.0f; };
	const float Accuracy = QuantizeLambda(Weapon.GetAccuracy());
	const float Handling = QuantizeLambda(Weapon.GetHandling());
	const float RPM = QuantizeLambda(Weapon.GetRPM());
	m_progressAccuracy.SetTwoPos(Accuracy, SlotWeapon ? QuantizeLambda(SlotWeapon->GetAccuracy()) : Accuracy);
	m_progressDamage.SetTwoPos(QuantizeLambda(Damage), QuantizeLambda(SlotDamage));
	m_progressHandling.SetTwoPos(Handling, SlotWeapon ? QuantizeLambda(SlotWeapon->GetHandling()) : Handling);
	m_progressRPM.SetTwoPos(RPM, SlotWeapon ? QuantizeLambda(SlotWeapon->GetRPM()) : RPM);
	if (!IsSinglePlayer)
	{
		return;
	}
	if (m_textAmmoCount2)
	{
		const int Count = WeaponUI::MagazineSize(Weapon, IsAmmoAware);
		const int SlotCount = SlotWeapon ? WeaponUI::MagazineSize(*SlotWeapon, IsAmmoAware) : Count;
		m_textAmmoCount2->SetTextColor(Count == SlotCount ? color_rgba(170, 170, 170, 255) : Count < SlotCount ? color_rgba(255, 0, 0, 255)
																											   : color_rgba(0, 255, 0, 255));
		string32 Text;
		xr_sprintf(Text, "%d", Count);
		m_textAmmoCount2->SetText(Text);
	}
	const bool SupportsArmorPiercing = IsAmmoAware && !Weapon.cast_weapon_rpg7() && !Weapon.cast_weapon_rg6() && Stats.AmmoSection.size() && _valid(Stats.ArmorPiercing) && Stats.ArmorPiercing >= 0.0f;
	if (ArmorPiercingCaption)
	{
		ArmorPiercingCaption->SetVisible(SupportsArmorPiercing);
	}
	if (ArmorPiercingProgress)
	{
		ArmorPiercingProgress->SetVisible(SupportsArmorPiercing);
		float SlotArmorPiercing = Stats.ArmorPiercing;
		if (SlotWeapon && !SlotWeapon->cast_weapon_rpg7() && !SlotWeapon->cast_weapon_rg6() && SlotStats.AmmoSection.size() && _valid(SlotStats.ArmorPiercing) && SlotStats.ArmorPiercing >= 0.0f)
		{
			SlotArmorPiercing = SlotStats.ArmorPiercing;
		}
		ArmorPiercingProgress->SetTwoPos(WeaponUI::ArmorPiercingScale(Stats.ArmorPiercing), WeaponUI::ArmorPiercingScale(SlotArmorPiercing));
	}
	UpdateAmmoDisplay(IsAmmoAware ? Stats.AmmoSection : AmmoTypes.empty() ? shared_str()
																		  : AmmoTypes.front().Section,
					  SupportsArmorPiercing);
}

const CUIWpnParams::AmmoInfo* CUIWpnParams::FindAmmo(const shared_str& AmmoSection) const
{
	const auto Found = std::find_if(AmmoTypes.begin(), AmmoTypes.end(), [&AmmoSection](const AmmoInfo& Info)
									{ return Info.Section == AmmoSection; });
	return Found != AmmoTypes.end() ? &*Found : nullptr;
}

bool CUIWpnParams::IsPreviewAmmoValid(const shared_str& AmmoSection) const
{
	return IsAmmoAware && FindAmmo(AmmoSection) != nullptr;
}

bool CUIWpnParams::CanCycleAmmo() const
{
	return IsAmmoAware && AmmoFrame && AmmoTypes.size() > 1;
}

shared_str CUIWpnParams::NextAmmo() const
{
	if (!CanCycleAmmo())
	{
		return shared_str();
	}
	for (size_t Index = 0; Index < AmmoTypes.size(); ++Index)
	{
		if (AmmoTypes[Index].Section == CurrentAmmoSection)
		{
			return AmmoTypes[(Index + 1) % AmmoTypes.size()].Section;
		}
	}
	return AmmoTypes.front().Section;
}

void CUIWpnParams::PrepareAmmoTypes(const xr_vector<shared_str>& Types)
{
	if (AmmoTypeSource == Types)
	{
		return;
	}
	AmmoTypeSource = Types;
	AmmoTypes.clear();
	for (const shared_str& Section : Types)
	{
		if (IsAmmoAware && (!Section.size() || !pSettings->section_exist(Section.c_str()) || FindAmmo(Section)))
		{
			continue;
		}
		AmmoInfo Info;
		Info.Section = Section;
		if (IsAmmoAware)
		{
			Info.Params = WeaponAmmo::ReadUIParams(Section);
		}
		if (Section.size() && pSettings->section_exist(Section.c_str()))
		{
			Info.Name = WeaponUI::AmmoName(Section, IsAmmoAware);
		}
		AmmoTypes.push_back(Info);
		if (!IsAmmoAware && AmmoTypes.size() == AmmoIcons.size())
		{
			break;
		}
	}
	EnsureAmmoIcons();
	IsAmmoDisplayDirty = true;
}

void CUIWpnParams::EnsureAmmoIcons()
{
	if (!IsAmmoAware || !AmmoFrame)
	{
		return;
	}
	// Reuse widgets up to the largest compatible set seen by this tooltip.
	for (size_t Index = 0; Index < AmmoTypes.size(); ++Index)
	{
		if (Index == AmmoIcons.size())
		{
			AmmoIcons.emplace_back();
		}
		AmmoIcon& Icon = AmmoIcons[Index];
		if (!Icon.Window)
		{
			Icon.Window = new CUI3dStatic();
			Icon.Window->SetAutoDelete(true);
			AmmoFrame->AttachChild(Icon.Window);
			Icon.Window->Enable(false);
			Icon.Position = GridPosition;
			Icon.Color = PrototypeColor;
		}
	}
}

bool CUIWpnParams::SetAmmoIcon(size_t Index, const shared_str& Section)
{
	AmmoIcon& Icon = AmmoIcons[Index];
	if (!Icon.Window)
	{
		return false;
	}
	if (Icon.Section == Section)
	{
		return Icon.IsValid;
	}
	Icon.Section = Section;
	Icon.IsValid = false;
	Icon.Window->SetVisual(nullptr);
	Icon.Window->SetTextureColor(Icon.Color);
	Icon.Window->SetVisible(false);
	if (!Section.size() || !pSettings->section_exist(Section.c_str()))
	{
		return false;
	}
	const char* Name = Section.c_str();
	if (!pSettings->line_exist(Name, "inv_grid_x") || !pSettings->line_exist(Name, "inv_grid_y") || !pSettings->line_exist(Name, "inv_grid_width") || !pSettings->line_exist(Name, "inv_grid_height") || !pSettings->line_exist(Name, "visual"))
	{
		return false;
	}
	InventoryIconParams Params = GetInventoryIconParams(Name);
	if (!_valid(Params.scaleIcon) || Params.scaleIcon <= 0.0f || !_valid(Params.inv_grid_x) || !_valid(Params.inv_grid_y) || !_valid(Params.inv_grid_width) || !_valid(Params.inv_grid_height) || Params.inv_grid_width <= 0.0f || Params.inv_grid_height <= 0.0f)
	{
		return false;
	}
	Frect TextureRect;
	TextureRect.lt.set(Params.inv_grid_x * INV_GRID_WIDTH(Params.scaleIcon), Params.inv_grid_y * INV_GRID_HEIGHT(Params.scaleIcon));
	TextureRect.rb.set(Params.inv_grid_width * INV_GRID_WIDTH(Params.scaleIcon), Params.inv_grid_height * INV_GRID_HEIGHT(Params.scaleIcon));
	TextureRect.rb.add(TextureRect.lt);
	Icon.Size.set(TextureRect.width() / Params.scaleIcon, TextureRect.height() / Params.scaleIcon);
	if (!_valid(Icon.Size.x) || !_valid(Icon.Size.y) || Icon.Size.x <= 0.0f || Icon.Size.y <= 0.0f)
	{
		return false;
	}
	if (EngineExternal()[EEngineExternalGame::Enable3DIcons])
	{
		Icon.Window->SetVisual(shared_str(Params._3d_static_visual));
		Icon.Window->SetXYZ(Params._3d_static_rotate);
		Icon.Window->SetScaleFactor(Params._3d_static_scale);
		Icon.Window->SetIconOffset(Params.IconOffset);
	}
	Icon.Window->SetShader(GetEquipmentIconsShader(Params.icons_texture));
	Icon.Window->SetTextureRect(TextureRect);
	Icon.Window->TextureOn();
	Icon.Window->SetStretchTexture(true);
	Icon.IsValid = true;
	return true;
}

void CUIWpnParams::UpdateAmmoDisplay(const shared_str& AmmoSection, bool SupportsArmorPiercing)
{
	const float Aspect = UI().get_current_kx();
	const float Width = AmmoFrame ? AmmoFrame->GetWidth() : GetWidth();
	if (!IsAmmoDisplayDirty && CurrentAmmoSection == AmmoSection && DisplayWidth == Width && DisplayAspect == Aspect && DisplayTargetHeight == Device.TargetHeight && IsArmorPiercingDisplayed == SupportsArmorPiercing)
	{
		return;
	}
	CurrentAmmoSection = AmmoSection;
	DisplayWidth = Width;
	DisplayAspect = Aspect;
	DisplayTargetHeight = Device.TargetHeight;
	IsArmorPiercingDisplayed = SupportsArmorPiercing;
	IsAmmoDisplayDirty = false;
	const AmmoInfo* Current = FindAmmo(AmmoSection);
	if (m_textAmmoUsedType)
	{
		xr_string Text = Current ? Current->Name.c_str() : "";
		if (!Current && AmmoSection.size() && pSettings->section_exist(AmmoSection.c_str()))
		{
			Text = WeaponUI::AmmoName(AmmoSection, IsAmmoAware).c_str();
		}
		m_textAmmoUsedType->SetText(Text.c_str());
	}
	float LastStatsBottom = 0.0f;
	if (IsAmmoAware && AmmoFrame)
	{
		const xr_array<CUIStatic*, 2> Counts = {m_textAmmoCount, m_textAmmoCount2};
		for (CUIStatic* Text : Counts)
		{
			if (Text)
			{
				LastStatsBottom = std::max(LastStatsBottom, Text->GetWndPos().y + std::max(Text->GetHeight(), Text->TextItemControl()->GetVisibleHeight()));
			}
		}
		if (m_stAmmo)
		{
			LastStatsBottom = std::max(LastStatsBottom, m_stAmmo->GetWndPos().y + m_stAmmo->GetHeight());
		}
	}
	float ArmorBottom = LastStatsBottom;
	if (IsAmmoAware && ArmorPiercingCaption && SupportsArmorPiercing)
	{
		ArmorPiercingCaption->SetWndPos(AmmoTextPositions[0]);
		ArmorPiercingCaption->SetTextComplexMode(true);
		ArmorPiercingCaption->AdjustHeightToText();
		ArmorBottom = std::max(ArmorBottom, ArmorPiercingCaption->GetWndPos().y + ArmorPiercingCaption->GetHeight());
	}
	if (ArmorPiercingProgress && SupportsArmorPiercing)
	{
		ArmorBottom = std::max(ArmorBottom, ArmorProgressBottom);
	}

	const bool IsGridLayout = IsAmmoAware && AmmoFrame;
	float PreviewBottom = 0.0f;
	if (IsGridLayout)
	{
		const float Padding = GridPosition.x;
		const float InnerWidth = std::max(0.0f, Width - 2.0f * Padding);
		const bool ShowHint = AmmoSwitchHint && CanCycleAmmo();
		float HeaderRight = Width - Padding;
		if (AmmoSwitchHint)
		{
			AmmoSwitchHint->SetVisible(ShowHint);
			AmmoSwitchHint->Enable(false);
			if (ShowHint)
			{
				Fvector2 Size = HintSize;
				Size.x *= Aspect;
				if (Size.x > 0.0f)
				{
					Size.mul(std::min(1.0f, InnerWidth / Size.x));
				}
				const Fvector2 Position = Fvector2().set(Width - Padding - Size.x, HintPosition.y);
				AmmoSwitchHint->SetWndPos(Position);
				AmmoSwitchHint->SetWndSize(Size);
				HeaderRight = Position.x - GridGap.x;
				PreviewBottom = Position.y + Size.y;
			}
		}
		if (m_textAmmoTypes)
		{
			const Fvector2 Position = AmmoTextPositions[1];
			m_textAmmoTypes->SetWndPos(Position);
			m_textAmmoTypes->SetWidth(std::max(0.0f, HeaderRight - Position.x));
			m_textAmmoTypes->SetTextComplexMode(true);
			m_textAmmoTypes->AdjustHeightToText();
			PreviewBottom = std::max(PreviewBottom, Position.y + m_textAmmoTypes->GetHeight());
		}
		if (m_textAmmoUsedType)
		{
			Fvector2 Position = AmmoTextPositions[2];
			Position.y = PreviewBottom + NameGap;
			m_textAmmoUsedType->SetWndPos(Position);
			m_textAmmoUsedType->SetWidth(std::max(0.0f, Width - 2.0f * Position.x));
			m_textAmmoUsedType->SetTextComplexMode(true);
			m_textAmmoUsedType->AdjustHeightToText();
			PreviewBottom = Position.y + m_textAmmoUsedType->GetHeight();
		}
	}

	float RowX = GridPosition.x;
	float RowY = PreviewBottom + PreviewIconGap;
	float RowHeight = 0.0f;
	const float InnerWidth = std::max(0.0f, Width - 2.0f * GridPosition.x);
	for (size_t Index = 0; Index < AmmoIcons.size(); ++Index)
	{
		AmmoIcon& Icon = AmmoIcons[Index];
		if (!Icon.Window)
		{
			continue;
		}
		const AmmoInfo* Info = Index < AmmoTypes.size() ? &AmmoTypes[Index] : nullptr;
		if (!SetAmmoIcon(Index, Info ? Info->Section : shared_str()) || (IsGridLayout && InnerWidth <= 0.0f))
		{
			Icon.Window->SetVisible(false);
			continue;
		}
		Fvector2 Position = Icon.Position;
		Fvector2 Size = Icon.Size;
		Size.x *= Aspect;
		if (IsGridLayout)
		{
			float Scale = IconMaxHeight > 0.0f ? std::min(1.0f, IconMaxHeight / Size.y) : 1.0f;
			Scale = std::min(Scale, InnerWidth / Size.x);
			Size.mul(Scale);
			if (RowX > GridPosition.x && RowX + Size.x > Width - GridPosition.x)
			{
				RowX = GridPosition.x;
				RowY += RowHeight + GridGap.y;
				RowHeight = 0.0f;
			}
			Position.set(RowX, RowY);
			RowX += Size.x + GridGap.x;
			RowHeight = std::max(RowHeight, Size.y);
			const bool IsSelected = Info == Current;
			Icon.Window->SetTextureColor(HasAmmoColors ? (IsSelected ? SelectedAmmoColor : UnselectedAmmoColor) : (IsSelected ? Icon.Color : subst_alpha(Icon.Color, color_get_A(Icon.Color) * 160 / 255)));
		}
		Icon.Window->SetWndPos(Position);
		Icon.Window->SetWndSize(Size);
		Icon.Window->SetVisible(true);
		Icon.Window->Enable(false);
	}
	if (!IsGridLayout)
	{
		return;
	}
	const bool ShowFrame = !AmmoTypes.empty() || AmmoSection.size();
	AmmoFrame->SetVisible(ShowFrame);
	Fvector2 Position = AmmoFramePosition;
	Position.y = ArmorBottom + FrameSpacing;
	AmmoFrame->SetWndPos(Position);
	const float Bottom = RowHeight > 0.0f ? RowY + RowHeight : PreviewBottom;
	AmmoFrame->SetWndSize(Fvector2().set(Width, Bottom + FramePadding));
	if (AmmoBackground)
	{
		AmmoBackground->SetWndSize(Fvector2().set(std::max(0.0f, Width - 2.0f * BackgroundPosition.x), std::max(0.0f, AmmoFrame->GetHeight() - 2.0f * BackgroundPosition.y)));
	}
	SetHeight((ShowFrame ? Position.y + AmmoFrame->GetHeight() : LastStatsBottom) + BlockPadding);
}

bool CUIWpnParams::Check(CInventoryItem& wpn_section)
{
	if (wpn_section.cast_weapon() == nullptr)
	{
		return false;
	}

	const char* wpn_sect = wpn_section.object().cNameSect().c_str();
	if (pSettings->line_exist(wpn_sect, "fire_dispersion_base"))
	{
		// Alundaio: Most likely a fake weapon or melee weapon
		if (pSettings->line_exist(wpn_sect, "ammo_mag_size") && pSettings->r_u32(wpn_sect, "ammo_mag_size") == 0)
		{
			return false;
		}

		if (wpn_section.cast_weapon_binoculars())
		{
			return false;
		}
		if (wpn_section.cast_weapon_knife())
		{
			return false;
		}

		return true;
	}
	return false;
}

// -------------------------------------------------------------------------------------------------

CUIConditionParams::CUIConditionParams()
{
	AttachChild(&m_progress);
	AttachChild(&m_text);
}

CUIConditionParams::~CUIConditionParams()
{
}

void CUIConditionParams::InitFromXml(CUIXml& xml_doc)
{
	if (!xml_doc.NavigateToNode("condition_params", 0))
	{
		return;
	}
	CUIXmlInit::InitWindow(xml_doc, "condition_params", 0, this);
	CUIXmlInit::InitStatic(xml_doc, "condition_params:caption", 0, &m_text);
	m_progress.InitFromXml(xml_doc, "condition_params:progress_state");
}

void CUIConditionParams::SetInfo(CInventoryItem const* slot_item, CInventoryItem const& cur_item)
{
	float cur_value = cur_item.GetConditionToShow() * 100.0f + 1.0f - EPS;
	float slot_value = cur_value;

	if (slot_item && (slot_item != &cur_item) /*&& (cur_item.object().cNameSect()._get() == slot_item->object().cNameSect()._get())*/)
	{
		slot_value = slot_item->GetConditionToShow() * 100.0f + 1.0f - EPS;
	}
	m_progress.SetTwoPos(cur_value, slot_value);
}
