#include "bink_rendering.hpp"

#include "hades2/pdb_symbol_map.hpp"
#include "safetyhook.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace big::hades::bink_rendering
{
	namespace
	{
		struct DrawableView
		{
			std::byte pad_000[0x98];
			std::uint8_t mShader;
			std::byte pad_099[0x87];
			void* pMovie;
		};

		static_assert(offsetof(DrawableView, mShader) == 0x98);
		static_assert(offsetof(DrawableView, pMovie) == 0x1'20);

		struct RenderMeshView
		{
			std::byte pad_000[0x30];
			void* mTexturePtr;
		};

		static_assert(offsetof(RenderMeshView, mTexturePtr) == 0x30);

		struct TextureDescView
		{
			std::byte pad_000[0x20];
			std::uint32_t mFormat;
			std::byte pad_024[0x3D];
			bool mSimultaneosAccess;
		};

		static_assert(offsetof(TextureDescView, mFormat) == 0x20);
		static_assert(offsetof(TextureDescView, mSimultaneosAccess) == 0x61);

		enum class Shader : std::uint8_t
		{
			Bink            = 1,
			Outline         = 2,
			Flasher         = 5,
			FlasherAddColor = 6,
		};

		constexpr std::int32_t kDoDrawDispatchOffset                  = 0x3'0A;
		constexpr std::array<std::uint8_t, 3> kExpectedDispatchPrefix = {0x8B, 0x8C, 0x82};
		constexpr std::array<std::uint8_t, 5> kExpectedDispatchSuffix = {0x48, 0x03, 0xCA, 0xFF, 0xE1};

		constexpr std::int32_t kAddTextureComponentMappingOffset = 0x7'E8;
		constexpr std::array<std::uint8_t, 13> kExpectedComponentMappingBytes = {0xC7, 0x45, 0xF0, 0x88, 0x16, 0x00, 0x00, 0x8D, 0x42, 0xED, 0x83, 0xF8, 0x25};
		constexpr std::uint32_t kR8UnormFormat         = 7;
		constexpr std::uint32_t kBinkRedAsAlphaMapping = 0x10'88;

		SafetyHookMid g_bink_shader_dispatch_hook;
		SafetyHookMid g_bink_alpha_component_mapping_hook;

		void hook_bink_shader_dispatch(SafetyHookContext& context)
		{
			const auto* drawable = reinterpret_cast<const DrawableView*>(context.rdi);
			if ((context.r14 & 0xFFu) != static_cast<std::uint8_t>(Shader::Bink) || drawable->pMovie == nullptr)
			{
				return;
			}

			switch (static_cast<Shader>(drawable->mShader))
			{
			case Shader::Outline:
			{
				auto* mesh        = reinterpret_cast<RenderMeshView*>(context.r13);
				mesh->mTexturePtr = reinterpret_cast<void*>(context.r15);
				context.rax       = std::to_underlying(Shader::Outline) - 1;
				context.r14       = (context.r14 & ~0xFFull) | std::to_underlying(Shader::Outline);
				break;
			}
			case Shader::Flasher:
				// The switch table uses the original shader value minus one while R14 remains Bink for YRB plane binding
				context.rax = std::to_underlying(Shader::Flasher) - 1;
				break;
			case Shader::FlasherAddColor: context.rax = std::to_underlying(Shader::FlasherAddColor) - 1; break;
			default:                      break;
			}
		}

		void hook_bink_alpha_component_mapping(SafetyHookContext& context)
		{
			const auto* texture_desc = reinterpret_cast<const TextureDescView*>(context.r14);
			if (texture_desc->mFormat != kR8UnormFormat || !texture_desc->mSimultaneosAccess)
			{
				return;
			}

			auto* component_mapping = reinterpret_cast<std::uint32_t*>(context.rbp - 0x10);
			*component_mapping      = kBinkRedAsAlphaMapping;
		}
	} // namespace

	void init_hooks()
	{
		const auto do_draw = hades2_symbol_to_address["sgg::DrawManager::DoDraw"];
		if (!do_draw)
		{
			LOG(WARNING) << "Bink shader dispatch hook disabled because DrawManager::DoDraw is unavailable";
		}
		else
		{
			const auto hook_site = do_draw.offset(kDoDrawDispatchOffset);
			if (std::memcmp(hook_site.as<void*>(), kExpectedDispatchPrefix.data(), kExpectedDispatchPrefix.size()) != 0
			    || std::memcmp(hook_site.offset(7).as<void*>(), kExpectedDispatchSuffix.data(), kExpectedDispatchSuffix.size()) != 0)
			{
				LOG(WARNING)
				    << "Bink shader dispatch hook disabled because DrawManager::DoDraw has an unexpected layout";
			}
			else
			{
				g_bink_shader_dispatch_hook = safetyhook::create_mid(hook_site.as<void*>(), hook_bink_shader_dispatch);
			}
		}

		const auto add_texture = hades2_symbol_to_address["addTexture"];
		if (!add_texture)
		{
			LOG(WARNING) << "Bink alpha component mapping hook disabled because addTexture is unavailable";
		}
		else
		{
			const auto validation_site = add_texture.offset(kAddTextureComponentMappingOffset - 7);
			if (std::memcmp(validation_site.as<void*>(),
			                kExpectedComponentMappingBytes.data(),
			                kExpectedComponentMappingBytes.size())
			    != 0)
			{
				LOG(WARNING)
				    << "Bink alpha component mapping hook disabled because addTexture has an unexpected layout";
			}
			else
			{
				const auto hook_site = add_texture.offset(kAddTextureComponentMappingOffset);
				g_bink_alpha_component_mapping_hook = safetyhook::create_mid(hook_site.as<void*>(), hook_bink_alpha_component_mapping);
			}
		}
	}
} // namespace big::hades::bink_rendering
