#include "bink_flash.hpp"

#include "hades2/pdb_symbol_map.hpp"
#include "safetyhook.hpp"

#include <array>
#include <cstring>

namespace big::hades::bink_flash
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

		enum class Shader : std::uint8_t
		{
			Bink            = 1,
			Flasher         = 5,
			FlasherAddColor = 6,
		};

		constexpr std::int32_t kDoDrawDispatchOffset = 0x3'0A;
		constexpr std::array<std::uint8_t, 12> kExpectedDispatchBytes = {0x8B, 0x8C, 0x82, 0x48, 0x83, 0x1E, 0x00, 0x48, 0x03, 0xCA, 0xFF, 0xE1};

		SafetyHookMid g_bink_flash_dispatch_hook;

		void hook_bink_flash_dispatch(SafetyHookContext& context)
		{
			const auto* drawable = reinterpret_cast<const DrawableView*>(context.rdi);
			if ((context.r14 & 0xFFu) != static_cast<std::uint8_t>(Shader::Bink) || drawable->pMovie == nullptr)
			{
				return;
			}

			switch (static_cast<Shader>(drawable->mShader))
			{
			case Shader::Flasher:
				// The switch table uses the original shader value minus one while R14 remains Bink for YRB plane binding
				context.rax = std::to_underlying(Shader::Flasher) - 1;
				break;
			case Shader::FlasherAddColor: context.rax = std::to_underlying(Shader::FlasherAddColor) - 1; break;
			default:                      break;
			}
		}
	} // namespace

	void init_hooks()
	{
		const auto do_draw = hades2_symbol_to_address["sgg::DrawManager::DoDraw"];
		if (!do_draw)
		{
			LOG(WARNING) << "Bink flasher hook disabled because DrawManager::DoDraw is unavailable";
			return;
		}

		const auto hook_site = do_draw.offset(kDoDrawDispatchOffset);
		if (std::memcmp(hook_site.as<void*>(), kExpectedDispatchBytes.data(), kExpectedDispatchBytes.size()) != 0)
		{
			LOG(WARNING) << "Bink flasher hook disabled because DrawManager::DoDraw has an unexpected layout";
			return;
		}

		g_bink_flash_dispatch_hook = safetyhook::create_mid(hook_site.as<void*>(), hook_bink_flash_dispatch);
	}
} // namespace big::hades::bink_flash
