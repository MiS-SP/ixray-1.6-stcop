#include "StdAfx.h"
#include "Weapon.h"
#include "WeaponKnife.h"
#include "WeaponMagazinedWGrenade.h"

static float normalize(float val, float min, float max)
{
	float d = 100.0f * (val - min) / (max - min);
	return d < 0.0f ? 0.0f : d;
}

float CWeapon::GetRPM() const
{
	float rpm = fOneShotTimeSaved;
	return normalize(rpm, 0.0f, 1500.0f);
}

float CWeapon::GetDamage() const
{
	// Fixed UI calibration, shared by every weapon: D50 maps to 50 percent.
	constexpr double DamageHalfScale = 1.75;
	constexpr double DamageExponent = 1.25;

	double Damage = fvHitPower[g_SingleGameDifficulty];
	// Knife hits do not use firearm ammunition or the owner's weapon modifiers.
	if (!smart_cast<const CWeaponKnife*>(this))
	{
		// Keep the rifle cartridge when the underbarrel launcher swaps magazines.
		const auto* GrenadeWeapon = IsGrenadeMode() ? smart_cast<const CWeaponMagazinedWGrenade*>(this) : nullptr;
		const auto& Magazine = GrenadeWeapon ? GrenadeWeapon->m_magazine2 : m_magazine;
		const CCartridge* Cartridge = nullptr;
		if (m_bAmmoInChamber && !m_chamber.empty())
		{
			Cartridge = &m_chamber.back();
		}
		else if (!Magazine.empty())
		{
			Cartridge = &Magazine.back();
		}

		double CartridgeHitPower = 1.0;
		int ProjectileCount = 1;
		if (Cartridge)
		{
			CartridgeHitPower = Cartridge->param_s.kHit;
			ProjectileCount = Cartridge->param_s.buckShot;
		}
		else
		{
			// Empty weapons use the selected primary ammo type, never an ammo average.
			const auto& AmmoTypes = getAmmoTypes();
			if (!AmmoTypes.empty())
			{
				const u8 AmmoIndex = GetAmmoType();
				const shared_str& AmmoSection = AmmoTypes[AmmoIndex < AmmoTypes.size() ? AmmoIndex : 0];
				CartridgeHitPower = READ_IF_EXISTS(pSettings, r_float, AmmoSection, "k_hit", 1.0f);
				ProjectileCount = READ_IF_EXISTS(pSettings, r_s32, AmmoSection, "buck_shot", 1);
			}
		}

		// Potential damage of one cartridge, before distance, accuracy and target armor.
		const auto& Modifiers = *OwnerWeaponModifiers;
		Damage *= static_cast<double>(Modifiers[WeaponModifiers::eHitPowerCoeff]) * Modifiers[IsSilencerAttached() ? WeaponModifiers::eSilencerHitPowerCoeff : WeaponModifiers::eUnsilencedHitPowerCoeff] * cur_silencer_koef.hit_power * CartridgeHitPower * std::max(ProjectileCount, 0);
	}

	if (!(Damage > 0.0))
	{
		return 0.0f;
	}

	// Equivalent to 100 * D^gamma / (D^gamma + D50^gamma), without large powers.
	return static_cast<float>(100.0 / (1.0 + std::pow(DamageHalfScale / Damage, DamageExponent)));
}

float CWeapon::GetDamageMP() const
{
	float hit_power = fvHitPower[g_SingleGameDifficulty];
	hit_power *= 100.0f;

	return clampr(hit_power, 1.0f, hit_power);
}

float CWeapon::GetHandling() const
{
	float crosshair_inertion = m_crosshair_inertion;

	crosshair_inertion = 11.9f - crosshair_inertion;
	crosshair_inertion = normalize(crosshair_inertion, 0.0f, 10.5f);

	float pdm = m_pdm.m_fPDM_disp_base;

	pdm = 3.5f - pdm;
	pdm = normalize(pdm, 0.5f, 3.2f);

	float cam_disp = rad2deg(cam_recoil.Dispersion);

	cam_disp = std::sqrt(cam_disp);
	cam_disp = normalize(2.25f - cam_disp, 0.2f, 1.8f);

	return (pdm * 2.0f + crosshair_inertion + cam_disp * 3.0f) / 6.0f;
}

float CWeapon::GetAccuracy() const
{
	float fire_dispersion_base = rad2deg(fireDispersionBase);

	fire_dispersion_base = 0.85f - fire_dispersion_base;
	fire_dispersion_base = normalize(fire_dispersion_base, 0.3f, 0.8f);

	float pdm = m_pdm.m_fPDM_disp_base;

	pdm = 3.5f - pdm;
	pdm = normalize(pdm, 0.5f, 3.2f);

	return (pdm + fire_dispersion_base * 2.0f) / 3.0f;
}
