/**************************************/
/***** Состояние "Смена магазина" *****/ //--#SM+#--
/**************************************/

#include "StdAfx.h"
#include "Weapon.h"

// Полноценно использовать магазинное питание может только игрок <!>
// Логику для НПС потом можно сделать в CObjectActionReload()

// Пробуем начать смену магазина на клиенте
bool CWeapon::Try2SwitchMag(bool bCheckOnlyMode, bool bFromInv)
{
    if (!bCheckOnlyMode && GetState() == eSwitchMag)
        return false;
    if (IsPending() == true)
        return false;
    if (m_bUseMagazines == false)
        return false;
    if (m_bGrenadeMode == true)
        return false;

    // Иначе просто перенаправляем вызов
    if (IsMisfire()) //--> Если у оружия осечка, то её требуется исправить
        m_sub_state = eSubstateMagazMisfire;

    // Если мы не меняем тип магазина\фиксим осечку, то проводим доп. проверки
    if (m_sub_state != eSubstateMagazMisfire &&          //--> Осечка
        m_set_next_magaz_on_reload == empty_addon_idx && //--> Тип магазина
        m_set_next_magaz_by_id == u16(-1) &&
        (bFromInv == false && m_sub_state != eSubstateMagazDetach) && //--> Снятие магазина
        GetAddonBySlot(eMagaz)->bActive == true)
    {
        // Проверяем что у нас не полный магазин
        if (iAmmoElapsed >= iMagazineSize)
            return false;

        // Проверяем есть-ли вообще магазин на замену
        CWeapon* pNewMagaz = GetBestMagazine();
        if (pNewMagaz == NULL)
            return false;

        // И что в нём магазине патронов больше чем в текущем
        if (pNewMagaz->GetMainAmmoElapsed() <= iAmmoElapsed)
            return false;
    }

    // Оружие в заклинившем состоянии
    if (m_sub_state == eSubstateMagazMisfire)
    {
        SAddonData* pCurMagaz = GetAddonBySlot(eMagaz);
        if (pCurMagaz->bActive)
        { //--> Нельзя исправить клин перезарядкой, если в текущем магазине слишком мало патронов
            u32 iMinRequiredAmmoInMag =
                READ_IF_EXISTS(pSettings, r_u32, pCurMagaz->GetAddonName(), WEAPON_MRAIM_L, WEAPON_MRAIM_DEF_VAL);
            if (GetMainAmmoElapsed() < iMinRequiredAmmoInMag)
            {
                //--> Убираем осечку просто так
                bMisfire = false;

                //--> Не запускаем анимацию
                m_sub_state = eSubstateMagazFinish;
                return false;
            }
        }
    }

    if (!bCheckOnlyMode)
        SwitchState(eSwitchMag);

    return true;
}

// Принудительная смена магазина
void CWeapon::Need2SwitchMag() { SwitchState(eSwitchMag); }

// Нужно остановить смену магазина на клиенте
void CWeapon::Need2Stop_SwitchMag()
{
    if (GetState() != eSwitchMag)
        return;
    Need2Idle();
}

// Переключение стэйта на "Смена магазина"
void CWeapon::switch2_SwitchMag()
{
    // Требуется завершить смену магазина
    if (m_sub_state == eSubstateMagazFinish)
    {
        bMisfire = false;
        goto label_SwitchMag_End;
    }

    // Повторная проверка для МП, где вызов стэйтов идёт в обход Try-функций
    if (m_sub_state == eSubstateReloadBegin ||
        m_sub_state == eSubstateMagazDetach) // Если мы уже в процессе смены магазина, то игнорируем этот участок
    {
        if (!Try2SwitchMag(true))
            goto label_SwitchMag_End;

        m_overridenAmmoForReloadAnm = GetMainAmmoElapsed();
    }

    OnZoomOut();

    // Получаем текущий магазин
    SAddonData* pAddonMagaz = GetAddonBySlot(eMagaz);

    // Возможно нам требуется смеить тип магазина
    LPCSTR sMagazSection = NULL;
    if (m_set_next_magaz_on_reload != empty_addon_idx) //--> idx нового магазина
    {
        sMagazSection = pAddonMagaz->GetAddonNameByIdx(m_set_next_magaz_on_reload).c_str();
    }

    // Обрабатываем стэйты смены магазина
    CWeapon* pNewMagaz = NULL;

    if (m_set_next_magaz_by_id == u16(-1))
        pNewMagaz = GetBestMagazine(sMagazSection);
    else
    {
        IGameObject* pObject = Level().Objects.net_Find(m_set_next_magaz_by_id);
        R_ASSERT(pObject != NULL);

        pNewMagaz = smart_cast<CWeapon*>(pObject);
        R_ASSERT(pNewMagaz != NULL);

        if (pNewMagaz->H_Parent() == NULL || pNewMagaz->H_Parent() != this->H_Parent()) //--> На случаи если мы успели потерять магазин
            goto label_SwitchMag_End;

        R_ASSERT(m_set_next_magaz_on_reload != empty_addon_idx);
        R_ASSERT(pNewMagaz->cNameSect() == sMagazSection);
    }

    switch (m_sub_state)
    {
    case eSubstateMagazMisfire:             //--> Требуется исправить осечку (просто играем анимацию)
        m_sub_state = eSubstateMagazFinish;
        break;
    ////////////////////////////////////////////////////////////////////////////////////////////////
    case eSubstateMagazDetach: //--> Требуется снять текущий магазин
        m_sub_state = eSubstateMagazDetach_Do;
        break;
    ////////////////////////////////////////////////////////////////////////////////////////////////
    case eSubstateReloadBegin: //--> Требуется установить другой магазин любого типа
        if (pNewMagaz == NULL)
            goto label_SwitchMag_End;
        m_sub_state = eSubstateMagazSwitch;
        break;
    ///////////////////////////////////////////////////////////////////////////////////////////////
    case eSubstateMagazDetach_Do:
    case eSubstateMagazSwitch:
        // Убираем осечку
        bMisfire = false;

        // Снимаем старый
        if (pAddonMagaz->bActive)
            DetachMagazine(pAddonMagaz->GetAddonName().c_str(), true, false);

        if (m_sub_state == eSubstateMagazDetach_Do || pNewMagaz == NULL)
        {
            m_sub_state = eSubstateMagazFinish;
            break;
        }

        // Устанавливаем новый
        u8      addon_idx = 0;
        EAddons iSlot     = GetAddonSlot(pNewMagaz->cNameSect_str(), &addon_idx);
        if (iSlot == eMagaz)
        {
            AttachMagazine(pNewMagaz, true, false);
        }
        else
            goto label_SwitchMag_End;

        m_sub_state = eSubstateMagazFinish;
        break;
    };

    PlayAnimReload();
    SetPending(TRUE);
    return;

// Не нашли подходящего действия
label_SwitchMag_End:
    Need2Idle();
    return;
}

// Переключение на другой стэйт из стэйта "Смена магазина"
void CWeapon::switchFrom_SwitchMag(u32 newS)
{
    if (newS != eSwitchMag)
    {
        m_set_next_magaz_on_reload  = empty_addon_idx;
        m_set_next_magaz_by_id      = u16(-1);
        m_sub_state                 = eSubstateReloadBegin;
        m_overridenAmmoForReloadAnm = -1;
    }
}

// Обновление оружия в состоянии "Смена магазина"
void CWeapon::state_SwitchMag(float dt) {}

////////////////////////////////////////////////////////////////////
// ************************************************************** //
////////////////////////////////////////////////////////////////////

// Получить наилучший (по кол-ву патронов) магазин текущего типа (или заданной секции) из инвентаря
CWeapon* CWeapon::GetBestMagazine(LPCSTR section)
{
    if (!m_bUseMagazines)
        return NULL;

    SAddonData* pAddonMagaz = GetAddonBySlot(eMagaz);
    if (pAddonMagaz->bActive == false && section == NULL)
        return NULL;

    LPCSTR sCurMagazName;
    if (section == NULL)
        sCurMagazName = pAddonMagaz->GetAddonName().c_str(); //--> Секция текущего магазина
    else
        sCurMagazName = section; //--> Переданная секция

    // Производим поиск всех магазинов заданного типа
    TIItemContainer::iterator itb = m_pInventory->m_ruck.begin();
    TIItemContainer::iterator ite = m_pInventory->m_ruck.end();

    CWeapon* pMagazBest = NULL; //--> Наилучший магазин
    for (; itb != ite; ++itb)
    {
        CWeapon* pMagaz = (*itb)->cast_weapon();
        if (pMagaz && (pMagaz->cNameSect().equal(sCurMagazName)))
        { //--> Ищем магазин с наибольшим числом патронов
            if (pMagazBest == NULL || (pMagaz->GetMainAmmoElapsed() > pMagazBest->GetMainAmmoElapsed()))
                pMagazBest = pMagaz;
        }
    }

    // В найденном магазине патронов должно быть больше необходимого минимума
    if (pMagazBest != NULL && pMagazBest->HaveMinRequiredAmmoInMag() == false)
        return NULL; //--> В текущей логике pMagazBest - всегда магазин с наибольшим числом патронов

    return pMagazBest;
}

///////////////////////////////////////////

// Попытаться сменить текущий тип используемых магазинов
bool CWeapon::SwitchMagazineType()
{
    if (OnClient())
        return false;

    if (m_bUseMagazines == false)
        return false;

    if (GetState() == eSwitchMag)
        return false;

    SAddonData* pAddon          = GetAddonBySlot(eMagaz);
    u32         totalMagazTypes = pAddon->m_addons_list.size();

    if (totalMagazTypes == 0)
        return false; //--> Список магазинов пуст

    // Пробуем найти другие магазины в инвентаре
    u8     l_newType = pAddon->addon_idx;
    LPCSTR sMagazSection;

    u8 typeIdx = 0;
    for (; typeIdx < totalMagazTypes; typeIdx++)
    {
        l_newType     = u8((u32(l_newType + 1)) % totalMagazTypes);
        sMagazSection = pAddon->GetAddonNameByIdx(l_newType).c_str();

        if (m_pInventory->GetAny(sMagazSection) != NULL)
        {
            CWeapon* pMagaz = GetBestMagazine(sMagazSection);
            if (pMagaz != NULL && pMagaz->iAmmoElapsed > 0)
                break; // Нашли подходящий тип в инвентаре
        }
    }

    if (typeIdx != totalMagazTypes)
    {
        // Начинаем смену
        m_set_next_magaz_on_reload = l_newType;

        if (OnServer())
        {
            if (GetState() != eSwitchMag)
                if (!Try2SwitchMag())
                    m_set_next_magaz_on_reload = empty_addon_idx;
        }

        return true;
    }

    return false;
}
