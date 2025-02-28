//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//

#include "cbase.h"
#include "basehlcombatweapon.h"
#include "player.h"
#include "gamerules.h"
#include "ammodef.h"
#include "mathlib/mathlib.h"
#include "in_buttons.h"
#include "soundent.h"
#include "animation.h"
#include "ai_condition.h"
#include "basebludgeonweapon.h"
#include "ndebugoverlay.h"
#include "te_effect_dispatch.h"
#include "rumble_shared.h"
#include "GameStats.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

IMPLEMENT_SERVERCLASS_ST( CBaseHLBludgeonWeapon, DT_BaseHLBludgeonWeapon )
END_SEND_TABLE()

#define BLUDGEON_HULL_DIM		20
#define BLUDGEON_HULL_DIM_MOTION 16

static const Vector g_bludgeonMins(-BLUDGEON_HULL_DIM,-BLUDGEON_HULL_DIM,-BLUDGEON_HULL_DIM);
static const Vector g_bludgeonMaxs(BLUDGEON_HULL_DIM,BLUDGEON_HULL_DIM,BLUDGEON_HULL_DIM);

static const Vector g_bludgeonMotionMins(-BLUDGEON_HULL_DIM_MOTION,-BLUDGEON_HULL_DIM_MOTION,-BLUDGEON_HULL_DIM_MOTION);
static const Vector g_bludgeonMotionMaxs(BLUDGEON_HULL_DIM_MOTION,BLUDGEON_HULL_DIM_MOTION,BLUDGEON_HULL_DIM_MOTION);


//-----------------------------------------------------------------------------
// Constructor
//-----------------------------------------------------------------------------
CBaseHLBludgeonWeapon::CBaseHLBludgeonWeapon()
{
	m_bFiresUnderwater = true;
	m_flNextMotionCheck = 0;
	m_flLastMotionCheck = 0;
	m_prevMotionPosition.Init();
}

//-----------------------------------------------------------------------------
// Purpose: Spawn the weapon
//-----------------------------------------------------------------------------
void CBaseHLBludgeonWeapon::Spawn( void )
{
	m_fMinRange1	= 0;
	m_fMinRange2	= 0;
	m_fMaxRange1	= 64;
	m_fMaxRange2	= 64;
	//Call base class first
	BaseClass::Spawn();
}

//-----------------------------------------------------------------------------
// Purpose: Precache the weapon
//-----------------------------------------------------------------------------
void CBaseHLBludgeonWeapon::Precache( void )
{
	//Call base class first
	BaseClass::Precache();
}

int CBaseHLBludgeonWeapon::CapabilitiesGet()
{ 
	return bits_CAP_WEAPON_MELEE_ATTACK1; 
}


int CBaseHLBludgeonWeapon::WeaponMeleeAttack1Condition( float flDot, float flDist )
{
	if (flDist > 64)
	{
		return COND_TOO_FAR_TO_ATTACK;
	}
	else if (flDot < 0.7)
	{
		return COND_NOT_FACING_ATTACK;
	}

	return COND_CAN_MELEE_ATTACK1;
}

//------------------------------------------------------------------------------
// Purpose : Update weapon
//------------------------------------------------------------------------------
void CBaseHLBludgeonWeapon::ItemPostFrame( void )
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	
	if ( pOwner == NULL )
		return;

	if ( CheckSwingMotion() )
	{
		// Handles it's own animation....
	} 
	else if ( (pOwner->m_nButtons & IN_ATTACK) && (m_flNextPrimaryAttack <= gpGlobals->curtime) )
	{
		PrimaryAttack();
	} 
	else if ( (pOwner->m_nButtons & IN_ATTACK2) && (m_flNextSecondaryAttack <= gpGlobals->curtime) )
	{
		SecondaryAttack();
	}
	else 
	{
		WeaponIdle();
		return;
	}
}


// Disabling motion swing check filter for now, will run on every update

#define MOTION_CHECK_RATE .01
#define MOTION_SWING_THRESHOLD 50

bool CBaseHLBludgeonWeapon::CheckSwingMotion( )
{
	if ( m_flNextMotionCheck > gpGlobals->curtime || m_flNextPrimaryAttack > gpGlobals->curtime )
		return false;

	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	
	if ( pOwner == NULL )
		return false;

	// Get the head of the actual crowbar
	Vector forward, up, right;
	pOwner->EyeVectors(&forward, &right, &up);
	Vector position = pOwner->Weapon_ShootPosition() + (15 * up); 
	

	Vector currentMotionPosition = position - pOwner->EyePosition();
	
	if ( m_prevMotionPosition.IsZero() )
	{
		m_prevMotionPosition = currentMotionPosition;
		m_flLastMotionCheck = gpGlobals->curtime;
	}
	
	// Get vector differences and calculate velocity
	Vector motion = currentMotionPosition - m_prevMotionPosition;
		
	float velocity =  motion.Length() /  (gpGlobals->curtime - m_flLastMotionCheck); //inches per second
	
	bool isSwinging = velocity > MOTION_SWING_THRESHOLD;

	if ( isSwinging ) 
	{
		MotionSwing(forward, position, motion, velocity);
	}
	
	// update prev motion position
	m_prevMotionPosition = currentMotionPosition;
	m_flLastMotionCheck = gpGlobals->curtime;
	m_flNextMotionCheck = m_flLastMotionCheck + MOTION_CHECK_RATE;

	return isSwinging;
}


//------------------------------------------------------------------------------
// Purpose :
// Input   :
// Output  :
//------------------------------------------------------------------------------
void CBaseHLBludgeonWeapon::PrimaryAttack()
{
	Swing( false );
}

//------------------------------------------------------------------------------
// Purpose :
// Input   :
// Output  :
//------------------------------------------------------------------------------
void CBaseHLBludgeonWeapon::SecondaryAttack()
{
	Swing( true );
}


//------------------------------------------------------------------------------
// Purpose: Implement impact function
//------------------------------------------------------------------------------
void CBaseHLBludgeonWeapon::Hit( trace_t &traceHit, Activity nHitActivity, bool bIsSecondary )
{
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	
	//Make sound for the AI
	CSoundEnt::InsertSound( SOUND_BULLET_IMPACT, traceHit.endpos, 400, 0.2f, pPlayer );

	// This isn't great, but it's something for when the crowbar hits.
	pPlayer->RumbleEffect( RUMBLE_AR2, 0, RUMBLE_FLAG_RESTART );

	CBaseEntity	*pHitEntity = traceHit.m_pEnt;

	//Apply damage to a hit target
	if ( pHitEntity != NULL )
	{
		Vector hitDirection;
		pPlayer->EyeVectors( &hitDirection );
		VectorNormalize( hitDirection );

		CTakeDamageInfo info( GetOwner(), GetOwner(), GetDamageForActivity( nHitActivity ), DMG_CLUB );

		if( pPlayer && pHitEntity->IsNPC() )
		{
			// If bonking an NPC, adjust damage.
			info.AdjustPlayerDamageInflictedForSkillLevel();
		}

		CalculateMeleeDamageForce( &info, hitDirection, traceHit.endpos );

		pHitEntity->DispatchTraceAttack( info, hitDirection, &traceHit ); 
		ApplyMultiDamage();

		// Now hit all triggers along the ray that... 
		TraceAttackToTriggers( info, traceHit.startpos, traceHit.endpos, hitDirection );

		if ( ToBaseCombatCharacter( pHitEntity ) )
		{
			gamestats->Event_WeaponHit( pPlayer, !bIsSecondary, GetClassname(), info );
		}
	}

	// Apply an impact effect
	ImpactEffect( traceHit );
}

Activity CBaseHLBludgeonWeapon::ChooseIntersectionPointAndActivity( trace_t &hitTrace, const Vector &mins, const Vector &maxs, CBasePlayer *pOwner )
{
	int			i, j, k;
	float		distance;
	const float	*minmaxs[2] = {mins.Base(), maxs.Base()};
	trace_t		tmpTrace;
	Vector		vecHullEnd = hitTrace.endpos;
	Vector		vecEnd;

	distance = 1e6f;
	Vector vecSrc = hitTrace.startpos;

	vecHullEnd = vecSrc + ((vecHullEnd - vecSrc)*2);
	UTIL_TraceLine( vecSrc, vecHullEnd, MASK_SHOT_HULL, pOwner, COLLISION_GROUP_NONE, &tmpTrace );
	if ( tmpTrace.fraction == 1.0 )
	{
		for ( i = 0; i < 2; i++ )
		{
			for ( j = 0; j < 2; j++ )
			{
				for ( k = 0; k < 2; k++ )
				{
					vecEnd.x = vecHullEnd.x + minmaxs[i][0];
					vecEnd.y = vecHullEnd.y + minmaxs[j][1];
					vecEnd.z = vecHullEnd.z + minmaxs[k][2];

					UTIL_TraceLine( vecSrc, vecEnd, MASK_SHOT_HULL, pOwner, COLLISION_GROUP_NONE, &tmpTrace );
					if ( tmpTrace.fraction < 1.0 )
					{
						float thisDistance = (tmpTrace.endpos - vecSrc).Length();
						if ( thisDistance < distance )
						{
							hitTrace = tmpTrace;
							distance = thisDistance;
						}
					}
				}
			}
		}
	}
	else
	{
		hitTrace = tmpTrace;
	}


	return ACT_VM_HITCENTER;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : &traceHit - 
//-----------------------------------------------------------------------------
bool CBaseHLBludgeonWeapon::ImpactWater( const Vector &start, const Vector &end )
{
	//FIXME: This doesn't handle the case of trying to splash while being underwater, but that's not going to look good
	//		 right now anyway...
	
	// We must start outside the water
	if ( UTIL_PointContents( start ) & (CONTENTS_WATER|CONTENTS_SLIME))
		return false;

	// We must end inside of water
	if ( !(UTIL_PointContents( end ) & (CONTENTS_WATER|CONTENTS_SLIME)))
		return false;

	trace_t	waterTrace;

	UTIL_TraceLine( start, end, (CONTENTS_WATER|CONTENTS_SLIME), GetOwner(), COLLISION_GROUP_NONE, &waterTrace );

	if ( waterTrace.fraction < 1.0f )
	{
		CEffectData	data;

		data.m_fFlags  = 0;
		data.m_vOrigin = waterTrace.endpos;
		data.m_vNormal = waterTrace.plane.normal;
		data.m_flScale = 8.0f;

		// See if we hit slime
		if ( waterTrace.contents & CONTENTS_SLIME )
		{
			data.m_fFlags |= FX_WATER_IN_SLIME;
		}

		DispatchEffect( "watersplash", data );
	}

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseHLBludgeonWeapon::ImpactEffect( trace_t &traceHit, float scale )
{
	// See if we hit water (we don't do the other impact effects in this case)
	if ( ImpactWater( traceHit.startpos, traceHit.endpos ) )
		return;

	//FIXME: need new decals
	UTIL_ImpactTrace( &traceHit, DMG_CLUB, NULL, scale );
}


//------------------------------------------------------------------------------
// Purpose : Starts the swing of the weapon and determines the animation
// Input   : bIsSecondary - is this a secondary attack?
//------------------------------------------------------------------------------
void CBaseHLBludgeonWeapon::Swing( int bIsSecondary )
{
	trace_t traceHit;

	// Try a ray
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( !pOwner )
		return;

	pOwner->RumbleEffect( RUMBLE_CROWBAR_SWING, 0, RUMBLE_FLAG_RESTART );

	Vector swingStart = pOwner->Weapon_ShootPosition( );
	Vector forward;
	pOwner->EyeVectors(&forward);
	

	Vector swingEnd = swingStart + forward * GetRange();
	UTIL_TraceLine( swingStart, swingEnd, MASK_SHOT_HULL, pOwner, COLLISION_GROUP_NONE, &traceHit );
	Activity nHitActivity = ACT_VM_HITCENTER;

	// Like bullets, bludgeon traces have to trace against triggers.
	CTakeDamageInfo triggerInfo( GetOwner(), GetOwner(), GetDamageForActivity( nHitActivity ), DMG_CLUB );
	triggerInfo.SetDamagePosition( traceHit.startpos );
	triggerInfo.SetDamageForce( forward );
	TraceAttackToTriggers( triggerInfo, traceHit.startpos, traceHit.endpos, forward );

	if ( traceHit.fraction == 1.0 )
	{
		float bludgeonHullRadius = 1.732f * BLUDGEON_HULL_DIM;  // hull is +/- 16, so use cuberoot of 2 to determine how big the hull is from center to the corner point

		// Back off by hull "radius"
		swingEnd -= forward * bludgeonHullRadius;

		UTIL_TraceHull( swingStart, swingEnd, g_bludgeonMins, g_bludgeonMaxs, MASK_SHOT_HULL, pOwner, COLLISION_GROUP_NONE, &traceHit );
		if ( traceHit.fraction < 1.0 && traceHit.m_pEnt )
		{
			Vector vecToTarget = traceHit.m_pEnt->GetAbsOrigin() - swingStart;
			VectorNormalize( vecToTarget );

			float dot = vecToTarget.Dot( forward );

			// YWB:  Make sure they are sort of facing the guy at least...
			if ( dot < 0.70721f )
			{
				// Force amiss
				traceHit.fraction = 1.0f;
			}
			else
			{
				nHitActivity = ChooseIntersectionPointAndActivity( traceHit, g_bludgeonMins, g_bludgeonMaxs, pOwner );
			}
		}
	}

	if ( !bIsSecondary )
	{
		m_iPrimaryAttacks++;
	} 
	else 
	{
		m_iSecondaryAttacks++;
	}

	gamestats->Event_WeaponFired( pOwner, !bIsSecondary, GetClassname() );

	// -------------------------
	//	Miss
	// -------------------------
	if ( traceHit.fraction == 1.0f )
	{
		nHitActivity = bIsSecondary ? ACT_VM_MISSCENTER2 : ACT_VM_MISSCENTER;

		// We want to test the first swing again
		Vector testEnd = swingStart + forward * GetRange();
		
		// See if we happened to hit water
		ImpactWater( swingStart, testEnd );
	}
	else
	{
		Hit( traceHit, nHitActivity, bIsSecondary ? true : false );
	}

	// Send the anim
	SendWeaponAnim( nHitActivity );

	//Setup our next attack times
	m_flNextPrimaryAttack = gpGlobals->curtime + GetFireRate();
	m_flNextSecondaryAttack = gpGlobals->curtime + SequenceDuration();

	//Play swing sound
	WeaponSound( SINGLE );
}



// TODO: need to add debounce on a particular swing, saving of the vector during impact 
// and requiring something opposite before an impact can occur again...

void CBaseHLBludgeonWeapon::MotionSwing( const Vector &aimDirection, const Vector &pos, const Vector &dir, float velocity )
{
	trace_t traceHit;

	// Try a ray
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( !pOwner )
		return;
	
	Vector v;
	
	Vector swingStart = pos;
	Vector forward = dir/dir.Length();

	Vector up,right;
	VectorVectors(forward, right, up);
	Vector swingEnd = swingStart + forward*19.5; 
	
	UTIL_TraceLine( swingStart, swingEnd, MASK_SHOT_HULL, pOwner, COLLISION_GROUP_NONE, &traceHit );
	Activity nHitActivity = ACT_VM_HITCENTER;
		
	// Like bullets, bludgeon traces have to trace against triggers.
	CTakeDamageInfo triggerInfo( GetOwner(), GetOwner(), GetDamageForActivity( nHitActivity ), DMG_CLUB );
	triggerInfo.SetDamagePosition( traceHit.startpos );
	triggerInfo.SetDamageForce( forward );

	// calculate a damage scale to use for effects etc...
	float damageScale = Clamp((velocity-80) / 120.f, .175f, 1.5f);
	triggerInfo.ScaleDamage( damageScale );
	triggerInfo.ScaleDamageForce( damageScale );
	TraceAttackToTriggers( triggerInfo, traceHit.startpos, traceHit.endpos, forward );

	gamestats->Event_WeaponFired( pOwner, true, GetClassname() );

	float motionFireRate = .35f;
	bool wasBlock = false;
	Vector vecToTarget = forward.Normalized();


	// If no hit we'd do a second broader hulltrace that checks only for impacts with enemies
	// Since it's quite hard to hit manhacks, flying crabs etc otherwise
	// Ideally this acts as an 'autoaim' of sorts for the motion swinging without screwing up the normal 
	// crowbar interactions with world objects
	if ( traceHit.fraction == 1.0 )
	{
		// Get radius of hull trace and adjust start to include it
		float bludgeonHullRadius = 1.732f * BLUDGEON_HULL_DIM_MOTION;  
		Vector hullSwingStart = swingStart + forward * bludgeonHullRadius;
		Vector hullSwingEnd = swingEnd + forward;
		
		UTIL_TraceHull( hullSwingStart, swingEnd, g_bludgeonMotionMins, g_bludgeonMotionMaxs, MASK_SHOT_HULL, pOwner, COLLISION_GROUP_NONE, &traceHit );
				
		if ( traceHit.fraction < 1.0 && traceHit.m_pEnt )
		{
			if ( traceHit.m_pEnt->ClassMatches("npc_manhack") || 
				 traceHit.m_pEnt->ClassMatches("npc_headcra*") || 
				 traceHit.m_pEnt->ClassMatches("npc_ant*") ||
				 traceHit.m_pEnt->ClassMatches("npc_zomb*") ||
				 traceHit.m_pEnt->ClassMatches("npc_fastzombie") ) // TODO: other entities here...
			{
				Msg("Bludgeoned a %s \n", traceHit.m_pEnt->GetClassname());
				vecToTarget = traceHit.m_pEnt->GetAbsOrigin() - swingStart;
				nHitActivity = ChooseIntersectionPointAndActivity( traceHit, g_bludgeonMotionMins, g_bludgeonMotionMaxs, pOwner );
			}
			else
			{
				traceHit.fraction = 1.0f;
			}
		}
	}

	
	// Now handle the hit if there was one..

	if ( traceHit.fraction == 1.0f )
	{
		nHitActivity = ACT_VM_MISSCENTER;

		// We want to test the first swing again
		Vector testEnd = swingStart + forward*24.f + up*-2;
				
		// See if we happened to hit water
		if ( ImpactWater( swingStart, testEnd ) )
			m_flNextPrimaryAttack = gpGlobals->curtime + motionFireRate;
	
	}
	else
	{
		// Hit stuff here...
		CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
		
		//Make sound for the AI
		CSoundEnt::InsertSound( SOUND_BULLET_IMPACT, traceHit.endpos, 400, 0.2f, pPlayer );
		CBaseEntity	*pHitEntity = traceHit.m_pEnt;

		//Apply damage to a hit target
		if ( pHitEntity != NULL )
		{
		
			CTakeDamageInfo info( GetOwner(), GetOwner(), GetDamageForActivity( nHitActivity ) * damageScale, DMG_CLUB );
			
			if( pPlayer && pHitEntity->IsNPC() )
			{
				// If bonking an NPC, adjust damage.
				info.AdjustPlayerDamageInflictedForSkillLevel();
			}

			CalculateMeleeDamageForce( &info, forward, traceHit.endpos, damageScale );

			pHitEntity->DispatchTraceAttack( info, forward, &traceHit ); 
			ApplyMultiDamage();

			// Now hit all triggers along the ray that... 
			TraceAttackToTriggers( info, traceHit.startpos, traceHit.endpos, forward );

			if ( ToBaseCombatCharacter( pHitEntity ) )
			{
				gamestats->Event_WeaponHit( pPlayer, true, GetClassname(), info );
			}
		}

		// Apply an impact effect
		ImpactEffect( traceHit, Clamp(damageScale, 0.f, 1.f) );
		SendWeaponAnim( ACT_VM_HITDYNAMIC );

		//Setup our next attack times
		m_flNextPrimaryAttack = gpGlobals->curtime + motionFireRate;
		m_flNextSecondaryAttack = gpGlobals->curtime + motionFireRate;

	}
}















