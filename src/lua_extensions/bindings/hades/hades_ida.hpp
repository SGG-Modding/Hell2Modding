#pragma once

#include <EASTL/vector.h>

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
		//sgg::Rtti_vtbl *__vftable /*VFT*/;
		//const sgg::Rtti__Type mType;
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
