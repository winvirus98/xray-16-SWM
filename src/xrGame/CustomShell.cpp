/*******************************/
/********** 3D-Гильзы **********/ //--#SM+#--
/*******************************/

#include "StdAfx.h"
#include "CustomShell.h"
#include "HUDManager.h"
#include "xrPhysics/ExtendedGeom.h"
#include "xrEngine/vis_object_data.h"
#include "Include/xrRender/ParticleCustom.h"

//======= Описание класса и основные наследуемые методы =======//

CCustomShell::CCustomShell()
{
    m_bIsParticlesAutoRemoved = false;
    m_vPrevVel.set(0, 0, 0);

    m_bShellLightsEnabled = false;
    m_pShellLight = nullptr;

    dwHUDMode_upd_frame = u32(-1);
    m_bHUD_mode = false;

    m_dwDestroyTime = 0;
    m_dwFOVTranslateTime = 0;
    m_dwFOVStableTime = 0;

    m_launch_point_idx = 0;
}

CCustomShell::~CCustomShell() { m_pShellLight.destroy(); }

void CCustomShell::Load(LPCSTR section)
{
    inherited::Load(section);
    reload(section);
}

void CCustomShell::reload(LPCSTR section)
{
    inherited::reload(section);

    m_bShellLightsEnabled = !!pSettings->r_bool(section, "lights_enabled");
    if (m_bShellLightsEnabled)
    {
        // clang-format off
        sscanf(pSettings->r_string(section, "shell_light_color"), "%f,%f,%f",
            &m_ShellLightColor.r,
            &m_ShellLightColor.g,
            &m_ShellLightColor.b);
        // clang-format on

        m_fShellLightRange = pSettings->r_float(section, "shell_light_range");
        m_fShellLightTime = pSettings->r_float(section, "shell_light_time");
    }

    if (pSettings->line_exist(section, "shell_particles"))
        m_sShellParticles = pSettings->r_string(section, "shell_particles");
}

BOOL CCustomShell::net_Spawn(CSE_Abstract* DC)
{
    BOOL bRes = inherited::net_Spawn(DC);

    // Добавляем объект в список рендера HUD-а
    HUD().RenderHUD_Add(this);

    // Считываем индекс Launch Point-а, из которого мы вылетели, из имени объекта
    shared_str sLP_idx = DC->name_replace();
    R_ASSERT(sLP_idx != nullptr && sLP_idx != "");

    m_launch_point_idx = atoi(sLP_idx.c_str());
    R_ASSERT(m_launch_point_idx > 0);

    return bRes;
}

void CCustomShell::net_Destroy()
{
    // Удаляем объект из списка рендера HUD-а
    HUD().RenderHUD_Remove(this);

    // Останавливаем партиклы и свечение
    StopShellParticles(true);
    StopShellLights();

    CPHUpdateObject::Deactivate();
    inherited::net_Destroy();
}

// Вызывается из родительского NetSpawn, после вызова reload() там-же
void CCustomShell::reinit()
{
    inherited::reinit();

    // Создаём источник света
    if (m_pShellLight != nullptr)
        m_pShellLight.destroy();

    m_pShellLight = GEnv.Render->light_create();
    m_pShellLight->set_shadow(true);

    // Подготавливаем параметры партиклов
    m_pShellParticles = nullptr;
    m_vPrevVel.set(0, 0, 0);
}

// Вызывается после присоединения гильзы к родителю
void CCustomShell::OnH_A_Chield()
{
    inherited::OnH_A_Chield();

    CShellLauncher* pLauncher = GetLauncher();

    m_dwRegisterTime = Device.dwTimeGlobal;

    // Запускаем эффекты
    StartShellParticles();
    StartShellLights();

    if (pLauncher != nullptr)
    { //--> Владелец присутствует и это CShellLauncher
        const CShellLauncher::launch_points& pointsAll = ShellGetAllLaunchPoints();
        const CShellLauncher::_lpoint& pointCurr = ShellGetCurrentLaunchPoint();

        m_dwFOVTranslateTime = pointsAll.dwFOVTranslateTime;
        m_dwFOVStableTime = pointsAll.dwFOVStableTime;

        // Если гильза не анимированная - сразу её "выкидываем"
        if (pointCurr.bAnimatedLaunch == false)
            ShellDrop();
    }
    else
    { //--> Владелец присутствует но это не CShellLauncher
        ShellDrop(); //--> Пробуем сразу выкинуть её
    }
}

// Вызывается после отсоединения гильзы от родителя, CShellLauncher уже недоступен
void CCustomShell::OnH_A_Independent()
{
    inherited::OnH_A_Independent();

    if (!g_pGameLevel->bReady)
        return;

    setVisible(true);

    prepare_physic_shell();
}

// Вызывается на каждом кадре
void CCustomShell::PostUpdateCL(bool bUpdateCL_disabled)
{
    inherited::PostUpdateCL(bUpdateCL_disabled);

    UpdateShellHUDMode();
    UpdateShellAnimated();
    UpdateShellLights();
    UpdateShellParticles();

    // Удаляем устаревшие гильзы
    if (m_dwDestroyTime != 0 && m_dwDestroyTime < Device.dwTimeGlobal)
    {
        if (Local())
            DestroyObject();
    }
}

//================== Рендеринг ==================//

// Рендер мировой модели
void CCustomShell::renderable_Render()
{
    if (m_bHUD_mode)
        return;

    //--> Просто отрисовываем модель
    inherited::renderable_Render();

    //--> Рисуем партиклы с мировым FOV
    UpdateShellParticlesHUDMode(false, 0.0f);
}

// Рендер худовой модели
void CCustomShell::OnRenderHUD(IGameObject* pCurViewEntity)
{
    if (!m_bHUD_mode)
        return;

    // Отрисовываем модель с динамическим HUD FOV
    extern ENGINE_API float psHUD_FOV;

    //--> Производим расчёт плавного изменения FOV объекта
    // clang-format off
    float fFactor = (m_dwFOVTranslateTime < 1 || (Device.dwTimeGlobal <= (m_dwRegisterTime + m_dwFOVStableTime)) ?
          1.f :
          (1.f -  ((float)(Device.dwTimeGlobal - (m_dwRegisterTime + m_dwFOVStableTime)) / (float)m_dwFOVTranslateTime)));
    clamp(fFactor, 0.0f, 1.0f);
    // clang-format on

    // 1 - только заспавнился (HUD FOV), <= 0 - закончился таймер перехода (FOV)
    float fHUD_FOV_delta = Device.fFOV - (psHUD_FOV * Device.fFOV);
    float fHUD_FOV = Device.fFOV - (fHUD_FOV_delta * fFactor);

    // Msg("[%d]: fFactor = %f, fHUD_FOV = %f, Device.dwTimeGlobal = %d, m_dwRegisterTime = %d", ID(), fFactor,
    // fHUD_FOV, Device.dwTimeGlobal, m_dwRegisterTime);

    //--> Рисуем модель
    Visual()->getObjectData()->m_hud_custom_fov = fHUD_FOV;

    GEnv.Render->set_Transform(&XFORM());
    GEnv.Render->add_Visual(Visual());

    //--> Рисуем партиклы с худовым FOV
    UpdateShellParticlesHUDMode(true, fHUD_FOV);

    //--> После полного перехода в мировой FOV отключаем HUD-режим
    if (fFactor <= 0.0f)
    {
        m_bHUD_mode = false;
        Visual()->getObjectData()->m_hud_custom_fov = -1.f;
    }
}

//================== Физика ==================//

// Подготовка физической оболочки сразу после выпадания гильзы из оружия
void CCustomShell::prepare_physic_shell()
{
    CPhysicsShellHolder& obj = *this;
    VERIFY(obj.Visual());

    IKinematics* K = obj.Visual()->dcast_PKinematics();
    VERIFY(K);

    if (!obj.PPhysicsShell())
    {
        Msg("! ERROR: PhysicsShell is NULL, object [%s][%d]", obj.cName().c_str(), obj.ID());
        return;
    }

    if (!obj.PPhysicsShell()->isFullActive())
    {
        K->CalculateBones_Invalidate();
        K->CalculateBones(TRUE);
    }
    obj.PPhysicsShell()->GetGlobalTransformDynamic(&obj.XFORM());

    K->CalculateBones_Invalidate();
    K->CalculateBones(TRUE);

    obj.spatial_move();
}

// Инициализация физ. оболочки (NetSpawn)
void CCustomShell::setup_physic_shell()
{
    R_ASSERT(!m_pPhysicsShell);

    create_physic_shell();
    m_pPhysicsShell->Activate(XFORM(), 0, XFORM());

    IKinematics* pKinematics = smart_cast<IKinematics*>(Visual());
    R_ASSERT(pKinematics);

    pKinematics->CalculateBones_Invalidate();
    pKinematics->CalculateBones(TRUE);
}

// Настройка и активация физ. оболочки гильзы (OnH_B_Independent)
void CCustomShell::activate_physic_shell()
{
    Fvector l_vel, a_vel;
    Fmatrix launch_matrix;
    launch_matrix.identity();

    CShellLauncher* pLauncher = GetLauncher();

    // Если в момент активации у нас есть владелец CShellLauncher - считываем параметры "полёта" гильзы
    if (pLauncher != nullptr)
    {
        const CShellLauncher::_lpoint& point = ShellGetCurrentLaunchPoint();

        //--> Начальная матрица трнсформации гильзы (позиция + поворот)
        launch_matrix = point.GetLaunchMatrix();

        //--> Начальная линейная скорость (и вектор полёта)
        l_vel = point.GetLaunchVel();

        //--> Начальная угловая скорость (кручение гильзы)
        float fi, teta, r;
        fi = ::Random.randF(0.f, 2.f * M_PI);
        teta = ::Random.randF(0.f, M_PI);
        r = ::Random.randF(2.f * M_PI, 3.f * M_PI);
        float rxy = r * _sin(teta);
        a_vel.set(rxy * _cos(fi), rxy * _sin(fi), r * _cos(teta));
        a_vel.mul(point.GetLaunchAVel());

        //--> Скорость от перемещения владельца гильзы
        u8 iLockGuard = 1000;
        IGameObject* pParentRoot = pLauncher->GetParentObject();
        while (iLockGuard > 0 && pParentRoot->H_Parent() != nullptr)
        { //--> Находим истинного владельца
            pParentRoot = pParentRoot->H_Parent();
            iLockGuard--;
        }
        VERIFY(iLockGuard > 0);

        constexpr float fParentSpeedMod = SHELL3D_PARENT_SPEED_FACTOR;
        CEntityAlive* entity_alive = smart_cast<CEntityAlive*>(pParentRoot);
        if (entity_alive && entity_alive->character_physics_support())
        {
            Fvector parent_vel;
            entity_alive->character_physics_support()->movement()->GetCharacterVelocity(parent_vel);
            l_vel.add(parent_vel.mul(fParentSpeedMod));
        }
        else
        {
            if (pParentRoot != this && pParentRoot->cast_physics_shell_holder() != nullptr)
            {
                Fvector vParentLVel;
                pParentRoot->cast_physics_shell_holder()->PHGetLinearVell(vParentLVel);
                l_vel.add(vParentLVel.mul(fParentSpeedMod));
            }
        }
    }

    R_ASSERT(!m_pPhysicsShell);
    create_physic_shell();

    R_ASSERT(m_pPhysicsShell);
    if (m_pPhysicsShell->isActive())
        return;

    m_pPhysicsShell->Activate(launch_matrix, l_vel, a_vel);
    m_pPhysicsShell->Update();

    XFORM().set(m_pPhysicsShell->mXFORM);
    Position().set(m_pPhysicsShell->mXFORM.c);

    m_pPhysicsShell->set_PhysicsRefObject(this);
    m_pPhysicsShell->set_ObjectContactCallback(nullptr);
    m_pPhysicsShell->set_ContactCallback(nullptr);

    m_pPhysicsShell->SetAirResistance(0.f, 0.f);
    m_pPhysicsShell->set_DynamicScales(1.f, 1.f);

    m_pPhysicsShell->SetAllGeomTraced();
    m_pPhysicsShell->DisableCharacterCollision();

    if (READ_IF_EXISTS(pSettings, r_bool, this->cNameSect(), "shell_ignore_dynamic", false))
        m_pPhysicsShell->SetIgnoreDynamic();

    if (READ_IF_EXISTS(pSettings, r_bool, this->cNameSect(), "shell_ignore_static", false))
        m_pPhysicsShell->SetIgnoreStatic();
}

//============== Специфичный код для гильз ==============//

// Обновление анимации вылета гильзы
void CCustomShell::UpdateShellAnimated()
{
    // Если гильза уже отвязана от владельца - то не анимируем её
    if (ShellIsDropped())
        return;

    CShellLauncher* pLauncher = GetLauncher();

    // Владелец не может проигрывать анимацию по какой-либо причине
    if (pLauncher->CanPlay3DShellAnim() == false)
    {
        ShellDrop();
        return;
    }

    // Обновляем позицию анимированной гильзы
    const CShellLauncher::_lpoint& pointCurr = ShellGetCurrentLaunchPoint();
    bool bAnimated = pointCurr.bAnimatedLaunch;

    if (bAnimated)
    {
        pLauncher->Update3DShellTransform();

        //--> Пересчитываем стартовую позицию вылет
        Fmatrix launch_matrix;
        launch_matrix.identity();
        launch_matrix = pointCurr.GetLaunchMatrix();

        XFORM().set(launch_matrix);

        setVisible(true);

        //--> По окончанию таймера запускаем гильзу в свободный полёт
        if (Device.dwTimeGlobal >= m_dwRegisterTime + pointCurr.dwAnimReleaseTime)
        {
            ShellDrop();
        }
    }
}

// Обновление текущего режима гильзы (Мировая \ Худовая)
void CCustomShell::UpdateShellHUDMode()
{
    if (dwHUDMode_upd_frame == Device.dwFrame)
        return; //--> Мы уже обновляли флаг на текущем кадре

    CShellLauncher* pLauncher = GetLauncher();
    if (pLauncher == nullptr)
        return; //--> У нас нет владельца, запоминаем последнее значение

    CGameObject* pGameObj = pLauncher->GetParentObject();
    CHudItem* pHudItem = pGameObj->cast_hud_item();

    m_bHUD_mode = pHudItem ? pHudItem->GetHUDmode() : false;

    dwHUDMode_upd_frame = Device.dwFrame;
}

// Получить владельца гильзы, возможно лишь до запуска гильзы в свободный полёт
CShellLauncher* CCustomShell::GetLauncher()
{
    CGameObject* pParent = (H_Parent() != nullptr ? H_Parent()->cast_game_object() : nullptr);
    if (pParent)
    {
        CShellLauncher* pLauncher = pParent->cast_shell_launcher();
        R_ASSERT4(pLauncher != nullptr, "Shell parent is not CShellLauncher", this->Name(), pParent->Name());

        return pLauncher;
    }

    return nullptr;
}

// Запустить гильзу в свободный полёт
void CCustomShell::ShellDrop()
{
    if (ShellIsDropped())
        return;

    const CShellLauncher::launch_points& pointsAll = ShellGetAllLaunchPoints();

    m_dwDestroyTime = (pointsAll.dwLifetimeTime == 0 ? 0 : Device.dwTimeGlobal + pointsAll.dwLifetimeTime);

    H_SetParent(nullptr);
}

// Запущена-ли гильза в свободный полёт?
bool CCustomShell::ShellIsDropped() { return H_Parent() == nullptr; }

// Получить мировые и худовые точки запуска гильз
const CShellLauncher::launch_points& CCustomShell::ShellGetAllLaunchPoints()
{
    CShellLauncher* pLauncher = GetLauncher();
    R_ASSERT2(pLauncher, "Can't get shell launch points, owner not found or shell already dropped");

    const u32 nLPCnt = pLauncher->m_launch_points.size();
    R_ASSERT2(m_launch_point_idx <= nLPCnt, "Wrong m_launch_point_idx");

    return pLauncher->m_launch_points[m_launch_point_idx - 1];
}

// Получить нужную в данный момент точку запуска гильзы (худовую \ мировую)
const CShellLauncher::_lpoint& CCustomShell::ShellGetCurrentLaunchPoint()
{
    UpdateShellHUDMode();

    const CShellLauncher::launch_points& points = ShellGetAllLaunchPoints();
    return (m_bHUD_mode ? points.point_hud : points.point_world);
}

//================== Освещение ==================//

// Обновляем свечение гильзы
void CCustomShell::UpdateShellLights()
{
    // clang-format off
    if (!m_bShellLightsEnabled || m_pShellLight == nullptr || !m_pShellLight->get_active())
        return;

    // Плавное затухание эффекта со временем
    if (m_fShellLightFactor > 0.0f)
    {
        m_pShellLight->set_color(
            m_ShellLightColor.r * m_fShellLightFactor,
            m_ShellLightColor.g * m_fShellLightFactor,
            m_ShellLightColor.b * m_fShellLightFactor
        );
        m_pShellLight->set_range(m_fShellLightRange * m_fShellLightFactor);
        m_pShellLight->set_position(Position());
    }
    else
    {
        StopShellLights();
    }

    m_fShellLightFactor -= Device.fTimeDelta / m_fShellLightTime;
    clamp(m_fShellLightFactor, 0.0f, 1.0f);
    // clang-format on
}

// Включить свечение гильзы
void CCustomShell::StartShellLights()
{
    if (!m_bShellLightsEnabled)
        return;

    m_fShellLightFactor = 1.0f;

    m_pShellLight->set_color(m_ShellLightColor.r, m_ShellLightColor.g, m_ShellLightColor.b);
    m_pShellLight->set_range(m_fShellLightRange);
    m_pShellLight->set_position(Position());
    m_pShellLight->set_active(true);
}

// Остановить свечение гильзы
void CCustomShell::StopShellLights()
{
    if (!m_bShellLightsEnabled)
        return;

    m_pShellLight->set_active(false);
}

//================== Партиклы ==================//

// Обновить партиклы гильз
void CCustomShell::UpdateShellParticles()
{
    if (IsShellParticleAlive() == false)
        return;

    Fvector vel;
    PHGetLinearVell(vel);

    vel.add(m_vPrevVel, vel);
    vel.mul(0.5f);
    m_vPrevVel.set(vel);

    Fmatrix particles_xform;
    particles_xform.identity();
    particles_xform.k.set(XFORM().k);
    particles_xform.k.mul(-1.f);

    Fvector::generate_orthonormal_basis(particles_xform.k, particles_xform.j, particles_xform.i);
    particles_xform.c.set(XFORM().c);

    m_pShellParticles->UpdateParent(particles_xform, vel);
}

// Обновить отрисовку партиклов в худовом режиме
void CCustomShell::UpdateShellParticlesHUDMode(bool bHUD, float fHUD_FOV)
{
    if (IsShellParticleAlive())
    {
        IRenderVisual* pV = m_pShellParticles->renderable.visual;
        IParticleCustom* pPC = smart_cast<IParticleCustom*>(pV);

        pPC->SetHudMode(bHUD);

        if (bHUD)
        {
            pPC->OverrideHudFov(fHUD_FOV);
        }
    }
}

// Проверка на то, что партикл запущен и ещё проигрывается
bool CCustomShell::IsShellParticleAlive()
{
    //--> Проверяем что партикл существует
    if (m_pShellParticles == nullptr)
        return false;

    //--> Проверяем что они не в очереди на удаление
    if (!m_pShellParticles->IsLooped() && m_pShellParticles->PSI_alive() == false)
    {
        m_pShellParticles = nullptr;
        return false;
    }

    return true;
}

// Запустить партиклы гильзы
void CCustomShell::StartShellParticles()
{
    VERIFY(m_pShellParticles == nullptr);

    if (!m_sShellParticles)
        return;

    UpdateShellHUDMode();

    m_pShellParticles = CParticlesObject::Create(*m_sShellParticles, FALSE);

    UpdateShellParticles();
    VERIFY(m_pShellParticles);

    m_pShellParticles->Play(m_bHUD_mode);

    if (m_pShellParticles->IsLooped() == false)
    { //--> Если партикл не зацикленный - то включаем ему автоудаление
        m_bIsParticlesAutoRemoved = true;
        m_pShellParticles->SetAutoRemove(true);
    }
}

// Остановить партиклы гильзы
void CCustomShell::StopShellParticles(bool bFromDestroy)
{
    //--> Если партикл автоудаляемый, и мы в процессе уничтожения - просто очищаем указатель
    if (bFromDestroy && m_bIsParticlesAutoRemoved)
    { //--> В этот момент (при выходе из игры) партикл может быть уже удалён
        m_pShellParticles = nullptr;
        return;
    }

    //--> В остальных случаях пытаемся остановить его самим
    if (IsShellParticleAlive())
    {
        m_pShellParticles->Stop();
        m_pShellParticles->SetAutoRemove(true);
    }

    m_pShellParticles = nullptr;
}
