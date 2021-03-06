////////////////////////////////////////////////////////////////////////////
//	Module 		: xrServer_Objects_ALife_Items.cpp
//	Created 	: 19.09.2002
//  Modified 	: 04.06.2003
//	Author		: Oles Shyshkovtsov, Alexander Maksimchuk, Victor Reutskiy and Dmitriy Iassenev
//	Description : Server objects items for ALife simulator
////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "xrMessages.h"
#include "xrServer_Objects_ALife_Items.h"
#include "clsid_game.h"
#include "Common/object_broker.h"
#include "xrCore/Animation/Bone.hpp"

#ifdef XRGAME_EXPORTS
#ifdef DEBUG
#define PHPH_DEBUG
#endif
#endif
#ifdef PHPH_DEBUG
#include "PHDebug.h"
#endif
////////////////////////////////////////////////////////////////////////////
// CSE_ALifeInventoryItem
////////////////////////////////////////////////////////////////////////////
CSE_ALifeInventoryItem::CSE_ALifeInventoryItem(LPCSTR caSection) : m_self(nullptr), prev_freezed(false), m_u8NumItems(0)
{
    //текущее состояние вещи
    m_fCondition = 1.0f;

    m_fMass = pSettings->r_float(caSection, "inv_weight");
    m_dwCost = pSettings->r_u32(caSection, "cost");

    if (pSettings->line_exist(caSection, "condition"))
        m_fCondition = pSettings->r_float(caSection, "condition");

    if (pSettings->line_exist(caSection, "health_value"))
        m_iHealthValue = pSettings->r_s32(caSection, "health_value");
    else
        m_iHealthValue = 0;

    if (pSettings->line_exist(caSection, "food_value"))
        m_iFoodValue = pSettings->r_s32(caSection, "food_value");
    else
        m_iFoodValue = 0;

    m_fDeteriorationValue = 0;

    m_last_update_time = 0;

    State.quaternion.x = 0.f;
    State.quaternion.y = 0.f;
    State.quaternion.z = 1.f;
    State.quaternion.w = 0.f;

    State.angular_vel.set(0.f, 0.f, 0.f);
    State.linear_vel.set(0.f, 0.f, 0.f);

#ifdef XRGAME_EXPORTS
    m_freeze_time = Device.dwTimeGlobal;
#else
    m_freeze_time = 0;
#endif

    m_relevent_random.seed(u32(CPU::GetCLK() & u32(-1)));
    freezed = false;
}

CSE_Abstract* CSE_ALifeInventoryItem::init()
{
    m_self = smart_cast<CSE_ALifeObject*>(this);
    R_ASSERT(m_self);
    //	m_self->m_flags.set			(CSE_ALifeObject::flSwitchOffline,TRUE);
    return (base());
}

CSE_ALifeInventoryItem::~CSE_ALifeInventoryItem() {}
void CSE_ALifeInventoryItem::STATE_Write(NET_Packet& tNetPacket)
{
    tNetPacket.w_float(m_fCondition);
    save_data(m_upgrades, tNetPacket);
    State.position = base()->o_Position;
}

void CSE_ALifeInventoryItem::STATE_Read(NET_Packet& tNetPacket, u16 size)
{
    u16 m_wVersion = base()->m_wVersion;
    if (m_wVersion > 52)
        tNetPacket.r_float(m_fCondition);

    if (m_wVersion > 123)
    {
        load_data(m_upgrades, tNetPacket);
    }

    State.position = base()->o_Position;
}

static inline bool check(const u8& mask, const u8& test) { return (!!(mask & test)); }
const u32 CSE_ALifeInventoryItem::m_freeze_delta_time = 1000;
const u32 CSE_ALifeInventoryItem::random_limit = 120;

// if TRUE, then object sends update packet
BOOL CSE_ALifeInventoryItem::Net_Relevant()
{
    if (base()->ID_Parent != u16(-1))
        return FALSE;

    if (!freezed)
        return TRUE;

#ifdef XRGAME_EXPORTS
    if (Device.dwTimeGlobal >= (m_freeze_time + m_freeze_delta_time))
        return FALSE;
#endif

    if (!prev_freezed)
    {
        prev_freezed = true; // i.e. freezed
        return TRUE;
    }

    if (m_relevent_random.randI(random_limit))
        return FALSE;

    return TRUE;
}

void CSE_ALifeInventoryItem::UPDATE_Write(NET_Packet& tNetPacket)
{
    if (!m_u8NumItems)
    {
        tNetPacket.w_u8(0);
        return;
    }

    mask_num_items num_items;
    num_items.mask = 0;
    num_items.num_items = m_u8NumItems;

    R_ASSERT2(num_items.num_items < (u8(1) << 5), make_string("%d", num_items.num_items));

    if (State.enabled)
        num_items.mask |= inventory_item_state_enabled;
    if (fis_zero(State.angular_vel.square_magnitude()))
        num_items.mask |= inventory_item_angular_null;
    if (fis_zero(State.linear_vel.square_magnitude()))
        num_items.mask |= inventory_item_linear_null;
    // if (anim_use)										num_items.mask |= animated;

    tNetPacket.w_u8(num_items.common);

    /*if(check(num_items.mask,animated))
    {
        tNetPacket.w_float				(m_blend_timeCurrent);
    }*/

    {
        tNetPacket.w_vec3(State.force);
        tNetPacket.w_vec3(State.torque);

        tNetPacket.w_vec3(State.position);

        tNetPacket.w_float(State.quaternion.x);
        tNetPacket.w_float(State.quaternion.y);
        tNetPacket.w_float(State.quaternion.z);
        tNetPacket.w_float(State.quaternion.w);

        if (!check(num_items.mask, inventory_item_angular_null))
        {
            tNetPacket.w_float(State.angular_vel.x);
            tNetPacket.w_float(State.angular_vel.y);
            tNetPacket.w_float(State.angular_vel.z);
        }

        if (!check(num_items.mask, inventory_item_linear_null))
        {
            tNetPacket.w_float(State.linear_vel.x);
            tNetPacket.w_float(State.linear_vel.y);
            tNetPacket.w_float(State.linear_vel.z);
        }
    }
    tNetPacket.w_u8(1); // not freezed - doesn't mean anything...
};

void CSE_ALifeInventoryItem::UPDATE_Read(NET_Packet& tNetPacket)
{
    tNetPacket.r_u8(m_u8NumItems);
    if (!m_u8NumItems)
    {
        // Msg("--- Object [%d] has no sync items", this->cast_abstract()->ID);
        return;
    }

    mask_num_items num_items;
    num_items.common = m_u8NumItems;
    m_u8NumItems = num_items.num_items;

    R_ASSERT2(m_u8NumItems < (u8(1) << 5), make_string("%d", m_u8NumItems));

    /*if (check(num_items.mask,animated))
    {
        tNetPacket.r_float(m_blend_timeCurrent);
        anim_use=true;
    }
    else
    {
    anim_use=false;
    }*/

    {
        tNetPacket.r_vec3(State.force);
        tNetPacket.r_vec3(State.torque);

        tNetPacket.r_vec3(State.position);
        base()->o_Position.set(State.position); // this is very important because many functions use this o_Position..

        tNetPacket.r_float(State.quaternion.x);
        tNetPacket.r_float(State.quaternion.y);
        tNetPacket.r_float(State.quaternion.z);
        tNetPacket.r_float(State.quaternion.w);

        State.enabled = check(num_items.mask, inventory_item_state_enabled);

        if (!check(num_items.mask, inventory_item_angular_null))
        {
            tNetPacket.r_float(State.angular_vel.x);
            tNetPacket.r_float(State.angular_vel.y);
            tNetPacket.r_float(State.angular_vel.z);
        }
        else
            State.angular_vel.set(0.f, 0.f, 0.f);

        if (!check(num_items.mask, inventory_item_linear_null))
        {
            tNetPacket.r_float(State.linear_vel.x);
            tNetPacket.r_float(State.linear_vel.y);
            tNetPacket.r_float(State.linear_vel.z);
        }
        else
            State.linear_vel.set(0.f, 0.f, 0.f);

        /*if (check(num_items.mask,animated))
        {
            anim_use=true;
        }*/
    }
    prev_freezed = freezed;
    if (tNetPacket.r_eof()) // in case spawn + update
    {
        freezed = false;
        return;
    }
    if (tNetPacket.r_u8())
    {
        freezed = false;
    }
    else
    {
        if (!freezed)
#ifdef XRGAME_EXPORTS
            m_freeze_time = Device.dwTimeGlobal;
#else
            m_freeze_time = 0;
#endif
        freezed = true;
    }
};

#ifndef XRGAME_EXPORTS
void CSE_ALifeInventoryItem::FillProps(LPCSTR pref, PropItemVec& values)
{
    PHelper().CreateFloat(values, PrepareKey(pref, *base()->s_name, "Item condition"), &m_fCondition, 0.f, 1.f);
    CSE_ALifeObject* alife_object = smart_cast<CSE_ALifeObject*>(base());
    R_ASSERT(alife_object);
    PHelper().CreateFlag32(values, PrepareKey(pref, *base()->s_name, "ALife" DELIMITER "Useful for AI"),
        &alife_object->m_flags, CSE_ALifeObject::flUsefulForAI);
    PHelper().CreateFlag32(values, PrepareKey(pref, *base()->s_name, "ALife" DELIMITER "Visible for AI"),
        &alife_object->m_flags, CSE_ALifeObject::flVisibleForAI);
}
#endif // #ifndef XRGAME_EXPORTS

bool CSE_ALifeInventoryItem::bfUseful() { return (true); }
u32 CSE_ALifeInventoryItem::update_rate() const { return (1000); }
bool CSE_ALifeInventoryItem::has_upgrade(const shared_str& upgrade_id)
{
    return (std::find(m_upgrades.begin(), m_upgrades.end(), upgrade_id) != m_upgrades.end());
}

void CSE_ALifeInventoryItem::add_upgrade(const shared_str& upgrade_id)
{
    if (!has_upgrade(upgrade_id))
    {
        m_upgrades.push_back(upgrade_id);
        return;
    }
    FATAL(make_string("Can`t add existent upgrade (%s)!", upgrade_id.c_str()).c_str());
}

////////////////////////////////////////////////////////////////////////////
// CSE_ALifeItem
////////////////////////////////////////////////////////////////////////////
CSE_ALifeItem::CSE_ALifeItem(LPCSTR caSection)
    : CSE_ALifeDynamicObjectVisual(caSection), CSE_ALifeInventoryItem(caSection)
{
    m_physics_disabled = false;
}

CSE_ALifeItem::~CSE_ALifeItem() {}
CSE_Abstract* CSE_ALifeItem::init()
{
    inherited1::init();
    inherited2::init();
    return (base());
}

CSE_Abstract* CSE_ALifeItem::base() { return (inherited1::base()); }
const CSE_Abstract* CSE_ALifeItem::base() const { return (inherited1::base()); }
void CSE_ALifeItem::STATE_Write(NET_Packet& tNetPacket)
{
    inherited1::STATE_Write(tNetPacket);
    inherited2::STATE_Write(tNetPacket);
}

void CSE_ALifeItem::STATE_Read(NET_Packet& tNetPacket, u16 size)
{
    inherited1::STATE_Read(tNetPacket, size);
    if ((m_tClassID == CLSID_OBJECT_W_BINOCULAR) && (m_wVersion < 37))
    {
        tNetPacket.r_u16();
        tNetPacket.r_u16();
        tNetPacket.r_u8();
    }
    inherited2::STATE_Read(tNetPacket, size);
}

void CSE_ALifeItem::UPDATE_Write(NET_Packet& tNetPacket)
{
    inherited1::UPDATE_Write(tNetPacket);
    inherited2::UPDATE_Write(tNetPacket);

#ifdef XRGAME_EXPORTS
    m_last_update_time = Device.dwTimeGlobal;
#endif // XRGAME_EXPORTS
};

void CSE_ALifeItem::UPDATE_Read(NET_Packet& tNetPacket)
{
    inherited1::UPDATE_Read(tNetPacket);
    inherited2::UPDATE_Read(tNetPacket);

    m_physics_disabled = false;
};

#ifndef XRGAME_EXPORTS
void CSE_ALifeItem::FillProps(LPCSTR pref, PropItemVec& values)
{
    inherited1::FillProps(pref, values);
    inherited2::FillProps(pref, values);
}
#endif // #ifndef XRGAME_EXPORTS

BOOL CSE_ALifeItem::Net_Relevant()
{
    if (inherited1::Net_Relevant())
        return (TRUE);

    if (inherited2::Net_Relevant())
        return (TRUE);

    if (attached())
        return (FALSE);

    if (!m_physics_disabled && !fis_zero(State.linear_vel.square_magnitude(), EPS_L))
        return (TRUE);

#ifdef XRGAME_EXPORTS
//	if (Device.dwTimeGlobal < (m_last_update_time + update_rate()))
//		return					(FALSE);
#endif // XRGAME_EXPORTS

    return (FALSE);
}

void CSE_ALifeItem::OnEvent(NET_Packet& tNetPacket, u16 type, u32 time, ClientID sender)
{
    inherited1::OnEvent(tNetPacket, type, time, sender);

    if (type != GE_FREEZE_OBJECT)
        return;

    //	R_ASSERT					(!m_physics_disabled);
    m_physics_disabled = true;
}

////////////////////////////////////////////////////////////////////////////
// CSE_ALifeItemTorch
////////////////////////////////////////////////////////////////////////////
CSE_ALifeItemTorch::CSE_ALifeItemTorch(LPCSTR caSection) : CSE_ALifeItem(caSection)
{
    m_active = false;
    m_nightvision_active = false;
    m_attached = false;
}

CSE_ALifeItemTorch::~CSE_ALifeItemTorch() {}
BOOL CSE_ALifeItemTorch::Net_Relevant()
{
    if (m_attached)
        return true;
    return inherited::Net_Relevant();
}

void CSE_ALifeItemTorch::STATE_Read(NET_Packet& tNetPacket, u16 size)
{
    if (m_wVersion > 20)
        inherited::STATE_Read(tNetPacket, size);
}

void CSE_ALifeItemTorch::STATE_Write(NET_Packet& tNetPacket) { inherited::STATE_Write(tNetPacket); }
void CSE_ALifeItemTorch::UPDATE_Read(NET_Packet& tNetPacket)
{
    inherited::UPDATE_Read(tNetPacket);

    BYTE F = tNetPacket.r_u8();
    m_active = !!(F & eTorchActive);
    m_nightvision_active = !!(F & eNightVisionActive);
    m_attached = !!(F & eAttached);
}

void CSE_ALifeItemTorch::UPDATE_Write(NET_Packet& tNetPacket)
{
    inherited::UPDATE_Write(tNetPacket);

    BYTE F = 0;
    F |= (m_active ? eTorchActive : 0);
    F |= (m_nightvision_active ? eNightVisionActive : 0);
    F |= (m_attached ? eAttached : 0);
    tNetPacket.w_u8(F);
}

#ifndef XRGAME_EXPORTS
void CSE_ALifeItemTorch::FillProps(LPCSTR pref, PropItemVec& values) { inherited::FillProps(pref, values); }
#endif // #ifndef XRGAME_EXPORTS

////////////////////////////////////////////////////////////////////////////
// CSE_ALifeItemWeapon //--#SM+#--
////////////////////////////////////////////////////////////////////////////
CSE_ALifeItemWeapon::CSE_ALifeItemWeapon(LPCSTR caSection) : CSE_ALifeItem(caSection)
{
    wpn_flags = 0;
    wpn_state = 0;

    m_u8CurFireMode = 0;
    m_bGrenadeMode = 0;

    m_fHitPower = pSettings->r_float(caSection, "hit_power");
    m_tHitType = ALife::g_tfString2HitType(pSettings->r_string(caSection, "hit_type"));

    m_pAmmoMain.clear();
    m_pAmmoGL.clear();
    m_caAmmoSections = pSettings->r_string(caSection, "ammo_class");
    m_ammo_elapsed_sdk = u16(-1);
    m_ammo_type_sdk = u8(-1);

    if (pSettings->section_exist(caSection) && pSettings->line_exist(caSection, "visual"))
        set_visual(pSettings->r_string(caSection, "visual"));

    m_addon_flags_sdk.zero();

    m_scope_section = nullptr;
    m_muzzle_section = nullptr;
    m_launcher_section = nullptr;
    m_magaz_section = nullptr;
    m_spec_1_section = nullptr;
    m_spec_2_section = nullptr;
    m_spec_3_section = nullptr;
    m_spec_4_section = nullptr;

    m_scope_status = (EWeaponAddonStatus)(READ_IF_EXISTS(pSettings, r_s32, s_name, "scope_status", 0));
    m_silencer_status = (EWeaponAddonStatus)(READ_IF_EXISTS(pSettings, r_s32, s_name, "silencer_status", 0));
    m_grenade_launcher_status = (EWeaponAddonStatus)(READ_IF_EXISTS(pSettings, r_s32, s_name, "grenade_launcher_status", 0));
    m_magazine_status = (EWeaponAddonStatus)(READ_IF_EXISTS(pSettings, r_s32, s_name, "magazine_status", 0));
    m_spec_1_status = (EWeaponAddonStatus)(READ_IF_EXISTS(pSettings, r_s32, s_name, "special_1_status", 0));
    m_spec_2_status = (EWeaponAddonStatus)(READ_IF_EXISTS(pSettings, r_s32, s_name, "special_2_status", 0));
    m_spec_3_status = (EWeaponAddonStatus)(READ_IF_EXISTS(pSettings, r_s32, s_name, "special_3_status", 0));
    m_spec_4_status = (EWeaponAddonStatus)(READ_IF_EXISTS(pSettings, r_s32, s_name, "special_4_status", 0));

    if (m_scope_status != EWeaponAddonStatus::eAddonDisabled)
        load_def_addons_list("scopes_sect", m_def_scope_list);
    if (m_silencer_status != EWeaponAddonStatus::eAddonDisabled)
        load_def_addons_list("muzzles_sect", m_def_muzzle_list);
    if (m_grenade_launcher_status != EWeaponAddonStatus::eAddonDisabled)
        load_def_addons_list("launchers_sect", m_def_launcher_list);
    if (m_magazine_status != EWeaponAddonStatus::eAddonDisabled)
        load_def_addons_list("magazines_sect", m_def_magaz_list);
    if (m_spec_1_status != EWeaponAddonStatus::eAddonDisabled)
        load_def_addons_list("specials_1_sect", m_def_spec_1_list);
    if (m_spec_2_status != EWeaponAddonStatus::eAddonDisabled)
        load_def_addons_list("specials_2_sect", m_def_spec_2_list);
    if (m_spec_3_status != EWeaponAddonStatus::eAddonDisabled)
        load_def_addons_list("specials_3_sect", m_def_spec_3_list);
    if (m_spec_4_status != EWeaponAddonStatus::eAddonDisabled)
        load_def_addons_list("specials_4_sect", m_def_spec_4_list);

    m_ef_main_weapon_type = READ_IF_EXISTS(pSettings, r_u32, caSection, "ef_main_weapon_type", u32(-1));
    m_ef_weapon_type = READ_IF_EXISTS(pSettings, r_u32, caSection, "ef_weapon_type", u32(-1));

    // Загружаем аддоны, установленные по умолчанию
#define DEF_LoadDefaultAddon(DEF_status, DEF_output, DEF_line)                                \
    {                                                                                         \
        if (DEF_status == ALife::eAddonAttachable && pSettings->line_exist(s_name, DEF_line)) \
        {                                                                                     \
            string256 sTemp;                                                                  \
            LPCSTR sAllAddons = pSettings->r_string(s_name, DEF_line);                        \
            _GetRandomItem(sAllAddons, sTemp);                                                \
            if (xr_strcmp(sTemp, "none") != 0)                                                \
                DEF_output = sTemp;                                                           \
        }                                                                                     \
    }

    DEF_LoadDefaultAddon(m_scope_status, m_scope_section, "scope_by_default");
    DEF_LoadDefaultAddon(m_silencer_status, m_muzzle_section, "sil_by_default");
    DEF_LoadDefaultAddon(m_grenade_launcher_status, m_launcher_section, "gl_by_default");
    DEF_LoadDefaultAddon(m_magazine_status, m_magaz_section, "magaz_by_default");
    DEF_LoadDefaultAddon(m_spec_1_status, m_spec_1_section, "spec_1_by_default");
    DEF_LoadDefaultAddon(m_spec_2_status, m_spec_2_section, "spec_2_by_default");
    DEF_LoadDefaultAddon(m_spec_3_status, m_spec_3_section, "spec_3_by_default");
    DEF_LoadDefaultAddon(m_spec_4_status, m_spec_4_section, "spec_4_by_default");
}

CSE_ALifeItemWeapon::~CSE_ALifeItemWeapon() {}

// Заполнить главный магазин оружия, патроны сбрасываются на стандартный тип
void CSE_ALifeItemWeapon::refill_with_ammo(bool bForce)
{
    u32 a_elapsed = 0;

    if (m_magaz_section != NULL)
    { // При установленном магазине считываем из него
        bool bFillMagazWithAmmo = bForce || READ_IF_EXISTS(pSettings, r_bool, s_name, "magaz_spawn_ammo", false);
        if (bFillMagazWithAmmo)
        {
            LPCSTR sMagazineSect = READ_IF_EXISTS(pSettings, r_string, m_magaz_section, "magazine_name", NULL);
            if (sMagazineSect != NULL)
                a_elapsed = READ_IF_EXISTS(pSettings, r_u16, sMagazineSect, "ammo_mag_size", 0);
        }
    }
    else
    {
        a_elapsed = get_ammo_magsize();
    }

    CAmmoCompressUtil::AddAmmo(m_pAmmoMain, a_elapsed, 0, true);
}

u32 CSE_ALifeItemWeapon::ef_main_weapon_type() const
{
    VERIFY(m_ef_main_weapon_type != u32(-1));
    return (m_ef_main_weapon_type);
}

u32 CSE_ALifeItemWeapon::ef_weapon_type() const
{
    VERIFY(m_ef_weapon_type != u32(-1));
    return (m_ef_weapon_type);
}

// Загрузка дефолтного списка аддонов (без учёта возможных аддонов от апгрейдов)
void CSE_ALifeItemWeapon::load_def_addons_list(LPCSTR sAddonsList, ADDONS_VECTOR& m_addons_list)
{
    if (pSettings->line_exist(s_name, sAddonsList))
    {
        LPCSTR str = pSettings->r_string(s_name, sAddonsList);
        if (xr_strcmp(str, "none") != 0)
        {
            for (int i = 0, count = _GetItemCount(str); i < count; ++i)
            {
                string128 addon_section;
                _GetItem(str, i, addon_section);
                m_addons_list.push_back(addon_section);
            }
        }
    }
}

u8 CSE_ALifeItemWeapon::GetAddonsState() const
{
    u8 m_flagsAddOnState = 0;

    if (m_launcher_section != nullptr)
        m_flagsAddOnState |= CSE_ALifeItemWeapon::eWeaponAddonGrenadeLauncher;

    if (m_scope_section != nullptr)
        m_flagsAddOnState |= CSE_ALifeItemWeapon::eWeaponAddonScope;

    if (m_muzzle_section != nullptr)
        m_flagsAddOnState |= CSE_ALifeItemWeapon::eWeaponAddonSilencer;

    if (m_magaz_section != nullptr)
        m_flagsAddOnState |= CSE_ALifeItemWeapon::eWeaponAddonMagazine;

    if (m_spec_1_section != nullptr)
        m_flagsAddOnState |= CSE_ALifeItemWeapon::eWeaponAddonSpecial_1;

    if (m_spec_2_section != nullptr)
        m_flagsAddOnState |= CSE_ALifeItemWeapon::eWeaponAddonSpecial_2;

    if (m_spec_3_section != nullptr)
        m_flagsAddOnState |= CSE_ALifeItemWeapon::eWeaponAddonSpecial_3;

    if (m_spec_4_section != nullptr)
        m_flagsAddOnState |= CSE_ALifeItemWeapon::eWeaponAddonSpecial_4;

    return m_flagsAddOnState;
}

// Выставить аддоны в оружии через 8-битовый флаг (секцию аддона выбрать нельзя - всегда первая)
// bAddMode - если true, текущие аддоны не будут удалены, а лишь добавлены новые
void CSE_ALifeItemWeapon::SetAddonsState(u8 m_flagsAddOnState, bool bAddMode)
{
    if (bAddMode == false)
    {
        m_scope_section = nullptr;
        m_muzzle_section = nullptr;
        m_launcher_section = nullptr;
        m_magaz_section = nullptr;
        m_spec_1_section = nullptr;
        m_spec_2_section = nullptr;
        m_spec_3_section = nullptr;
        m_spec_4_section = nullptr;
    }

    if (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonScope)
        if (m_def_scope_list.size() > 0)
            m_scope_section = m_def_scope_list[0];

    if (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonSilencer)
        if (m_def_muzzle_list.size() > 0)
            m_muzzle_section = m_def_muzzle_list[0];

    if (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonGrenadeLauncher)
        if (m_def_launcher_list.size() > 0)
            m_launcher_section = m_def_launcher_list[0];

    if (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonMagazine)
        if (m_def_magaz_list.size() > 0)
            m_magaz_section = m_def_magaz_list[0];

    if (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonSpecial_1)
        if (m_def_spec_1_list.size() > 0)
            m_spec_1_section = m_def_spec_1_list[0];

    if (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonSpecial_2)
        if (m_def_spec_2_list.size() > 0)
            m_spec_2_section = m_def_spec_2_list[0];

    if (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonSpecial_3)
        if (m_def_spec_3_list.size() > 0)
            m_spec_3_section = m_def_spec_3_list[0];

    if (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonSpecial_4)
        if (m_def_spec_4_list.size() > 0)
            m_spec_4_section = m_def_spec_4_list[0];
}

void CSE_ALifeItemWeapon::clone_addons(CSE_ALifeItemWeapon* parent)
{
    m_scope_section = parent->m_scope_section;
    m_muzzle_section = parent->m_muzzle_section;
    m_launcher_section = parent->m_launcher_section;
    m_magaz_section = parent->m_magaz_section;
    m_spec_1_section = parent->m_spec_1_section;
    m_spec_2_section = parent->m_spec_2_section;
    m_spec_3_section = parent->m_spec_3_section;
    m_spec_4_section = parent->m_spec_4_section;

    m_def_scope_list = parent->m_def_scope_list;
    m_def_muzzle_list = parent->m_def_muzzle_list;
    m_def_launcher_list = parent->m_def_launcher_list;
    m_def_magaz_list = parent->m_def_magaz_list;
    m_def_spec_1_list = parent->m_def_spec_1_list;
    m_def_spec_2_list = parent->m_def_spec_2_list;
    m_def_spec_3_list = parent->m_def_spec_3_list;
    m_def_spec_4_list = parent->m_def_spec_4_list;
}

void CSE_ALifeItemWeapon::UPDATE_Read(NET_Packet& tNetPacket)
{
    inherited::UPDATE_Read(tNetPacket);

    if (m_wVersion < 129)
    {
        //========== Read vanila all.spawn \ save file ==========//
        u8 ammo_type;
        u16 a_elapsed;

        tNetPacket.r_float_q8(m_fCondition, 0.0f, 1.0f);
        tNetPacket.r_u8(wpn_flags);
        tNetPacket.r_u16(a_elapsed);
        tNetPacket.r_u8(m_addon_flags_sdk.flags);
        tNetPacket.r_u8(ammo_type);
        tNetPacket.r_u8(wpn_state);
        tNetPacket.r_u8(m_bZoom);

        CAmmoCompressUtil::AddAmmo(m_pAmmoMain, a_elapsed, ammo_type, true);
        CAmmoCompressUtil::AddAmmo(m_pAmmoGL, 0, 0, true);
    }
    else
    {
        //========== Read new all.spawn \ save file ==========//
        tNetPacket.r_float_q8(m_fCondition, 0.0f, 1.0f);

        tNetPacket.r_u8(wpn_flags);

        m_bGrenadeMode = !!tNetPacket.r_u8();

        CAmmoCompressUtil::UnpackAmmoFromPacket(m_pAmmoMain, tNetPacket);
        CAmmoCompressUtil::UnpackAmmoFromPacket(m_pAmmoGL, tNetPacket);

        tNetPacket.r_u8(wpn_state);
        tNetPacket.r_u8(m_bZoom);
        tNetPacket.r_u8(m_u8CurFireMode);

        tNetPacket.r_stringZ(m_scope_section);
        tNetPacket.r_stringZ(m_muzzle_section);
        tNetPacket.r_stringZ(m_launcher_section);
        tNetPacket.r_stringZ(m_magaz_section);
        tNetPacket.r_stringZ(m_spec_1_section);
        tNetPacket.r_stringZ(m_spec_2_section);
        tNetPacket.r_stringZ(m_spec_3_section);
        tNetPacket.r_stringZ(m_spec_4_section);
    }

    // Override addons with data from SDK
    SetAddonsState(m_addon_flags_sdk.get(), true);
    m_addon_flags_sdk.zero();
}

void CSE_ALifeItemWeapon::UPDATE_Write(NET_Packet& tNetPacket)
{
    inherited::UPDATE_Write(tNetPacket);

    tNetPacket.w_float_q8(m_fCondition, 0.0f, 1.0f);
    tNetPacket.w_u8(wpn_flags);

    tNetPacket.w_u8(m_bGrenadeMode ? 1 : 0);

    CAmmoCompressUtil::PackAmmoInPacket(m_pAmmoMain, tNetPacket);
    CAmmoCompressUtil::PackAmmoInPacket(m_pAmmoGL, tNetPacket);

    tNetPacket.w_u8(wpn_state);
    tNetPacket.w_u8(m_bZoom);
    tNetPacket.w_u8(m_u8CurFireMode);

    tNetPacket.w_stringZ(m_scope_section);
    tNetPacket.w_stringZ(m_muzzle_section);
    tNetPacket.w_stringZ(m_launcher_section);
    tNetPacket.w_stringZ(m_magaz_section);
    tNetPacket.w_stringZ(m_spec_1_section);
    tNetPacket.w_stringZ(m_spec_2_section);
    tNetPacket.w_stringZ(m_spec_3_section);
    tNetPacket.w_stringZ(m_spec_4_section);
}

void CSE_ALifeItemWeapon::STATE_Read(NET_Packet& tNetPacket, u16 size)
{
    inherited::STATE_Read(tNetPacket, size);

    if (m_wVersion < 129)
    {
        //========== Read vanila all.spawn \ save file ==========//
        u16 a_current_outdated;
        tNetPacket.r_u16(a_current_outdated);

        u8 ammo_type;
        u16 a_elapsed;

        tNetPacket.r_u16(a_elapsed);
        tNetPacket.r_u8(wpn_state);

        if (m_wVersion > 40)
            tNetPacket.r_u8(m_addon_flags_sdk.flags);

        if (m_wVersion > 46)
            tNetPacket.r_u8(ammo_type);

        if (m_wVersion > 122)
            u8 a_elapsed_grenades_outdated = tNetPacket.r_u8();

        CAmmoCompressUtil::AddAmmo(m_pAmmoMain, a_elapsed, ammo_type, true);
        CAmmoCompressUtil::AddAmmo(m_pAmmoGL, 0, 0, true);
    }
    else
    {
        //========== Read new all.spawn \ save file ==========//
        tNetPacket.r_u8(wpn_state);

        CAmmoCompressUtil::UnpackAmmoFromPacket(m_pAmmoMain, tNetPacket);
        CAmmoCompressUtil::UnpackAmmoFromPacket(m_pAmmoGL, tNetPacket);

        tNetPacket.r_u8(m_addon_flags_sdk.flags);

        tNetPacket.r_stringZ(m_scope_section);
        tNetPacket.r_stringZ(m_muzzle_section);
        tNetPacket.r_stringZ(m_launcher_section);
        tNetPacket.r_stringZ(m_magaz_section);
        tNetPacket.r_stringZ(m_spec_1_section);
        tNetPacket.r_stringZ(m_spec_2_section);
        tNetPacket.r_stringZ(m_spec_3_section);
        tNetPacket.r_stringZ(m_spec_4_section);
    }

    // Override ammo with data from SDK
    if (m_ammo_elapsed_sdk != u16(-1) && m_ammo_type_sdk != u8(-1))
    {
        CAmmoCompressUtil::AddAmmo(m_pAmmoMain, m_ammo_elapsed_sdk, m_ammo_type_sdk, true);
        m_ammo_elapsed_sdk = u16(-1);
        m_ammo_type_sdk = u8(-1);
    }
  
    // Override addons with data from SDK
    SetAddonsState(m_addon_flags_sdk.get(), true);
    m_addon_flags_sdk.zero();
}

void CSE_ALifeItemWeapon::STATE_Write(NET_Packet& tNetPacket)
{
    inherited::STATE_Write(tNetPacket);

    tNetPacket.w_u8(wpn_state);

    CAmmoCompressUtil::PackAmmoInPacket(m_pAmmoMain, tNetPacket);
    CAmmoCompressUtil::PackAmmoInPacket(m_pAmmoGL, tNetPacket);

    tNetPacket.w_u8(m_addon_flags_sdk.get());

    tNetPacket.w_stringZ(m_scope_section);
    tNetPacket.w_stringZ(m_muzzle_section);
    tNetPacket.w_stringZ(m_launcher_section);
    tNetPacket.w_stringZ(m_magaz_section);
    tNetPacket.w_stringZ(m_spec_1_section);
    tNetPacket.w_stringZ(m_spec_2_section);
    tNetPacket.w_stringZ(m_spec_3_section);
    tNetPacket.w_stringZ(m_spec_4_section);
}

void CSE_ALifeItemWeapon::OnEvent(NET_Packet& tNetPacket, u16 type, u32 time, ClientID sender)
{
    inherited::OnEvent(tNetPacket, type, time, sender);
    switch (type)
    {
    case GE_WPN_STATE_CHANGE:
    {
        CAmmoCompressUtil::AMMO_VECTOR pVAmmoFake;

        tNetPacket.r_u8(wpn_state);
        // u8 sub_state =
        tNetPacket.r_u8();
        //	u8 NewAmmoType =
        //	u8 AmmoElapsed =
        CAmmoCompressUtil::UnpackAmmoFromPacket(pVAmmoFake, tNetPacket);
    }
    break;
    }
}

u8 CSE_ALifeItemWeapon::get_slot() { return ((u8)pSettings->r_u8(s_name, "slot")); }
u16 CSE_ALifeItemWeapon::get_ammo_limit() { return (u16)pSettings->r_u16(s_name, "ammo_limit"); }
u16 CSE_ALifeItemWeapon::get_ammo_elapsed() { return ((u16)(CAmmoCompressUtil::GetAmmoTotal(m_pAmmoMain))); }
u16 CSE_ALifeItemWeapon::get_ammo_magsize()
{
    if (pSettings->line_exist(s_name, "ammo_mag_size"))
        return (pSettings->r_u16(s_name, "ammo_mag_size"));
    else
        return 0;
}
void CSE_ALifeItemWeapon::add_ammo(u16 iAmmoCnt, u8 iAmmoTypeIdx, bool bForGL, bool bClearFirst)
{
    CAmmoCompressUtil::AddAmmo((bForGL ? m_pAmmoGL : m_pAmmoMain), iAmmoCnt, iAmmoTypeIdx, bClearFirst);
}

void CSE_ALifeItemWeapon::clear_ammo(bool bForGL)
{
    this->add_ammo(0, 0, bForGL, true);
}

BOOL CSE_ALifeItemWeapon::Net_Relevant()
{
    if (wpn_flags == 1)
        return TRUE;

    return inherited::Net_Relevant();
}

#ifndef XRGAME_EXPORTS
void CSE_ALifeItemWeapon::FillProps(LPCSTR pref, PropItemVec& items)
{
    inherited::FillProps(pref, items);
        
    PHelper().CreateU8(items, PrepareKey(pref, *s_name, "Ammo type:"), &m_ammo_type_sdk, 0, 255, 1);
    PHelper().CreateU16(items, PrepareKey(pref, *s_name, "Ammo: in magazine"), &m_ammo_elapsed_sdk, 0, 30, 1);

    if (m_scope_status == ALife::eAddonAttachable)
        PHelper().CreateFlag8(
            items, PrepareKey(pref, *s_name, "Addons" DELIMITER "Scope"), &m_addon_flags_sdk, eWeaponAddonScope);

    if (m_silencer_status == ALife::eAddonAttachable)
        PHelper().CreateFlag8(
            items, PrepareKey(pref, *s_name, "Addons" DELIMITER "Silencer"), &m_addon_flags_sdk, eWeaponAddonSilencer);

    if (m_grenade_launcher_status == ALife::eAddonAttachable)
        PHelper().CreateFlag8(items, PrepareKey(pref, *s_name, "Addons" DELIMITER "Podstvolnik"), &m_addon_flags_sdk,
            eWeaponAddonGrenadeLauncher);
}
#endif // #ifndef XRGAME_EXPORTS

////////////////////////////////////////////////////////////////////////////
// CSE_ALifeItemWeaponShotGun //--#SM+#--
////////////////////////////////////////////////////////////////////////////
CSE_ALifeItemWeaponShotGun::CSE_ALifeItemWeaponShotGun(LPCSTR caSection) : CSE_ALifeItemWeaponMagazined(caSection) {}

CSE_ALifeItemWeaponShotGun::~CSE_ALifeItemWeaponShotGun() {}
void CSE_ALifeItemWeaponShotGun::UPDATE_Read(NET_Packet& P)
{
    inherited::UPDATE_Read(P);

    if (m_wVersion < 129)
    {
        //========== Read vanila all.spawn \ save file ==========//
        xr_vector<u8> m_AmmoIDs;
        m_AmmoIDs.clear();

        u8 AmmoCount = P.r_u8();
        for (u8 i = 0; i < AmmoCount; i++)
        {
            m_AmmoIDs.push_back(P.r_u8()); // Not used, for backward compatibility only
        }
    }
}
void CSE_ALifeItemWeaponShotGun::UPDATE_Write(NET_Packet& P) { inherited::UPDATE_Write(P); }
void CSE_ALifeItemWeaponShotGun::STATE_Read(NET_Packet& P, u16 size) { inherited::STATE_Read(P, size); }
void CSE_ALifeItemWeaponShotGun::STATE_Write(NET_Packet& P) { inherited::STATE_Write(P); }
#ifndef XRGAME_EXPORTS
void CSE_ALifeItemWeaponShotGun::FillProps(LPCSTR pref, PropItemVec& items) { inherited::FillProps(pref, items); }
#endif // #ifndef XRGAME_EXPORTS
////////////////////////////////////////////////////////////////////////////
// CSE_ALifeItemWeaponAutoShotGun
////////////////////////////////////////////////////////////////////////////
CSE_ALifeItemWeaponAutoShotGun::CSE_ALifeItemWeaponAutoShotGun(LPCSTR caSection) : CSE_ALifeItemWeaponShotGun(caSection)
{
}

CSE_ALifeItemWeaponAutoShotGun::~CSE_ALifeItemWeaponAutoShotGun() {}
void CSE_ALifeItemWeaponAutoShotGun::UPDATE_Read(NET_Packet& P) { inherited::UPDATE_Read(P); }
void CSE_ALifeItemWeaponAutoShotGun::UPDATE_Write(NET_Packet& P) { inherited::UPDATE_Write(P); }
void CSE_ALifeItemWeaponAutoShotGun::STATE_Read(NET_Packet& P, u16 size) { inherited::STATE_Read(P, size); }
void CSE_ALifeItemWeaponAutoShotGun::STATE_Write(NET_Packet& P) { inherited::STATE_Write(P); }
#ifndef XRGAME_EXPORTS
void CSE_ALifeItemWeaponAutoShotGun::FillProps(LPCSTR pref, PropItemVec& items) { inherited::FillProps(pref, items); }
#endif // #ifndef XRGAME_EXPORTS
////////////////////////////////////////////////////////////////////////////
// CSE_ALifeItemWeaponMagazined //--#SM+#--
////////////////////////////////////////////////////////////////////////////
CSE_ALifeItemWeaponMagazined::CSE_ALifeItemWeaponMagazined(LPCSTR caSection) : CSE_ALifeItemWeapon(caSection) {}

CSE_ALifeItemWeaponMagazined::~CSE_ALifeItemWeaponMagazined() {}
void CSE_ALifeItemWeaponMagazined::UPDATE_Read(NET_Packet& P)
{
    inherited::UPDATE_Read(P);

    if (m_wVersion < 129)
    {
        //========== Read vanila all.spawn \ save file ==========//
        m_u8CurFireMode = P.r_u8();
    }
}
void CSE_ALifeItemWeaponMagazined::UPDATE_Write(NET_Packet& P) { inherited::UPDATE_Write(P); }
void CSE_ALifeItemWeaponMagazined::STATE_Read(NET_Packet& P, u16 size) { inherited::STATE_Read(P, size); }
void CSE_ALifeItemWeaponMagazined::STATE_Write(NET_Packet& P) { inherited::STATE_Write(P); }
#ifndef XRGAME_EXPORTS
void CSE_ALifeItemWeaponMagazined::FillProps(LPCSTR pref, PropItemVec& items) { inherited::FillProps(pref, items); }
#endif // #ifndef XRGAME_EXPORTS

////////////////////////////////////////////////////////////////////////////
// CSE_ALifeItemWeaponMagazinedWGL //--#SM+#--
////////////////////////////////////////////////////////////////////////////
CSE_ALifeItemWeaponMagazinedWGL::CSE_ALifeItemWeaponMagazinedWGL(LPCSTR caSection)
    : CSE_ALifeItemWeaponMagazined(caSection)
{
}

CSE_ALifeItemWeaponMagazinedWGL::~CSE_ALifeItemWeaponMagazinedWGL() {}
void CSE_ALifeItemWeaponMagazinedWGL::UPDATE_Read(NET_Packet& P)
{
    if (m_wVersion < 129)
    {
        //========== Read vanila all.spawn \ save file ==========//
        m_bGrenadeMode = !!P.r_u8();
    }

    inherited::UPDATE_Read(P);
}
void CSE_ALifeItemWeaponMagazinedWGL::UPDATE_Write(NET_Packet& P) { inherited::UPDATE_Write(P); }
void CSE_ALifeItemWeaponMagazinedWGL::STATE_Read(NET_Packet& P, u16 size) { inherited::STATE_Read(P, size); }
void CSE_ALifeItemWeaponMagazinedWGL::STATE_Write(NET_Packet& P) { inherited::STATE_Write(P); }
#ifndef XRGAME_EXPORTS
void CSE_ALifeItemWeaponMagazinedWGL::FillProps(LPCSTR pref, PropItemVec& items) { inherited::FillProps(pref, items); }
#endif // #ifndef XRGAME_EXPORTS

////////////////////////////////////////////////////////////////////////////
// CSE_ALifeItemAmmo
////////////////////////////////////////////////////////////////////////////
CSE_ALifeItemAmmo::CSE_ALifeItemAmmo(LPCSTR caSection) : CSE_ALifeItem(caSection)
{
    a_elapsed = m_boxSize = (u16)pSettings->r_s32(caSection, "box_size");
    if (pSettings->section_exist(caSection) && pSettings->line_exist(caSection, "visual"))
        set_visual(pSettings->r_string(caSection, "visual"));
}

CSE_ALifeItemAmmo::~CSE_ALifeItemAmmo() {}
void CSE_ALifeItemAmmo::STATE_Read(NET_Packet& tNetPacket, u16 size)
{
    inherited::STATE_Read(tNetPacket, size);
    tNetPacket.r_u16(a_elapsed);
}

void CSE_ALifeItemAmmo::STATE_Write(NET_Packet& tNetPacket)
{
    inherited::STATE_Write(tNetPacket);
    tNetPacket.w_u16(a_elapsed);
}

void CSE_ALifeItemAmmo::UPDATE_Read(NET_Packet& tNetPacket)
{
    inherited::UPDATE_Read(tNetPacket);

    tNetPacket.r_u16(a_elapsed);
}

void CSE_ALifeItemAmmo::UPDATE_Write(NET_Packet& tNetPacket)
{
    inherited::UPDATE_Write(tNetPacket);

    tNetPacket.w_u16(a_elapsed);
}

#ifndef XRGAME_EXPORTS
void CSE_ALifeItemAmmo::FillProps(LPCSTR pref, PropItemVec& values)
{
    inherited::FillProps(pref, values);
    PHelper().CreateU16(values, PrepareKey(pref, *s_name, "Ammo: left"), &a_elapsed, 0, m_boxSize, m_boxSize);
}
#endif // #ifndef XRGAME_EXPORTS

bool CSE_ALifeItemAmmo::can_switch_online() const /* noexcept */ { return inherited::can_switch_online(); }
bool CSE_ALifeItemAmmo::can_switch_offline() const /* noexcept */
{
    return (inherited::can_switch_offline() && a_elapsed != 0);
}
////////////////////////////////////////////////////////////////////////////
// CSE_ALifeItemDetector
////////////////////////////////////////////////////////////////////////////
CSE_ALifeItemDetector::CSE_ALifeItemDetector(LPCSTR caSection) : CSE_ALifeItem(caSection)
{
    m_ef_detector_type = pSettings->r_u32(caSection, "ef_detector_type");
}

CSE_ALifeItemDetector::~CSE_ALifeItemDetector() {}
u32 CSE_ALifeItemDetector::ef_detector_type() const { return (m_ef_detector_type); }
void CSE_ALifeItemDetector::STATE_Read(NET_Packet& tNetPacket, u16 size)
{
    if (m_wVersion > 20)
        inherited::STATE_Read(tNetPacket, size);
}

void CSE_ALifeItemDetector::STATE_Write(NET_Packet& tNetPacket) { inherited::STATE_Write(tNetPacket); }
void CSE_ALifeItemDetector::UPDATE_Read(NET_Packet& tNetPacket) { inherited::UPDATE_Read(tNetPacket); }
void CSE_ALifeItemDetector::UPDATE_Write(NET_Packet& tNetPacket) { inherited::UPDATE_Write(tNetPacket); }
#ifndef XRGAME_EXPORTS
void CSE_ALifeItemDetector::FillProps(LPCSTR pref, PropItemVec& items) { inherited::FillProps(pref, items); }
#endif // #ifndef XRGAME_EXPORTS

////////////////////////////////////////////////////////////////////////////
// CSE_ALifeItemDetector
////////////////////////////////////////////////////////////////////////////
CSE_ALifeItemArtefact::CSE_ALifeItemArtefact(LPCSTR caSection) : CSE_ALifeItem(caSection) { m_fAnomalyValue = 100.f; }
CSE_ALifeItemArtefact::~CSE_ALifeItemArtefact() {}
void CSE_ALifeItemArtefact::STATE_Read(NET_Packet& tNetPacket, u16 size) { inherited::STATE_Read(tNetPacket, size); }
void CSE_ALifeItemArtefact::STATE_Write(NET_Packet& tNetPacket) { inherited::STATE_Write(tNetPacket); }
void CSE_ALifeItemArtefact::UPDATE_Read(NET_Packet& tNetPacket) { inherited::UPDATE_Read(tNetPacket); }
void CSE_ALifeItemArtefact::UPDATE_Write(NET_Packet& tNetPacket) { inherited::UPDATE_Write(tNetPacket); }
#ifndef XRGAME_EXPORTS
void CSE_ALifeItemArtefact::FillProps(LPCSTR pref, PropItemVec& items)
{
    inherited::FillProps(pref, items);
    PHelper().CreateFloat(items, PrepareKey(pref, *s_name, "Anomaly value:"), &m_fAnomalyValue, 0.f, 200.f);
}
#endif // #ifndef XRGAME_EXPORTS

BOOL CSE_ALifeItemArtefact::Net_Relevant()
{
    if (base()->ID_Parent == u16(-1))
        return TRUE;

    return FALSE;
}

////////////////////////////////////////////////////////////////////////////
// CSE_ALifeItemPDA
////////////////////////////////////////////////////////////////////////////
CSE_ALifeItemPDA::CSE_ALifeItemPDA(LPCSTR caSection) : CSE_ALifeItem(caSection)
{
    m_original_owner = 0xffff;
    m_specific_character = NULL;
    m_info_portion = NULL;
}

CSE_ALifeItemPDA::~CSE_ALifeItemPDA() {}
void CSE_ALifeItemPDA::STATE_Read(NET_Packet& tNetPacket, u16 size)
{
    inherited::STATE_Read(tNetPacket, size);
    if (m_wVersion > 58)
        tNetPacket.r_u16(m_original_owner);

    if (m_wVersion > 89)

        if ((m_wVersion > 89) && (m_wVersion < 98))
        {
            int tmp, tmp2;
            tNetPacket.r(&tmp, sizeof(int));
            tNetPacket.r(&tmp2, sizeof(int));
            m_info_portion = NULL;
            m_specific_character = NULL;
        }
        else
        {
            tNetPacket.r_stringZ(m_specific_character);
            tNetPacket.r_stringZ(m_info_portion);
        }
}

void CSE_ALifeItemPDA::STATE_Write(NET_Packet& tNetPacket)
{
    inherited::STATE_Write(tNetPacket);
    tNetPacket.w_u16(m_original_owner);
#ifdef XRGAME_EXPORTS
    tNetPacket.w_stringZ(m_specific_character);
    tNetPacket.w_stringZ(m_info_portion);
#else
    shared_str tmp_1 = NULL;
    shared_str tmp_2 = NULL;

    tNetPacket.w_stringZ(tmp_1);
    tNetPacket.w_stringZ(tmp_2);
#endif
}

void CSE_ALifeItemPDA::UPDATE_Read(NET_Packet& tNetPacket) { inherited::UPDATE_Read(tNetPacket); }
void CSE_ALifeItemPDA::UPDATE_Write(NET_Packet& tNetPacket) { inherited::UPDATE_Write(tNetPacket); }
#ifndef XRGAME_EXPORTS
void CSE_ALifeItemPDA::FillProps(LPCSTR pref, PropItemVec& items) { inherited::FillProps(pref, items); }
#endif // #ifndef XRGAME_EXPORTS

////////////////////////////////////////////////////////////////////////////
// CSE_ALifeItemDocument
////////////////////////////////////////////////////////////////////////////
CSE_ALifeItemDocument::CSE_ALifeItemDocument(LPCSTR caSection) : CSE_ALifeItem(caSection) { m_wDoc = NULL; }
CSE_ALifeItemDocument::~CSE_ALifeItemDocument() {}
void CSE_ALifeItemDocument::STATE_Read(NET_Packet& tNetPacket, u16 size)
{
    inherited::STATE_Read(tNetPacket, size);

    if (m_wVersion < 98)
    {
        u16 tmp;
        tNetPacket.r_u16(tmp);
        m_wDoc = NULL;
    }
    else
        tNetPacket.r_stringZ(m_wDoc);
}

void CSE_ALifeItemDocument::STATE_Write(NET_Packet& tNetPacket)
{
    inherited::STATE_Write(tNetPacket);
    tNetPacket.w_stringZ(m_wDoc);
}

void CSE_ALifeItemDocument::UPDATE_Read(NET_Packet& tNetPacket) { inherited::UPDATE_Read(tNetPacket); }
void CSE_ALifeItemDocument::UPDATE_Write(NET_Packet& tNetPacket) { inherited::UPDATE_Write(tNetPacket); }
#ifndef XRGAME_EXPORTS
void CSE_ALifeItemDocument::FillProps(LPCSTR pref, PropItemVec& items)
{
    inherited::FillProps(pref, items);
    //	PHelper().CreateU16			(items, PrepareKey(pref, *s_name, "Document index :"), &m_wDocIndex, 0, 65535);
    PHelper().CreateRText(items, PrepareKey(pref, *s_name, "Info portion :"), &m_wDoc);
}
#endif // #ifndef XRGAME_EXPORTS

////////////////////////////////////////////////////////////////////////////
// CSE_ALifeItemGrenade
////////////////////////////////////////////////////////////////////////////
CSE_ALifeItemGrenade::CSE_ALifeItemGrenade(LPCSTR caSection) : CSE_ALifeItem(caSection)
{
    m_ef_weapon_type = READ_IF_EXISTS(pSettings, r_u32, caSection, "ef_weapon_type", u32(-1));
}

CSE_ALifeItemGrenade::~CSE_ALifeItemGrenade() {}
u32 CSE_ALifeItemGrenade::ef_weapon_type() const
{
    VERIFY(m_ef_weapon_type != u32(-1));
    return (m_ef_weapon_type);
}

void CSE_ALifeItemGrenade::STATE_Read(NET_Packet& tNetPacket, u16 size) { inherited::STATE_Read(tNetPacket, size); }
void CSE_ALifeItemGrenade::STATE_Write(NET_Packet& tNetPacket) { inherited::STATE_Write(tNetPacket); }
void CSE_ALifeItemGrenade::UPDATE_Read(NET_Packet& tNetPacket) { inherited::UPDATE_Read(tNetPacket); }
void CSE_ALifeItemGrenade::UPDATE_Write(NET_Packet& tNetPacket) { inherited::UPDATE_Write(tNetPacket); }
#ifndef XRGAME_EXPORTS
void CSE_ALifeItemGrenade::FillProps(LPCSTR pref, PropItemVec& items) { inherited::FillProps(pref, items); }
#endif // #ifndef XRGAME_EXPORTS

////////////////////////////////////////////////////////////////////////////
// CSE_ALifeItemExplosive
////////////////////////////////////////////////////////////////////////////
CSE_ALifeItemExplosive::CSE_ALifeItemExplosive(LPCSTR caSection) : CSE_ALifeItem(caSection) {}
CSE_ALifeItemExplosive::~CSE_ALifeItemExplosive() {}
void CSE_ALifeItemExplosive::STATE_Read(NET_Packet& tNetPacket, u16 size) { inherited::STATE_Read(tNetPacket, size); }
void CSE_ALifeItemExplosive::STATE_Write(NET_Packet& tNetPacket) { inherited::STATE_Write(tNetPacket); }
void CSE_ALifeItemExplosive::UPDATE_Read(NET_Packet& tNetPacket) { inherited::UPDATE_Read(tNetPacket); }
void CSE_ALifeItemExplosive::UPDATE_Write(NET_Packet& tNetPacket) { inherited::UPDATE_Write(tNetPacket); }
#ifndef XRGAME_EXPORTS
void CSE_ALifeItemExplosive::FillProps(LPCSTR pref, PropItemVec& items) { inherited::FillProps(pref, items); }
#endif // #ifndef XRGAME_EXPORTS

////////////////////////////////////////////////////////////////////////////
// CSE_ALifeItemBolt
////////////////////////////////////////////////////////////////////////////
CSE_ALifeItemBolt::CSE_ALifeItemBolt(LPCSTR caSection) : CSE_ALifeItem(caSection)
{
    m_flags.set(flUseSwitches, false);
    m_flags.set(flSwitchOffline, false);
    m_ef_weapon_type = READ_IF_EXISTS(pSettings, r_u32, caSection, "ef_weapon_type", u32(-1));
}

CSE_ALifeItemBolt::~CSE_ALifeItemBolt() {}
u32 CSE_ALifeItemBolt::ef_weapon_type() const
{
    VERIFY(m_ef_weapon_type != u32(-1));
    return (m_ef_weapon_type);
}

void CSE_ALifeItemBolt::STATE_Write(NET_Packet& tNetPacket) { inherited::STATE_Write(tNetPacket); }
void CSE_ALifeItemBolt::STATE_Read(NET_Packet& tNetPacket, u16 size) { inherited::STATE_Read(tNetPacket, size); }
void CSE_ALifeItemBolt::UPDATE_Write(NET_Packet& tNetPacket) { inherited::UPDATE_Write(tNetPacket); };
void CSE_ALifeItemBolt::UPDATE_Read(NET_Packet& tNetPacket) { inherited::UPDATE_Read(tNetPacket); };
bool CSE_ALifeItemBolt::can_save() const /* noexcept */
{
    return false; //! attached());
}
bool CSE_ALifeItemBolt::used_ai_locations() const /* noexcept */ { return false; }
#ifndef XRGAME_EXPORTS
void CSE_ALifeItemBolt::FillProps(LPCSTR pref, PropItemVec& values) { inherited::FillProps(pref, values); }
#endif // #ifndef XRGAME_EXPORTS

////////////////////////////////////////////////////////////////////////////
// CSE_ALifeItemCustomOutfit
////////////////////////////////////////////////////////////////////////////
CSE_ALifeItemCustomOutfit::CSE_ALifeItemCustomOutfit(LPCSTR caSection) : CSE_ALifeItem(caSection)
{
    m_ef_equipment_type = pSettings->r_u32(caSection, "ef_equipment_type");
}

CSE_ALifeItemCustomOutfit::~CSE_ALifeItemCustomOutfit() {}
u32 CSE_ALifeItemCustomOutfit::ef_equipment_type() const { return (m_ef_equipment_type); }
void CSE_ALifeItemCustomOutfit::STATE_Read(NET_Packet& tNetPacket, u16 size)
{
    inherited::STATE_Read(tNetPacket, size);
}

void CSE_ALifeItemCustomOutfit::STATE_Write(NET_Packet& tNetPacket) { inherited::STATE_Write(tNetPacket); }
void CSE_ALifeItemCustomOutfit::UPDATE_Read(NET_Packet& tNetPacket)
{
    inherited::UPDATE_Read(tNetPacket);
    tNetPacket.r_float_q8(m_fCondition, 0.0f, 1.0f);
}

void CSE_ALifeItemCustomOutfit::UPDATE_Write(NET_Packet& tNetPacket)
{
    inherited::UPDATE_Write(tNetPacket);
    tNetPacket.w_float_q8(m_fCondition, 0.0f, 1.0f);
}

#ifndef XRGAME_EXPORTS
void CSE_ALifeItemCustomOutfit::FillProps(LPCSTR pref, PropItemVec& items) { inherited::FillProps(pref, items); }
#endif // #ifndef XRGAME_EXPORTS

BOOL CSE_ALifeItemCustomOutfit::Net_Relevant() { return (true); }
////////////////////////////////////////////////////////////////////////////
// CSE_ALifeItemHelmet
////////////////////////////////////////////////////////////////////////////
CSE_ALifeItemHelmet::CSE_ALifeItemHelmet(LPCSTR caSection) : CSE_ALifeItem(caSection) {}
CSE_ALifeItemHelmet::~CSE_ALifeItemHelmet() {}
void CSE_ALifeItemHelmet::STATE_Read(NET_Packet& tNetPacket, u16 size) { inherited::STATE_Read(tNetPacket, size); }
void CSE_ALifeItemHelmet::STATE_Write(NET_Packet& tNetPacket) { inherited::STATE_Write(tNetPacket); }
void CSE_ALifeItemHelmet::UPDATE_Read(NET_Packet& tNetPacket)
{
    inherited::UPDATE_Read(tNetPacket);
    tNetPacket.r_float_q8(m_fCondition, 0.0f, 1.0f);
}

void CSE_ALifeItemHelmet::UPDATE_Write(NET_Packet& tNetPacket)
{
    inherited::UPDATE_Write(tNetPacket);
    tNetPacket.w_float_q8(m_fCondition, 0.0f, 1.0f);
}

#ifndef XRGAME_EXPORTS
void CSE_ALifeItemHelmet::FillProps(LPCSTR pref, PropItemVec& items) { inherited::FillProps(pref, items); }
#endif // #ifndef XRGAME_EXPORTS

BOOL CSE_ALifeItemHelmet::Net_Relevant() { return (true); }
