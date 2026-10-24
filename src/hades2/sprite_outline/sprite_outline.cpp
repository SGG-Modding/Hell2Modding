#include "sprite_outline.hpp"

#include "hades2/pdb_symbol_map.hpp"
#include "hooks/hooking.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <unordered_set>

namespace big::hades::sprite_outline
{
	namespace
	{
		struct AnimationView
		{
			void** pVtable;
			std::byte pad_008[0xA0];
			AnimationView* pChildAnimation;
		};

		static_assert(offsetof(AnimationView, pVtable) == 0x00);
		static_assert(offsetof(AnimationView, pChildAnimation) == 0xA8);

		struct ThingDataView
		{
			std::byte pad_000[0x4C];
			std::uint32_t mGrannyModel;
		};

		static_assert(offsetof(ThingDataView, mGrannyModel) == 0x4C);

		struct ThingView
		{
			std::byte pad_000[0x20];
			bool mHasOutline;
			std::byte pad_021[0x7F];
			ThingDataView* pThingData;
			std::byte pad_0A8[0x68];
			AnimationView* pAnim;
			std::byte pad_118[0xB4];
			std::uint32_t mOutlineColor;
			std::byte pad_1D0[0x60];
			std::uint32_t mGrannyModelFileName;
		};

		static_assert(offsetof(ThingView, mHasOutline) == 0x20);
		static_assert(offsetof(ThingView, pThingData) == 0xA0);
		static_assert(offsetof(ThingView, pAnim) == 0x1'10);
		static_assert(offsetof(ThingView, mOutlineColor) == 0x1'CC);
		static_assert(offsetof(ThingView, mGrannyModelFileName) == 0x2'30);

		struct ColorView
		{
			std::uint32_t value;
		};

		struct LuaTableView
		{
			void* state;
			std::int32_t reference;
			std::uint32_t pad_00C;
		};

		static_assert(sizeof(LuaTableView) == 0x10);

		struct ScriptArgsView
		{
			LuaTableView table;
			std::byte pad_010[0x68];
		};

		static_assert(sizeof(ScriptArgsView) == 0x78);

		template<typename T>
		struct NativeVectorView
		{
			T* mpBegin;
			T* mpEnd;
			T* mpCapacityEnd;
		};

		static_assert(sizeof(NativeVectorView<void*>) == 0x18);

		struct OutlineInfoView
		{
			bool mAsOverlay;
			bool mAllAnimations;
			bool mIsInfinite;
			std::byte pad_003;
			ColorView mColor;
			float mOpacity;
			float mDuration;
			float mThickness;
			float mThreshold;
			float mFadeInDuration;
			float mFadeInTime;
			ThingView* pThing;
		};

		static_assert(sizeof(OutlineInfoView) == 0x28);

		struct OutlineSystemView
		{
			CRITICAL_SECTION mMutex;
			bool mFulfillingRequests;
			std::byte pad_029[0x07];
			NativeVectorView<OutlineInfoView> mToDrawAsOverlays;
			NativeVectorView<OutlineInfoView> mRequests;
			NativeVectorView<OutlineInfoView> mScratchList;
			std::byte mToDrawInline[0x30];
		};

		static_assert(offsetof(OutlineSystemView, mFulfillingRequests) == 0x28);
		static_assert(offsetof(OutlineSystemView, mToDrawAsOverlays) == 0x30);
		static_assert(offsetof(OutlineSystemView, mRequests) == 0x48);
		static_assert(offsetof(OutlineSystemView, mToDrawInline) == 0x78);
		static_assert(sizeof(OutlineSystemView) == 0xA8);

		struct WorldView
		{
			std::byte pad_000[0x50];
			OutlineSystemView* pOutlineSystem;
		};

		static_assert(offsetof(WorldView, pOutlineSystem) == 0x50);

		constexpr std::uintptr_t kPushOutlineRequestRva = 0x29'5F'B0;
		constexpr std::array<std::uint8_t, 12> kExpectedPushOutlineRequestBytes = {0x40, 0x53, 0x55, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x59, 0x08};
		constexpr std::uintptr_t kEraseInlineOutlineRva = 0x29'61'70;
		constexpr std::array<std::uint8_t, 16> kExpectedEraseInlineOutlineBytes = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x4C};

		using GetColorFn             = void (*)(ScriptArgsView*, ColorView*);
		using GetFloatFn             = float (*)(LuaTableView*, const char*, float);
		using GetBoolFn              = bool (*)(ScriptArgsView*, const char*, bool);
		using GetIdsFn               = NativeVectorView<int>* (*)(ScriptArgsView*);
		using GetActiveThingFn       = ThingView* (*)(WorldView*, int);
		using HasModelAnimationFn    = bool (*)(ThingView*);
		using PushOutlineRequestFn   = void (*)(NativeVectorView<OutlineInfoView>*, const OutlineInfoView*);
		using FastRemoveOutlineFn    = void (*)(ThingView*, NativeVectorView<OutlineInfoView>*);
		using EraseInlineOutlineFn   = std::size_t (*)(void*, ThingView* const*);
		using AnimationDrawOutlineFn = void (*)(AnimationView*, std::uint32_t, float, float, bool);

		GetColorFn g_get_color;
		GetFloatFn g_get_float;
		GetBoolFn g_get_bool;
		GetIdsFn g_get_ids;
		GetActiveThingFn g_get_active_thing;
		HasModelAnimationFn g_has_model_animation;
		PushOutlineRequestFn g_push_outline_request;
		FastRemoveOutlineFn g_fast_remove_outline;
		EraseInlineOutlineFn g_erase_inline_outline;
		WorldView** g_world;

		std::unordered_set<ThingView*> g_highlight_top_animation_only;
		std::shared_mutex g_highlight_top_animation_only_mutex;

		void set_highlight_top_animation_only(ThingView* thing, bool enabled)
		{
			std::unique_lock lock(g_highlight_top_animation_only_mutex);
			if (enabled)
			{
				g_highlight_top_animation_only.insert(thing);
			}
			else
			{
				g_highlight_top_animation_only.erase(thing);
			}
		}

		void hook_add_outline(ScriptArgsView* args)
		{
			ColorView color{};
			g_get_color(args, &color);
			if (color.value == 0)
			{
				color.value = 0xFF'FF'FF'FF;
			}

			const auto opacity                      = g_get_float(&args->table, "Opacity", 0.0f);
			const auto thickness                    = g_get_float(&args->table, "Thickness", 0.0f);
			const auto threshold                    = g_get_float(&args->table, "Threshold", 0.0f);
			const auto duration                     = g_get_float(&args->table, "Duration", -1.0f);
			const auto fade_in_duration             = g_get_float(&args->table, "FadeInDuration", -1.0f);
			const auto as_overlay                   = g_get_bool(args, "AsOverlay", false);
			const auto all_animations               = g_get_bool(args, "AllAnimations", false);
			const auto highlight_top_animation_only = g_get_bool(args, "HighlightTopAnimOnly", false);

			auto* world          = *g_world;
			auto* outline_system = world->pOutlineSystem;
			const auto* ids      = g_get_ids(args);
			for (auto* id = ids->mpBegin; id != ids->mpEnd; ++id)
			{
				auto* thing = g_get_active_thing(world, *id);
				if (thing == nullptr)
				{
					continue;
				}

				thing->mOutlineColor = color.value;
				if (thing->mGrannyModelFileName != 0
				    || (thing->pThingData != nullptr && thing->pThingData->mGrannyModel != 0)
				    || g_has_model_animation(thing))
				{
					set_highlight_top_animation_only(thing, false);
					continue;
				}

				set_highlight_top_animation_only(thing, highlight_top_animation_only);
				if (outline_system->mFulfillingRequests)
				{
					continue;
				}

				const auto overlay_count = outline_system->mToDrawAsOverlays.mpBegin == nullptr ?
				                               0 :
				                               static_cast<std::size_t>(outline_system->mToDrawAsOverlays.mpEnd
				                                                        - outline_system->mToDrawAsOverlays.mpBegin);
				if (overlay_count >= 16)
				{
					continue;
				}

				OutlineInfoView request{
				    .mAsOverlay      = as_overlay,
				    .mAllAnimations  = all_animations,
				    .mIsInfinite     = duration < 0.0f,
				    .mColor          = color,
				    .mOpacity        = opacity,
				    .mDuration       = duration,
				    .mThickness      = thickness,
				    .mThreshold      = threshold,
				    .mFadeInDuration = fade_in_duration,
				    .mFadeInTime     = 0.0f,
				    .pThing          = thing,
				};

				EnterCriticalSection(&outline_system->mMutex);
				bool replaced_request = false;
				for (auto* existing = outline_system->mRequests.mpBegin; existing != outline_system->mRequests.mpEnd; ++existing)
				{
					if (existing->pThing == thing)
					{
						*existing        = request;
						replaced_request = true;
						break;
					}
				}
				if (!replaced_request)
				{
					g_push_outline_request(&outline_system->mRequests, &request);
				}
				LeaveCriticalSection(&outline_system->mMutex);
			}
		}

		void hook_remove_outline(ScriptArgsView* args)
		{
			auto* world          = *g_world;
			auto* outline_system = world->pOutlineSystem;
			const auto* ids      = g_get_ids(args);
			for (auto* id = ids->mpBegin; id != ids->mpEnd; ++id)
			{
				auto* thing = g_get_active_thing(world, *id);
				if (thing == nullptr)
				{
					continue;
				}

				thing->mOutlineColor = 0xFF'00'00'00;
				set_highlight_top_animation_only(thing, false);

				EnterCriticalSection(&outline_system->mMutex);
				g_fast_remove_outline(thing, &outline_system->mRequests);
				LeaveCriticalSection(&outline_system->mMutex);

				g_fast_remove_outline(thing, &outline_system->mToDrawAsOverlays);
				ThingView* key = thing;
				g_erase_inline_outline(outline_system->mToDrawInline, &key);
				thing->mHasOutline = false;
			}
		}

		void hook_world_on_disposed(WorldView* world, void* component)
		{
			set_highlight_top_animation_only(reinterpret_cast<ThingView*>(component), false);
			big::g_hooking->get_original<hook_world_on_disposed>()(world, component);
		}

		void hook_thing_draw_outline(ThingView* thing, std::uint32_t color, float thickness, float threshold, bool as_overlay, bool all_animations)
		{
			bool highlight_top_animation_only;
			{
				std::shared_lock lock(g_highlight_top_animation_only_mutex);
				highlight_top_animation_only = g_highlight_top_animation_only.contains(thing);
			}
			if (highlight_top_animation_only && thing->pAnim != nullptr)
			{
				auto* animation = thing->pAnim;
				while (animation->pChildAnimation != nullptr)
				{
					animation = animation->pChildAnimation;
				}

				const auto draw_outline = reinterpret_cast<AnimationDrawOutlineFn>(animation->pVtable[0x70 / sizeof(void*)]);
				draw_outline(animation, color, thickness, threshold, as_overlay);
				return;
			}

			big::g_hooking->get_original<hook_thing_draw_outline>()(thing, color, thickness, threshold, as_overlay, all_animations);
		}
	} // namespace

	void init_hooks()
	{
		const auto add_outline          = hades2_symbol_to_address["sgg::ScriptAction::AddOutline"];
		const auto remove_outline       = hades2_symbol_to_address["sgg::ScriptAction::RemoveOutline"];
		const auto get_color            = hades2_symbol_to_address["sgg::ScriptArgs::GetColor"];
		const auto get_float            = hades2_symbol_to_address["sgg::LuaTableExtensions::Float"];
		const auto get_bool             = hades2_symbol_to_address["sgg::ScriptArgs::Bool"];
		const auto get_ids              = hades2_symbol_to_address["sgg::ScriptArgs::Ids"];
		const auto get_active_thing     = hades2_symbol_to_address["sgg::World::GetActiveThing"];
		const auto has_model_animation  = hades2_symbol_to_address["sgg::Thing::HasModelAnimation"];
		const auto fast_remove_outline  = hades2_symbol_to_address["sgg::OutlineSystem::FastRemove"];
		const auto world_on_disposed    = hades2_symbol_to_address["sgg::World::OnDisposed"];
		const auto thing_draw_outline   = hades2_symbol_to_address["sgg::Thing::DrawOutline"];
		const auto world                = hades2_symbol_to_address["sgg::world"];
		const auto module_base          = reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr));
		const auto push_outline_request = reinterpret_cast<void*>(module_base + kPushOutlineRequestRva);
		const auto erase_inline_outline = reinterpret_cast<void*>(module_base + kEraseInlineOutlineRva);

		const bool has_outline_action_symbols = add_outline && remove_outline && get_color && get_float && get_bool && get_ids && get_active_thing && has_model_animation && fast_remove_outline && world_on_disposed && thing_draw_outline && world;
		const bool has_outline_helper_layouts =
		    std::memcmp(push_outline_request, kExpectedPushOutlineRequestBytes.data(), kExpectedPushOutlineRequestBytes.size()) == 0
		    && std::memcmp(erase_inline_outline,
		                   kExpectedEraseInlineOutlineBytes.data(),
		                   kExpectedEraseInlineOutlineBytes.size())
		           == 0;
		if (!has_outline_action_symbols)
		{
			LOG(WARNING) << "Sprite outline compatibility hooks disabled because required PDB symbols are unavailable";
			return;
		}
		if (!has_outline_helper_layouts)
		{
			LOG(WARNING) << "Sprite outline compatibility hooks disabled because native OutlineSystem helpers have an "
			                "unexpected layout";
			return;
		}

		g_get_color            = get_color.as_func<void(ScriptArgsView*, ColorView*)>();
		g_get_float            = get_float.as_func<float(LuaTableView*, const char*, float)>();
		g_get_bool             = get_bool.as_func<bool(ScriptArgsView*, const char*, bool)>();
		g_get_ids              = get_ids.as_func<NativeVectorView<int>*(ScriptArgsView*)>();
		g_get_active_thing     = get_active_thing.as_func<ThingView*(WorldView*, int)>();
		g_has_model_animation  = has_model_animation.as_func<bool(ThingView*)>();
		g_push_outline_request = reinterpret_cast<PushOutlineRequestFn>(push_outline_request);
		g_fast_remove_outline  = fast_remove_outline.as_func<void(ThingView*, NativeVectorView<OutlineInfoView>*)>();
		g_erase_inline_outline = reinterpret_cast<EraseInlineOutlineFn>(erase_inline_outline);
		g_world                = world.as<WorldView**>();

		hooking::detour_hook_helper::add<hook_add_outline>("Restore AddOutline OutlineSystem requests", add_outline);
		hooking::detour_hook_helper::add<hook_remove_outline>("Restore RemoveOutline OutlineSystem cleanup", remove_outline);
		hooking::detour_hook_helper::add<hook_world_on_disposed>("Clear custom outline state on disposal", world_on_disposed);
		hooking::detour_hook_helper::add<hook_thing_draw_outline>("Restore sprite outline animation selection", thing_draw_outline);
	}
} // namespace big::hades::sprite_outline
