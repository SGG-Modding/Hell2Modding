#pragma once

#include <EASTL/vector.h>
#include <EASTL/optional.h>
#include <EASTL/hash_set.h>

namespace Vectormath
{
	struct Point2
	{
		float mX;
		float mY;
	};

	struct Vector2
	{
		float mX;
		float mY;
	};

	namespace SSE
	{
		struct Vector3
		{
			__m128 mVec128;
		};
	} // namespace SSE
} // namespace Vectormath

namespace eastl_custom
{
	struct allocator_forge
	{
	};

	namespace internal
	{
		struct unused_class
		{
		};

		union functor_storage_alignment
		{
			void(__fastcall *unused_func_ptr)();
			void(__fastcall *unused_func_mem_ptr)(eastl_custom::internal::unused_class *this_);
			void *unused_ptr;
		};

		template<size_t size>
		struct functor_storage
		{
			//functor_storage_alignment align;
			char storage[size];
		};

		template<size_t size>
		struct function_base_detail
		{
			functor_storage<size> mStorage;
		};

		enum ManagerOperations : __int32
		{
			MGROPS_DESTRUCT_FUNCTOR = 0x0,
			MGROPS_COPY_FUNCTOR     = 0x1,
			MGROPS_MOVE_FUNCTOR     = 0x2,
			MGROPS_GET_TYPE_INFO    = 0x3,
			MGROPS_GET_FUNC_PTR     = 0x4,
		};

		template<typename T, size_t size>
		struct function_detail : eastl_custom::internal::function_base_detail<size>
		{
			void *(__fastcall *mMgrFuncPtr)(void *, void *, ManagerOperations);
			T(*mInvokeFuncPtr);
		};

	} // namespace internal

	template<typename T>
	struct function : eastl_custom::internal::function_detail<T, 16>
	{
	};

	constexpr size_t testaaaa = offsetof(eastl_custom::function<void *>, mMgrFuncPtr);

	static_assert(offsetof(eastl_custom::function<void *>, mMgrFuncPtr) == 0x10);
	static_assert(offsetof(eastl_custom::function<void *>, mInvokeFuncPtr) == 0x18);
} // namespace eastl_custom

namespace FMOD
{
	namespace Studio
	{
		struct EventInstance
		{
		};
	} // namespace Studio
} // namespace FMOD

namespace sgg
{
	struct UsingInfo;

	struct HashGuid
	{
		unsigned int mId;
	};

	enum SortMode : __int8
	{
		Isometric  = 0x0,
		FromParent = 0x1,
		Id         = 0x2,
		Secondary  = 0x3,
	};

	enum RenderMeshType : __int8
	{
		Flat     = 0x0,
		Vertical = 0x1,
		Mesh     = 0x2,
		Sprite   = 0x3,
	};

	struct Color_32
	{
		unsigned __int8 r;
		unsigned __int8 g;
		unsigned __int8 b;
		unsigned __int8 a;
	};

	union Color
	{
		unsigned int v;
		Color_32 __s1;
	};

	struct AudioChannel;

	struct __declspec(align(8)) SoundCue
	{
		sgg::AudioChannel *pOwner;
		sgg::HashGuid mName;
	};

	struct SoundAction
	{
		eastl::vector<eastl::function<sgg::SoundCue>> mCallBacks;
	};

	struct /*VFT*/ AudioChannel_vtbl
	{
		void(__fastcall *Play)(sgg::AudioChannel *this_);
		bool(__fastcall *Update)(sgg::AudioChannel *this_);
		void(__fastcall *Dispose)(sgg::AudioChannel *this_);
		void(__fastcall *dctor)(sgg::AudioChannel *this_);
	};

	struct AudioChannel
	{
		sgg::AudioChannel_vtbl *__vftable /*VFT*/;
		bool mIs3D;
		bool mPersistent;
		bool mIsDisposed;
		bool mStarted;
		int mId;
		sgg::SoundCue mSoundCue;
		sgg::SoundAction mOnSoundDone;
		FMOD::Studio::EventInstance *pSound;
	};

	struct __declspec(align(8)) GameData
	{
		void *vfptr;
		sgg::HashGuid mName;
		sgg::HashGuid mOrganization;
		sgg::HashGuid mInheritFrom;
	};

	static_assert(sizeof(GameData) == 0x18);

	struct __declspec(align(8)) InteractDataDef
	{
		float mDistance;
		float mDistanceZ;
		float mArcDistance;
		bool mAutoActivate;
		bool mAutoActivateWithGamepad;
		bool mCursorOnly;
		bool mUseExtents;
		bool mUseTextBoxes;
		bool mFreeFormSelectable;
		float mFreeFormSelectOffsetX;
		float mFreeFormSelectOffsetY;
		float mFreeFormSelectInputMultiplierX;
		float mFreeFormSelectInputMultiplierY;
		bool mStartsUseable;
		bool mUseableWhileInputDisabled;
		float mTooltipOffsetX;
		float mTooltipOffsetY;
		float mTooltipX;
		float mTooltipY;
		float mOffsetX;
		float mOffsetY;
		float mExtentOffsetY;
		float mMaxZ;
		sgg::HashGuid mUnuseableAnimation;
		sgg::HashGuid mAutoUseFailedAnimation;
		sgg::HashGuid mVisualFx;
		sgg::SoundCue mDisabledUseSound;
		sgg::HashGuid mHighlightOnAnimation;
		sgg::HashGuid mHighlightOffAnimation;
		float mCooldown;
		bool mStartsCoolingDown;
		float mRepeatDelay;
		float mRepeatInterval;
		bool mDisableOnUse;
		bool mDestroyOnUse;
	};

	struct InteractData
	{
		sgg::InteractDataDef mDef;
	};

	struct __declspec(align(8)) ThingDataDef
	{
		eastl::vector<Vectormath::Point2> mPoints;
		eastl::vector<UsingInfo> mUsing;
		InteractData *mInteract;
		Vectormath::Point2 mOffset;
		Vectormath::Point2 mSize;
		HashGuid mGraphic;
		HashGuid mActiveGrannyAttachment;
		HashGuid mGrannyModel;
		float mGrannyModelVerticalMask;
		SoundCue mAmbientSound;
		HashGuid mImpulsedFx;
		float mImpulsedFxCooldown;
		SoundCue mFootstepSound;
		HashGuid mFootstepFxL;
		HashGuid mFootstepFxR;
		float mTallness;
		float mRelativeExtentScale;
		float mExtentScale;
		float mSortBoundsScale;
		float mRadius;
		float mSelectionRadius;
		float mSelectionWidth;
		float mSelectionHeight;
		float mSelectionShiftY;
		float mScale;
		float mOffsetZ;
		float mOriginX;
		float mOriginY;
		float mGrip;
		float mGravity;
		float mTerminalVelocity;
		float mUpwardTerminalVelocity;
		float mExternalForceMultiplier;
		Color mColor;
		float mHue;
		float mSaturation;
		float mValue;
		float mOcclusionWidth;
		float mOcclusionHeight;
		float mOcclusionOpacity;
		float mOcclusionInterpolateDuration;
		float mOcclusionAreaScalar;
		float mOcclusionOutlineRed;
		float mOcclusionOutlineGreen;
		float mOcclusionOutlineBlue;
		float mOcclusionOutlineThickness;
		float mOcclusionOutlineOpacity;
		float mOcclusionOutlineThreshold;
		float mOcclusionOutlineDelay;
		float mOcclusionOutlineFadeSpeed;
		HashGuid mOcclusionBaseAnim;
		SortMode mSortMode;
		HashGuid mAttachedAnim;
		SoundCue mTouchdownSound;
		HashGuid mOnTouchdownFxAnim;
		HashGuid mTouchdownGraphic;
		HashGuid mLight;
		RenderMeshType mMeshType;
		float mTimeModifierFraction;
		float mAmbient;
		float mGrannyMaskPulseSpeed;
		float mGrannyMaskScrollSpeed;
		float mGrannyMaskScrollSize;
		float mGrannyMaskScrollAngle;
		float mGrannyMaskAlpha;
		bool mHighPrioritySfx;
		bool mInvisible;
		bool mImmuneToVacuum;
		bool mImmuneToForce;
		bool mRotateGeometry;
		bool mMirrorGeometry;
		float mGeometryScaleY;
		bool mCreatesShadows;
		bool mStopsLight;
		bool mStopsUnits;
		bool mStopsProjectiles;
		bool mAddColor;
		bool mCanBeOccluded;
		bool mCausesOcclusion;
		bool mDrawEmbeddedBehind;
		bool mDrawVfxOnTop;
		bool mUseBoundsForSortDrawArea;
		bool mUseScreenLocation;
		bool mAllowDrawableCache;
		bool mDieOnTouchdown;
		bool mSpeechCapable;
		bool mAmbientSoundDistanceCheck;
		bool mTriggerOnSpawn;
		bool mEditorOutlineDrawBounds;
	};

	struct ThingData
	{
		sgg::ThingDataDef mDef;
	};

	enum Rtti__Type : __int32
	{
		Component_t           = 0x1,
		Weapon_t              = 0x2,
		GameEvent             = 0x4,
		IUndoRedoRecord       = 0x8,
		Thing_t               = 0x11,
		Animation_t           = 0x21,
		Light_t               = 0x41,
		Obstacle_t            = 0x91,
		Unit_t                = 0x1'11,
		Projectile_t          = 0x2'11,
		ArcProjectile         = 0x6'11,
		BeamProjectile        = 0xA'11,
		HomingProjectile      = 0x12'11,
		InstantProjectile     = 0x22'11,
		LobProjectile         = 0x42'11,
		BookAnimation         = 0x80'21,
		SlideAnimation        = 0x1'80'21,
		ConstantAnimation     = 0x2'00'21,
		DatalessAnimation     = 0x4'00'21,
		ModelAnimation        = 0x8'00'21,
		PlayerUnit            = 0x20'01'11,
		BlinkWeapon           = 0x22,
		GunWeapon             = 0x42,
		RamWeapon             = 0x82,
		DelayedGameEvent      = 0x24,
		ScriptUpdateGameEvent = 0x44,
		UpdateGameEvent       = 0x84,
		PersistentGameEvent   = 0x1'04,
		UndoTransaction       = 0x28,
		UndoRedoRecord        = 0x48,
	};

	/* 4816 */
	struct /*VFT*/ Rtti_vtbl
	{
		//void(__fastcall *dctor)(sgg::Rtti *this_);
	};

	struct __declspec(align(8)) Rtti
	{
		sgg::Rtti_vtbl *__vftable /*VFT*/;
		sgg::Rtti__Type mType;
		char pad_undefined[4];
	};

	struct SortScore
	{
		unsigned __int64 mValue;
	};

	struct Entity_Union
	{
		unsigned __int32 mIndex      : 22;
		unsigned __int32 mGeneration : 8;
		unsigned __int32 mManager    : 2;
	};

	/* 4821 */
	union Entity
	{
		unsigned int mId;
		Entity_Union __s1;
	};

	struct Bounds
	{
		float XMin;
		float XMax;
		float YMin;
		float YMax;
		float ZMin;
		float ZMax;
	};

	/* 4817 */
	struct IRenderComponent : sgg::Rtti
	{
		sgg::SortScore mSortScore;
		sgg::SortMode mSortMode;
		unsigned int mSecondarySortKey;
		bool mHasOutline;
		sgg::Entity mEntity;
		sgg::Bounds mBounds;
	};

	/* 4822 */
	struct /*VFT*/ IRenderComponent_vtbl
	{
		void(__fastcall *dctor)(struct sgg::IRenderComponent *this_);
		void(__fastcall *UpdateSortScore)(sgg::IRenderComponent *this_);
		void(__fastcall *PrepDraw)(sgg::IRenderComponent *this_);
		void(__fastcall *SubmitDraw)(sgg::IRenderComponent *this_, float);
		sgg::HashGuid *(__fastcall *DrawGroup)(sgg::IRenderComponent *this_, sgg::HashGuid *result);
		sgg::Bounds *(__fastcall *GetBounds)(sgg::IRenderComponent *this_, sgg::Bounds *result);
		bool(__fastcall *HasModelAnimation)(sgg::IRenderComponent *this_);
	};

	/* 4898 */
	struct IRectangle
	{
		int x;
		int y;
		int w;
		int h;
	};

	struct Polygon
	{
		float mHighestSideYValue;
		float mRadius;
		Vectormath::Vector2 mCenter;
		unsigned __int64 mMinXSideIndex;
		eastl::vector<Vectormath::Vector2> mPoints;
	};

	struct Thing_partial : sgg::IRenderComponent
	{
		char pad_0[0x8];							// 0x40
		int mId;                        			// 0x48
		sgg::HashGuid mName;						// 0x4C
		char pad_1[0x04];               			// 0x50..0x53
		float mZLocation;               			// 0x54
		char pad_2[0x18];               			// 0x58..0x6F
		Vectormath::Vector2 mLocation;  			// 0x70
		char pad_3[0x40];               			// 0x78..0xB7
		void *pPhysics;                 			// 0xB8
		char pad_4[0xC0];							// 0XC0..0x17F
		eastl::optional<sgg::Polygon> mGeometry;	// 0x180
		char pad_5[0x490];							// 0x1B8..0x647
		bool mUseScreenLocation;					// 0x648
		bool mFixGeometryWithZ;						// 0x649
		bool mAllow3DShadow;						// 0x64A
		char pad_6[0x5];							// 0x64B..0x64F
	};

	static_assert(offsetof(Thing_partial, mId) == 0x48);
	static_assert(offsetof(Thing_partial, mZLocation) == 0x54);
	static_assert(offsetof(Thing_partial, mLocation) == 0x70);
	static_assert(offsetof(Thing_partial, pPhysics) == 0xB8);
	static_assert(offsetof(Thing_partial, mGeometry) == 0x180);
	static_assert(sizeof(eastl::optional<sgg::Polygon>) == 0x38);
	static_assert(offsetof(Thing_partial, mUseScreenLocation) == 0x648);
	static_assert(offsetof(Thing_partial, mFixGeometryWithZ) == 0x649);
	static_assert(offsetof(Thing_partial, mAllow3DShadow) == 0x64A);
	static_assert(sizeof(Thing_partial) == 0x650);

	struct Obstacle_partial : sgg::Thing_partial
	{
		unsigned __int32 mFlags;		// 0x650
		unsigned __int32 mGridSets;		// 0x654
		char pad_0[0x168];				// 0x658..0x7BF
	};

	static_assert(offsetof(Obstacle_partial, mFlags) == 0x650);
	static_assert(offsetof(Obstacle_partial, mGridSets) == 0x654);
	static_assert(sizeof(Obstacle_partial) == 0x7C0);

	struct __declspec(align(8)) ProjectileDataDef
	{
		sgg::ThingData *mThingData;
		sgg::HashGuid mGroupName;
		bool mDrawAsBeam;
		bool mBeamDieOnTargetLost;
		bool mBeamStartAnimSkipsImpact;
		sgg::HashGuid mTipFx;
		sgg::HashGuid mArmedGraphic;
		sgg::HashGuid mDetonateFx;
		sgg::HashGuid mPreDetonateFx;
		float mPreDetonateFxDuration;
		sgg::HashGuid mDetonateAnimation;
		bool mHideGraphicOnDetonate;
		sgg::HashGuid mPauseAnimation;
		sgg::HashGuid mStartFx;
		bool mRepeatStartFx;
		bool mDrawDuringPause;
		bool mUnpauseResetLocation;
		sgg::HashGuid mReflectFx;
		sgg::SoundCue mSimpleImpactSound;
		sgg::SoundCue mInflictedDamageSound;
		// unimplemented enum sgg::ProjectileDataDef::Allegiance
		int mHomingAllegiance;
		float mSpeedVariance;
		float mSpinRateVariance;
		bool mInheritOwnerElapsedTimeMultiplier;
		bool mUseAttachedToTimeMultiplier;
		float mTimeModifierFraction;
		float mMinVictimZ;
		float mMaxVictimZ;
		sgg::HashGuid mImmunityKey;
		float mImmunityDuration;
		bool mRetainDetonationHitRecord;
		bool mCanHitWithoutDamage;
		bool mCheckUnitImpact;
		bool mCheckObstacleImpact;
		bool mCheckObstacleImpactOnDetonation;
		float mIgnoreObstacleImpactDuration;
		bool mCheckProjectileImpact;
		sgg::SoundCue mProjectileImpactSound;
		int mSoundManagerCap;
		bool mUnlimitedUnitPenetration;
		bool mUnlimitedObstaclePenetration;
		bool mDetonateOnImpact;
		bool mMultiDetonate;
		int mMaxDetonations;
		bool mDetonateOnTouchdown;
		sgg::SoundCue mDetonateSound;
		float mStopSoundOnFinishFade;
		//unimplemented enum sgg::ProjectileDataDef::ProjectileType
		int mType;
		float mDamage;
		float mDamageMultiplier;
		float mDamagePerConsecutiveHit;
		float mDamagePerConsecutiveHitFloor;
		bool mRoundDamageDown;
		float mArmedExpirationDamage;
		bool mDetonateWhenArmed;
		float mConsecutiveHitWindow;
		float mDamageRadius;
		float mDamageRadiusScaleX;
		float mDamageRadiusScaleY;
		float mHollowBlastRadiusBand;
		bool mDetonateLineOfSight;
		bool mDetonateLineOfSightFromOwner;
		bool mImpactLineOfSight;
		bool mImpactLineOfSightFromOwner;
		//unimplemented enum sgg::CollisionCheckType
		int mUnitCollisionCheck;
		//unimplemented enum sgg::CollisionCheckType
		int mObstacleCollisionCheck;
		bool mRequireHitCenter;
		bool mUseArmor;
		bool mUseVulnerability;
		float mDamageFalloff;
		sgg::HashGuid mIgnoreTargetsWithGroup;
		sgg::HashGuid mRequireTargetsHaveEffect;
		bool mAffectsFriends;
		bool mAffectsEnemies;
		bool mAffectsSelf;
		sgg::HashGuid mDoesNotAffectName;
		float mRange;
		float mMinRange;
		float mMaxRange;
		float mSpeed;
		float mMaxSpeed;
		float mSpeedScaleY;
		float mSpeedZ;
		float mAccelerationZ;
		float mAcceleration;
		float mActiveBlinkSpeedMultiplier;
		float mActiveBlinkRangeMultiplier;
		float mInheritOwnerVelocityMultiplier;
		float mSpinFromOwnerVelocityMultiplier;
		bool mExpandBlastDuringDetonation;
		float mBlastStartRadius;
		float mBlastSpeed;
		float mBlastSpeedAcceleration;
		float mBlastDuration;
		float mSpeedMultiplierOfEnemyProjectilesInside;
		float mMaxSize;
		float mSizeDuration;
		float mMaxBlastModifier;
		float mBlastModifierDuration;
		float mMaxAdjustRate;
		float mAdjustRateAcceleration;
		float mSpinRate;
		bool mAllowTargetInvulnerable;
		bool mManuallySetTarget;
		bool mTargetOwnerWithoutTarget;
		//unimplemented enum sgg::ProjectileDataDef::JumpType
		int mJumpType;
		int mNumJumps;
		float mNoJumpTargetRandomSpread;
		bool mFinalJumpToOwner;
		float mReturnToOwnerSpeed;
		float mReturnToOwnerAfterInactiveSeconds;
		float mReverseAfterDuration;
		bool mReturnToOwnerRecallOnly;
		bool mReturnToOwnerBlockReset;
		int mNumPenetrations;
		bool mExcludeLastHitThing;
		bool mRepeatHitOnReturn;
		bool mMultipleUnitCollisions;
		bool mMultipleObstacleCollisions;
		bool mResetCollisionOutsideDetonation;
		bool mResetCollisionOutsideImpact;
		float mJumpRange;
		float mJumpSpeedMultiplier;
		bool mAllowRepeatedJumpHit;
		bool mAllowRepeatedOwnerJumpHit;
		bool mIgnoreDodge;
		bool mIgnoreCoverageAngles;
		bool mIgnoreCoverageAngleAdjustment;
		bool mIgnoreProjectilesNeverJump;
		bool mCanBeReflected;
		bool mCanBeProjectileDefenseDestroyed;
		sgg::HashGuid mCanBeProjectileDefenseDestroyedByName;
		sgg::HashGuid mCanBeProjectileDefenseDestroyedByName2;
		sgg::HashGuid mCanBeProjectileDefenseDestroyedByLayer;
		float mStartDelay;
		float mArcStart;
		float mArcEnd;
		float mArcScaleY;
		float mArcSize;
		float mBlastTimeBlockVfxThreshold;
		float mFuse;
		float mFuseStart;
		float mTotalFuse;
		float mMoveInterval;
		bool mStartFuseOnTouchdown;
		float mFlashBeforeExpireDuration;
		sgg::HashGuid mDissipateFx;
		sgg::HashGuid mDeathFx;
		sgg::HashGuid mArmedDeathFx;
		bool mDetonatesProjectilesOnLayer;
		sgg::HashGuid mCollisionLayer;
		sgg::HashGuid mSpawnOnDetonate;
		sgg::HashGuid mSpawnOnDeath;
		sgg::HashGuid mSpawnOnDissipate;
		bool mSilentImpactOnInvulnerable;
		bool mSilentImpactOnProjectileAlwaysPenetrate;
		bool mSkipSpawnsIfBlocked;
		float mSkipSpawnsIfAwayFromPlayer;
		bool mSpawnsInheritAttach;
		uint8_t mSpawnType;
		sgg::HashGuid mSpawnGroup;
		bool mSpawnsInheritGroups;
		int mSpawnCount;
		int mSpawnCap;
		bool mFizzleOldSpawnsOnDetonate;
		bool mSpawnsEvenlySpaced;
		float mSpawnArc;
		float mSpawnsSpeedModifierScaleY;
		bool mAttachToOwner;
		bool mIgnoreCancelAttachedProjectiles;
		bool mRotateWithOwner;
		bool mSkipFireCancelIfFiring;
		bool mMatchOwnerBody;
		bool mUseOwnerAngle;
		bool mUseDirectionAngle;
		sgg::HashGuid mAttachToMarker;
		bool mSpawnAtVictimLocation;
		bool mDetonateAtVictimLocation;
		float mSelfImpactVelocityMultiplier;
		float mImpactVelocity;
		float mImpactVelocityFalloff;
		float mImpactVelocitySpawnProximityExclusion;
		float mImpactVelocityAngleOffset;
		bool mUseRadialImpact;
		float mInvulnerableImpactOwnerVelocity;
		float mProjectileDefenseRadius;
		bool mUseDetonationForProjectileDefense;
		bool mProjectileDefenseBlockSpawns;
		float mProjectileDefenseAngle;
		bool mDeflectProjectiles;
		bool mUseStartLocation;
		float mSpawnRadius;
		bool mGaussianSpawnDistribution;
		float mGravity;
		bool mAutoAdjustForTarget;
		bool mEndBeamAtTarget;
		float mLaunchAngle;
		float mLaunchDistanceFraction;
		float mDescentTime;
		float mHangTime;
		sgg::HashGuid mReticleGraphic;
		bool mClearOnAttackEffects;
		bool mDetonateAtTarget;
		sgg::HashGuid mStartAtMarker;
		bool mSetAnimRadialOffset;
		bool mAnimIgnoreBlastRadius;
		sgg::HashGuid mAddControlOnDeath;
		sgg::HashGuid mImpactFx;
		sgg::HashGuid mArmedImpactFx;
		sgg::HashGuid mBounceFx;
		bool mRestartAnimOnBounce;
		sgg::HashGuid mDescentStartFx;
		sgg::HashGuid mInflictedDamageFx;
		bool mDieWithOwner;
		//undefined type eastl::vector<sgg::EffectData>
		eastl::vector<char> mEffects;
	};

	static_assert(sizeof(ProjectileDataDef) == 0x2F8);

	struct ProjectileData : sgg::GameData
	{
		sgg::ProjectileDataDef mDef;
	};

	struct Projectile : sgg::Thing_partial
	{
		bool mHitMaxRange;
		bool mHasStarted;
		bool mUseStartMarker;
		bool mForceOwnerReturn;
		bool mDetonated;
		bool mBlockSpawns;
		bool mRetargetDirty;
		bool mBlinkWeaponActive;
		bool mTriggerFireEndOnDeath;
		bool mExcludeFromCap;
		float mDamageMultiplier;
		int mOwnerId;
		int mFireIndex;
		int mVolley;
		int mWaveIndex;
		bool mBonusWave;
		int mWaveNumProjectiles;
		int mOwnerSpawnedById;
		int mNumJumps;
		bool mArmed;
		bool mWasFirePerfectCharge;
		bool mFiredByTraitor;
		float mRange;
		float mSpeedMultiplier;
		float mRangeSqrd;
		float mBlastRadius;
		float mBlastSpeed;
		float mBlastTimeRemaining;
		float mBlastRadiusModifier;
		float mBlastRadiusModifierTarget;
		float mBlastRadiusModifierChangeSpeed;
		float mIgnoreObstacleImpactTimeRemaining;
		float mReturnTimeRemaining;
		float mPauseTimeRemaining;
		float mSpawnAngle;
		Vectormath::Vector2 mImpactLocation;
		Vectormath::Vector2 mTargetLocation;
		float mSpinRate;
		float mArcStart;
		float mArcEnd;
		sgg::HashGuid mDeathGraphic;
		//sgg::ProjectileData mData;
		char mData[0x310];
		//sgg::Weapon *pFiredBy;
		void* pFiredBy;
		eastl::vector<sgg::Entity> mThingsHit;
		eastl::vector<sgg::Entity> mProjectilesHit;
		eastl::hash_set<sgg::HashGuid> mHitByImpact;
		eastl::hash_set<sgg::HashGuid> mHitByDetonation;
		eastl::hash_set<sgg::HashGuid> mCollideWithGroups;
		bool mFlipAllegiance;
		bool mExpired;
		bool mExpireSilently;
		int mNumPenetrations;
		int mDetonations;
		int mDetonateSoundId;
		float mCurrentDistanceSqrd;
		float mSpeed;
		float mMaxSpeed;
		float mSpeedZ;
		float mAcceleration;
		float mLifeRemaining;
		float mFuseRemaining;
		float mMoveIntervalRemaining;
		float mStartDelay;
		int mLastReflectedByThingId;
		Vectormath::Vector2 mPrevLocation;
		sgg::HashGuid mCachedOwnerName;
	};

	static_assert(sizeof(Projectile) == 0xAF8);

	// TODO: this is legit too time consuming.

	/*
	struct IThingComponent_vtbl
	{
		void(__fastcall * dctor)(sgg::IThingComponent * this_);
		void(__fastcall * Reset)(sgg::IThingComponent * this_);
	};

	struct IThingComponent
	{
		sgg::IThingComponent_vtbl *__vftable;
		bool mIsEnabled;
		sgg::Thing *pOwner;
		eastl::basic_string<char, eastl::allocator_forge> mName;
	};

	struct ThingComponent : sgg::IThingComponent
	{
	};
	*/

	/*struct __declspec(align(16)) BodyComponent : sgg::ThingComponent
	{
		eastl::vector<sgg::BodyComponentTransitionHelper *, eastl::allocator_forge> mActiveTransitions;
		float mScale;
		float mSquashStretchScale;
		float mSquashStretchAngle;
		float mAngle;
		float mAmbient;
		Vectormath::Vector2 mScaleVector;
		sgg::RenderMeshType mMeshType;
		uint8_t mFlips[1];
		sgg::Color mBaseColor;
		Vectormath::SSE::Vector3 mHsv;
		float mIntensity;
		sgg::BodyComponentTransitionHelper mAngleTransition;
		sgg::BodyComponentTransitionHelper mScaleTransition;
		sgg::BodyComponentTransitionHelper mScaleXTransition;
		sgg::BodyComponentTransitionHelper mScaleYTransition;
		sgg::BodyComponentTransitionHelper mRedTransition;
		sgg::BodyComponentTransitionHelper mGreenTransition;
		sgg::BodyComponentTransitionHelper mBlueTransition;
		sgg::BodyComponentTransitionHelper mAlphaTransition;
		float mRed;
		float mGreen;
		float mBlue;
		float mAlpha;
		sgg::BodyComponentTransitionHelper mHueTransition;
		sgg::BodyComponentTransitionHelper mSaturationTransition;
		sgg::BodyComponentTransitionHelper mValueTransition;
	};

	struct __declspec(align(8)) Thing : sgg::IRenderComponent
	{
		bool mVisible;
		bool mAnimFreeze;
		bool mSuppressSounds;
		bool mIsDisposed;
		int mId;
		sgg::HashGuid mName;
		float mAttachOffsetZ;
		float mZLocation;
		float mTallness;
		float mTimeModifierFraction;
		float mElapsedTimeMultiplier;
		float mSpawnTime;
		Vectormath::Vector2 mLocation;
		Vectormath::Vector2 mSpawnLocation;
		sgg::IRectangle mRectangle;
		Vectormath::SSE::Vector3 mDirection;
		sgg::ThingData *pThingData;
		sgg::BodyComponent *pBody;
		sgg::LifeComponent *pLife;
		sgg::PhysicsComponent *pPhysics;
		sgg::VacuumComponent *pVacuum;
		sgg::MagnetismComponent *pMagnetism;
		sgg::LightOccluderComponent *pOccluder;
		sgg::TranslateComponent *pTranslate;
		sgg::PlayerNearbyComponent *pPlayerNearby;
		sgg::FlashComponent *pFlash;
		sgg::ShakeComponent *pShake;
		sgg::TextComponent *pText;
		sgg::SpeechComponent *pSpeech;
		sgg::Interact *pInteraction;
		sgg::Animation *pAnim;
		sgg::EntityLinkedObject<sgg::Thing> mAttachedTo;
		LuaTable mAttachedLua;
		eastl::vector<int, eastl::allocator_forge> mAttachmentIds;
		eastl::vector<sgg::Animation *, eastl::allocator_forge> mFrontAnims;
		eastl::vector<sgg::Animation *, eastl::allocator_forge> mBackAnims;
		eastl::optional<sgg::Polygon> mGeometry;
		sgg::Light *pLight;
		Vectormath::Vector2 mAttachOffset;
		eastl::basic_string<char, eastl::allocator_forge> mAsString;
		sgg::Polygon mMotionTestingPoly;
		sgg::GrannyModel mGrannyModel;
		sgg::MapThing *pMapThing;
		sgg::ThingManager *pManager;
		int *pRef;
		volatile unsigned int mPrepped;
		float mParallax;
		float mLastImpulsedAnimTime;
		int mAmbientSoundId;
		sgg::LifeStatus mLifeStatus;
		eastl::optional<sgg::TransitionHelper<float> > mAdjustZ;
		eastl::optional<sgg::TransitionHelper<float> > mAdjustParallax;
		eastl::vector<sgg::HashGuid, eastl::allocator_forge> mGroupNames;
		eastl::vector<sgg::ThingComponent *, eastl::allocator_forge> mComponents;
		eastl::vector<sgg::Animation *, eastl::allocator_forge> mAttachedAnims;
		bool mUseScreenLocation;
		bool mFixGeometryWithZ;
		bool mAllow3DShadow;
	};*/
} // namespace sgg
