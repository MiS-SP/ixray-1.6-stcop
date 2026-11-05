#pragma once

#include "../../xrUI/Widgets/UIWindow.h"
#include "../../xrUI/Widgets/UIDoubleProgressBar.h"
#include "../WeaponAmmo.h"

class CUIXml;
class CUI3dStatic;
class CUIFrameWindow;
class CInventoryItem;

#include "../../xrScripts/script_export_space.h"

struct SLuaWpnParams;

class CUIWpnParams final :
	public CUIWindow
{
public:
	CUIWpnParams();
	virtual ~CUIWpnParams() = default;

	void InitFromXml(CUIXml& xml_doc);
	void SetInfo(CInventoryItem* slot_wpn, CInventoryItem& cur_wpn, const shared_str& PreviewAmmo = shared_str());
	bool CanCycleAmmo() const;
	shared_str NextAmmo() const;
	bool IsPreviewAmmoValid(const shared_str& AmmoSection) const;
	bool Check(CInventoryItem& wpn_section);

	virtual CUIWindow* ui_cast_window() { return this; }

protected:
	CUIDoubleProgressBar m_progressAccuracy; // red or green
	CUIDoubleProgressBar m_progressHandling;
	CUIDoubleProgressBar m_progressDamage;
	CUIDoubleProgressBar m_progressRPM;

	CUIStatic* m_icon_acc = nullptr;
	CUIStatic* m_icon_dam = nullptr;
	CUIStatic* m_icon_han = nullptr;
	CUIStatic* m_icon_rpm = nullptr;

	CUIStatic* m_stAmmo = nullptr;
	CUIStatic m_textAccuracy;
	CUIStatic m_textHandling;
	CUIStatic m_textDamage;
	CUIStatic m_textRPM;
	CUIStatic* m_textAmmoTypes = nullptr;
	CUIStatic* m_textAmmoUsedType = nullptr;
	CUIStatic* m_textAmmoCount = nullptr;
	CUIStatic* m_textAmmoCount2 = nullptr;
	CUIStatic* m_Prop_line = nullptr;

private:
	struct AmmoInfo
	{
		shared_str Section;
		shared_str Name;
		WeaponAmmo::UIParams Params;
	};

	struct AmmoIcon
	{
		CUI3dStatic* Window = nullptr;
		shared_str Section;
		Fvector2 Position = {};
		Fvector2 Size = {};
		u32 Color = 0xffffffff;
		bool IsValid = false;
	};

	void PrepareAmmoTypes(const xr_vector<shared_str>& Types);
	void EnsureAmmoIcons();
	bool SetAmmoIcon(size_t Index, const shared_str& Section);
	void UpdateAmmoDisplay(const shared_str& AmmoSection, bool SupportsArmorPiercing);
	const AmmoInfo* FindAmmo(const shared_str& AmmoSection) const;

	xr_vector<shared_str> AmmoTypeSource;
	xr_vector<AmmoInfo> AmmoTypes;
	xr_vector<AmmoIcon> AmmoIcons;
	xr_array<Fvector2, 3> AmmoTextPositions = {};
	shared_str CurrentAmmoSection;
	CUIStatic* ArmorPiercingCaption = nullptr;
	CUIDoubleProgressBar* ArmorPiercingProgress = nullptr;
	CUIFrameWindow* AmmoFrame = nullptr;
	CUIStatic* AmmoBackground = nullptr;
	CUIStatic* AmmoSwitchHint = nullptr;
	Fvector2 AmmoFramePosition = {};
	Fvector2 BackgroundPosition = {};
	Fvector2 HintPosition = {};
	Fvector2 HintSize = {};
	Fvector2 GridPosition = {};
	Fvector2 GridGap = {};
	float IconMaxHeight = 0.0f;
	float BaseHeight = 0.0f;
	float ArmorProgressBottom = 0.0f;
	float FramePadding = 0.0f;
	float BlockPadding = 0.0f;
	float FrameSpacing = 0.0f;
	float NameGap = 0.0f;
	float PreviewIconGap = 0.0f;
	float DisplayWidth = 0.0f;
	float DisplayAspect = 0.0f;
	u32 DisplayTargetHeight = 0;
	u32 PrototypeColor = 0xffffffff;
	u32 SelectedAmmoColor = 0xffffffff;
	u32 UnselectedAmmoColor = 0xffffffff;
	bool HasAmmoColors = false;
	bool IsAmmoAware = false;
	bool IsAmmoDisplayDirty = true;
	bool IsArmorPiercingDisplayed = false;
};

// -------------------------------------------------------------------------------------------------

class CUIConditionParams : public CUIWindow
{
public:
	CUIConditionParams();
	virtual ~CUIConditionParams();

	void InitFromXml(CUIXml& xml_doc);
	void SetInfo(CInventoryItem const* slot_wpn, CInventoryItem const& cur_wpn);

	virtual CUIWindow* ui_cast_window() { return this; }

protected:
	CUIDoubleProgressBar m_progress; // red or green
	CUIStatic m_text;
};
