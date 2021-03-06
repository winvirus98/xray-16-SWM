/*******************************/
/********** 3D-Гильзы **********/ //--#SM+#--
/*******************************/

#pragma once

#include "physic_item.h"
#include "xrPhysics/PHUpdateObject.h"

class CShellLauncher;
struct CShellLauncher::launch_points;

#define SHELL3D_PARENT_SPEED_FACTOR 1.35f // Модификатор скорости владельца, прибавляемой к гильзе

class CCustomShell : public CPhysicItem, public CPHUpdateObject
{
    //======= Описание класса и основные наследуемые методы =======//
private:
    typedef CPhysicItem inherited;
    friend CShellLauncher;

protected:
    virtual void Load(LPCSTR section);
    virtual void reload(LPCSTR section);

    virtual BOOL net_Spawn(CSE_Abstract* DC);
    virtual void net_Destroy();

    virtual void reinit();

    virtual void OnH_A_Chield();
    virtual void OnH_A_Independent();

    virtual void PostUpdateCL(bool bUpdateCL_disabled);

    virtual BOOL AlwaysTheCrow() { return TRUE; }
    virtual BOOL UsedAI_Locations() { return FALSE; }
    virtual bool Useful() const { return true; }

public:
    CCustomShell(void);
    virtual ~CCustomShell(void);

    //================== Рендеринг ==================//
private:

protected:
    virtual void renderable_Render();
    virtual void OnRenderHUD(IGameObject* pCurViewEntity);

public:

    //================== Физика ==================//
private:
    u32 dwHUDMode_upd_frame;

protected:
            void prepare_physic_shell();
    virtual void setup_physic_shell();
    virtual void activate_physic_shell();

    virtual void PhDataUpdate(float step) {};
    virtual void PhTune(float step) {};

public:
    virtual bool IsCollideWithBullets() { return false; } // Level_bullet_manager_firetrace.cpp
    virtual bool IsCollideWithActorCamera() { return false; } // xrPhysics\ActorCameraCollision.cpp

    //============== Специфичный код для гильз ==============//
private:

protected:
    u32 m_dwRegisterTime;
    u32 m_dwDestroyTime;
    u32 m_dwFOVStableTime;
    u32 m_dwFOVTranslateTime;
    u32 m_launch_point_idx;
    bool m_bHUD_mode;

    void UpdateShellAnimated();
    void UpdateShellHUDMode();

public:
    CShellLauncher* GetLauncher();

    void ShellDrop();
    bool ShellIsDropped();

    const CShellLauncher::launch_points& ShellGetAllLaunchPoints();
    const CShellLauncher::_lpoint& ShellGetCurrentLaunchPoint();

    //================== Освещение ==================//
private:

protected:
    ref_light m_pShellLight;
    float m_fShellLightFactor;

    void UpdateShellLights();

public:
    bool m_bShellLightsEnabled;

    float m_fShellLightRange;
    float m_fShellLightTime;
    Fcolor m_ShellLightColor;

    void StartShellLights();
    void StopShellLights();

    //================== Партиклы ==================//
private:
    Fvector m_vPrevVel;
    bool m_bIsParticlesAutoRemoved; //--> Партикл является автоудаляемым (указатель может быть невалидным)

protected:
    shared_str m_sShellParticles;
    CParticlesObject* m_pShellParticles;

    void UpdateShellParticles();
    void UpdateShellParticlesHUDMode(bool bHUD, float fHUD_FOV);

public:
    bool IsShellParticleAlive();

    void StartShellParticles();
    void StopShellParticles(bool bFromDestroy = false);
};
