#include <D2RLPlugin/api.h>

#include <Windows.h>

#include "i_hate_tomls_client.h"
#include "loose_ui_setup.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>

namespace {

constexpr std::uintptr_t GetLocalDataContextRva = 0x08B2D0;
constexpr std::uintptr_t GetLocalPlayerRva = 0x09A480;
constexpr std::uintptr_t GetUnitRva = 0x09A5D0;
constexpr std::uintptr_t GetMercenaryIdRva = 0x13ABC0;
constexpr std::uintptr_t FindTopLevelPanelRva = 0x0846170;
constexpr std::uintptr_t FindChildWidgetByNameRva = 0x0856220;
constexpr std::uintptr_t GetWidgetRectRva = 0x08562A0;
constexpr std::int32_t PhysicalResistanceStatId = 36;
constexpr std::int32_t MagicResistanceStatId = 37;
constexpr std::int32_t PhysicalResistanceCap = 50;
constexpr std::int32_t MagicResistanceCap = 75;
constexpr std::uint16_t BaseStatLayer = 0;
constexpr std::size_t NativeWidgetRectOffset = 0x70;
constexpr std::size_t NativeTextPointerOffset = 0x88;
constexpr std::size_t NativeTextBufferBytes = 32;
constexpr DWORD RefreshIntervalMs = 250;

constexpr std::array<std::uint8_t, 32> GetLocalDataContextExpected{
	0x8B, 0x05, 0x2E, 0x84, 0x99, 0x02, 0xC3, 0xCC,
	0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
	0x8B, 0x05, 0x76, 0x84, 0x99, 0x02, 0xC3, 0xCC,
	0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
constexpr std::array<std::uint8_t, 32> GetLocalPlayerExpected{
	0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
	0xEC, 0x20, 0x83, 0xF9, 0x08, 0x0F, 0x83, 0x85,
	0x00, 0x00, 0x00, 0x8B, 0xD9, 0x48, 0x89, 0x5C,
	0x24, 0x38, 0x48, 0x83, 0xFB, 0x08, 0x72, 0x19};
constexpr std::array<std::uint8_t, 6> GetUnitExpected{
	0x4C, 0x63, 0xCA, 0x48, 0x8D, 0x05};
constexpr std::array<std::uint8_t, 12> GetMercenaryIdExpected{
	0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
	0x24, 0x10, 0x57, 0x48};
constexpr std::array<std::uint8_t, 22> FindTopLevelPanelExpected{
	0x48, 0x8B, 0xD1, 0x48, 0x8B, 0x0D, 0xF6, 0x9F,
	0xBF, 0x02, 0x48, 0x85, 0xC9, 0x0F, 0x85, 0xDD,
	0x95, 0x05, 0x00, 0x33, 0xC0, 0xC3};
constexpr std::array<std::uint8_t, 32> FindChildWidgetByNameExpected{
	0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74,
	0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48,
	0x8B, 0x59, 0x58, 0x48, 0x8B, 0xF2, 0x48, 0x8B,
	0x41, 0x60, 0x48, 0x8D, 0x3C, 0xC3, 0x48, 0x3B};
constexpr std::array<std::uint8_t, 32> GetWidgetRectExpected{
	0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x80, 0x79,
	0x52, 0x00, 0x48, 0x8B, 0xDA, 0x74, 0x2A, 0x48,
	0x8B, 0x49, 0x30, 0x48, 0x8D, 0x54, 0x24, 0x20,
	0xE8, 0xE3, 0xFF, 0xFF, 0xFF, 0x33, 0xC0, 0x48};

constexpr char BlankTextReserve[] = "                               ";
static_assert(sizeof(BlankTextReserve) == NativeTextBufferBytes);

constexpr D2RL::PluginInfo PluginInfo{
	.infoSize = D2RL::PluginInfoSize,
	.abiVersion = D2RL_PLUGIN_ABI_VERSION,
	.id = "character-resistance-display",
	.name = "Character Resistance Display",
	.version = "1.1.10",
	.author = "MadMike",
	.description = "Displays physical and magic resistance on the player and mercenary stat panels.",
	.flags = D2RL::PluginFlags::Client | D2RL::PluginFlags::ModScopedOnly | D2RL::PluginFlags::NativeHooks,
};

constexpr char SettingsMenuSchema[] = R"TOML(title = "Character Resistance Display"

[[setting]]
type = "header"
label = "Display switches"

[[setting]]
key = "enabled.character_physical"
type = "bool"
label = "Show Physical Resistance on the character sheet"

[[setting]]
key = "enabled.character_magic"
type = "bool"
label = "Show Magic Resistance on the character sheet"

[[setting]]
key = "enabled.controller_character_physical"
type = "bool"
label = "Show Physical Resistance on the controller character sheet"
note = "Controller support is partial and requires user-supplied modified sprites."

[[setting]]
key = "enabled.controller_character_magic"
type = "bool"
label = "Show Magic Resistance on the controller character sheet"
note = "Controller support is partial and requires user-supplied modified sprites."

[[setting]]
key = "enabled.mercenary_physical"
type = "bool"
label = "Show Physical Resistance on the mercenary panel"
note = "Requires a user-supplied modified mercenary sprite."

[[setting]]
key = "enabled.mercenary_magic"
type = "bool"
label = "Show Magic Resistance on the mercenary panel"
note = "Requires a user-supplied modified mercenary sprite."

[[setting]]
type = "header"
label = "Text positions"

[[setting]]
key = "desktop.physical"
type = "position"
label = "Character sheet: Physical Resistance"
hint = "Moves the label and value together."
min = -8192
max = 7830
default = [93, 1321]

[[setting]]
key = "desktop.magic"
type = "position"
label = "Character sheet: Magic Resistance"
hint = "Moves the label and value together."
min = -8192
max = 7829
default = [598, 1321]

[[setting]]
key = "controller.physical"
type = "position"
label = "Controller sheet: Physical Resistance"
hint = "Moves the label and value together. Requires user-supplied modified sprites."
min = -8192
max = 7700
default = [1989, 842]

[[setting]]
key = "controller.magic"
type = "position"
label = "Controller sheet: Magic Resistance"
hint = "Moves the label and value together. Requires user-supplied modified sprites."
min = -8192
max = 7700
default = [1989, 950]

[[setting]]
key = "mercenary.physical"
type = "position"
label = "Mercenary panel: Physical Resistance"
hint = "Moves the label and value together. Requires a user-supplied modified mercenary sprite."
min = -8192
max = 7862
default = [608, 1435]

[[setting]]
key = "mercenary.magic"
type = "position"
label = "Mercenary panel: Magic Resistance"
hint = "Moves the label and value together. Requires a user-supplied modified mercenary sprite."
min = -8192
max = 7862
default = [608, 1517]
)TOML";

struct RectConfig {
	D2RL::Widgets::Rect rect{};
};

struct LayoutConfig {
	RectConfig physicalLabel;
	RectConfig physicalValue;
	RectConfig magicLabel;
	RectConfig magicValue;
};

struct DisplayToggles {
	bool characterPhysical{true};
	bool characterMagic{true};
	bool controllerCharacterPhysical{};
	bool controllerCharacterMagic{};
	bool mercenaryPhysical{true};
	bool mercenaryMagic{true};
};

struct PluginConfig {
	LayoutConfig desktop;
	LayoutConfig controller;
	LayoutConfig mercenary;
	DisplayToggles enabled;
};

struct RectPair {
	D2RL::Widgets::Rect* label{};
	D2RL::Widgets::Rect* value{};
	std::int32_t valueXOffset{};
};

struct WidgetSet {
	D2RL::Widgets::WidgetHandle physicalLabel{};
	D2RL::Widgets::WidgetHandle physicalValue{};
	D2RL::Widgets::WidgetHandle physicalCapValue{};
	D2RL::Widgets::WidgetHandle magicLabel{};
	D2RL::Widgets::WidgetHandle magicValue{};
	D2RL::Widgets::WidgetHandle magicCapValue{};
};

using GetLocalDataContextFn = std::int32_t(__cdecl*)() noexcept;
using GetLocalPlayerFn = void*(__fastcall*)(std::int32_t context) noexcept;
using GetUnitFn = void*(__fastcall*)(std::uint32_t unitId, std::int32_t unitType) noexcept;
using GetMercenaryIdFn = std::int32_t(__fastcall*)(void* owner, std::int32_t petType, std::int32_t index) noexcept;
using ReadWideUnitStatFn = std::int32_t(__cdecl*)(void* unit, std::int32_t statId, std::int32_t layer) noexcept;
using FindTopLevelPanelFn = void*(__fastcall*)(const char* name) noexcept;
using FindChildWidgetByNameFn = void*(__fastcall*)(void* parent, const char* name) noexcept;
using GetNativeWidgetRectFn = D2RL::Widgets::Rect*(__fastcall*)(void* widget, D2RL::Widgets::Rect* output) noexcept;

struct NativeWidgetSet {
	void* physicalLabel{};
	void* physicalValue{};
	void* physicalCapValue{};
	void* magicLabel{};
	void* magicValue{};
	void* magicCapValue{};
};

const D2RL::PluginContext* Context{};
PluginMenu::Connection SettingsMenu{};
const D2RL::WidgetService* WidgetApi{};
const D2RL::ThreadService* ThreadApi{};
const D2RL::LifecycleService* LifecycleApi{};
GetLocalDataContextFn GetLocalDataContext{};
GetLocalPlayerFn GetLocalPlayer{};
GetUnitFn GetUnit{};
GetMercenaryIdFn GetMercenaryId{};
ReadWideUnitStatFn ReadWideUnitStat{};
FindTopLevelPanelFn FindTopLevelPanel{};
FindChildWidgetByNameFn FindChildWidgetByName{};
GetNativeWidgetRectFn GetNativeWidgetRect{};
PluginConfig Config{};
std::mutex ConfigMutex{};

void QueueUiRefresh(const D2RL::PluginContext* context) noexcept;

D2RL::Widgets::WidgetHandle CharacterPanel{};
D2RL::Widgets::WidgetHandle MercenaryPanel{};
WidgetSet DesktopWidgets{};
WidgetSet ControllerWidgets{};
WidgetSet MercenaryWidgets{};
HANDLE RefreshTimer{};
std::atomic<bool> WorkerRunning{};
std::atomic<bool> GameRefreshQueued{};
std::atomic<bool> UiRefreshQueued{};
std::atomic<bool> StatsAvailable{};
std::atomic<bool> NativeReadersAvailable{};
std::atomic<bool> MercenaryReadersAvailable{};
std::atomic<bool> LifecycleTrackingReady{};
std::atomic<bool> LocalPlayerReady{};
std::atomic<bool> ReadFailureWarningLogged{};
std::atomic<bool> MercenaryReadFailureWarningLogged{};
std::atomic<std::uint64_t> PlayerStateEpoch{};
std::atomic<std::int32_t> PhysicalResistance{};
std::atomic<std::int32_t> MagicResistance{};
std::atomic<std::int32_t> MercenaryPhysicalResistance{};
std::atomic<std::int32_t> MercenaryMagicResistance{};
std::atomic<bool> MercenaryStatsAvailable{};
bool LayoutWarningLogged{};
bool NativeWidgetWarningLogged{};
bool NativeTextWarningLogged{};
bool NativeRectWarningLogged{};
bool MercenaryLayoutWarningLogged{};
bool MercenaryNativeWidgetWarningLogged{};
bool MercenaryNativeTextWarningLogged{};
bool MercenaryNativeRectWarningLogged{};
bool NativeUiAvailable{};
bool CanSetText{};
bool CanSetWidgetRect{};
bool CanSetWidgetVisible{};
bool CanGetWidgetState{};

constexpr auto Trim(std::string_view value) noexcept -> std::string_view {
	while (!value.empty() && (value.front() == ' ' || value.front() == '\t' || value.front() == '\r')) value.remove_prefix(1);
	while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r')) value.remove_suffix(1);
	return value;
}

auto IsReadableRange(const void* address, std::size_t size) noexcept -> bool {
	if (address == nullptr || size == 0) return false;
	MEMORY_BASIC_INFORMATION info{};
	if (VirtualQuery(address, &info, sizeof(info)) == 0 || info.State != MEM_COMMIT
		|| (info.Protect & PAGE_GUARD) != 0 || (info.Protect & PAGE_NOACCESS) != 0) return false;
	const auto start = reinterpret_cast<std::uintptr_t>(address);
	const auto regionStart = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
	const auto regionEnd = regionStart + info.RegionSize;
	return start >= regionStart && start <= regionEnd && size <= regionEnd - start;
}

auto IsWritableRange(void* address, std::size_t size) noexcept -> bool {
	if (!IsReadableRange(address, size)) return false;
	MEMORY_BASIC_INFORMATION info{};
	if (VirtualQuery(address, &info, sizeof(info)) == 0) return false;
	const DWORD protection = info.Protect & 0xFFU;
	return protection == PAGE_READWRITE || protection == PAGE_WRITECOPY
		|| protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
}

template <typename T>
auto ReadNativeField(const void* base, std::size_t offset, T& value) noexcept -> bool {
	if (base == nullptr) return false;
	const auto address = reinterpret_cast<std::uintptr_t>(base);
	if (offset > static_cast<std::size_t>(UINTPTR_MAX - address)) return false;
	const auto* field = reinterpret_cast<const void*>(address + offset);
	if (!IsReadableRange(field, sizeof(T))) return false;
	std::memcpy(&value, field, sizeof(T));
	return true;
}

auto IsBlankTextReserve(const char* value) noexcept -> bool {
	if (value == nullptr || !IsReadableRange(value, NativeTextBufferBytes)) return false;
	return std::memcmp(value, BlankTextReserve, NativeTextBufferBytes) == 0;
}

auto BoundedTextLength(const char* value, std::size_t limit) noexcept -> std::size_t {
	if (value == nullptr) return limit;
	for (std::size_t length = 0; length < limit; ++length) {
		if (value[length] == '\0') return length;
	}
	return limit;
}

auto IsValidResistanceText(const char* value) noexcept -> bool {
	if (value == nullptr) return false;
	// The native fallback may have written this placeholder before the player was ready.
	if (value[0] == '-' && value[1] == '-' && value[2] == '\0') return true;
	const auto length = BoundedTextLength(value, NativeTextBufferBytes);
	if (length < 2 || length >= NativeTextBufferBytes || value[length - 1] != '%') return false;
	std::size_t index{};
	if (value[index] == '-') ++index;
	if (index + 1 >= length) return false;
	for (; index + 1 < length; ++index) {
		if (value[index] < '0' || value[index] > '9') return false;
	}
	return true;
}

auto IsExpectedText(const char* current, const char* next) noexcept -> bool {
	return IsBlankTextReserve(current) || IsValidResistanceText(current)
		|| (next != nullptr && std::strncmp(current, next, NativeTextBufferBytes) == 0);
}

auto ResolveNativeWidgetSet(const char* panelName, bool controller, NativeWidgetSet& widgets) noexcept -> bool {
	if (!NativeUiAvailable || FindTopLevelPanel == nullptr || FindChildWidgetByName == nullptr) return false;
	__try {
		void* const panel = FindTopLevelPanel(panelName);
		if (panel == nullptr) return false;
		const char* physicalLabel = controller ? "ControllerPhysicalResistanceTitle" : "PhysicalResistanceTitle";
		const char* physicalValue = controller ? "ControllerPhysicalText" : "PhysicalText";
		const char* physicalCapValue = controller ? "ControllerPhysicalCapText" : "PhysicalCapText";
		const char* magicLabel = controller ? "ControllerMagicResistanceTitle" : "MagicResistanceTitle";
		const char* magicValue = controller ? "ControllerMagicText" : "MagicText";
		const char* magicCapValue = controller ? "ControllerMagicCapText" : "MagicCapText";
		widgets.physicalLabel = FindChildWidgetByName(panel, physicalLabel);
		widgets.physicalValue = FindChildWidgetByName(panel, physicalValue);
		widgets.physicalCapValue = FindChildWidgetByName(panel, physicalCapValue);
		widgets.magicLabel = FindChildWidgetByName(panel, magicLabel);
		widgets.magicValue = FindChildWidgetByName(panel, magicValue);
		widgets.magicCapValue = FindChildWidgetByName(panel, magicCapValue);
		return widgets.physicalLabel != nullptr && widgets.physicalValue != nullptr
			&& widgets.physicalCapValue != nullptr && widgets.magicLabel != nullptr
			&& widgets.magicValue != nullptr && widgets.magicCapValue != nullptr;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		widgets = {};
		return false;
	}
}

auto ApplyNativeRect(void* widget, const D2RL::Widgets::Rect& rect) noexcept -> bool {
	if (!NativeUiAvailable || widget == nullptr || GetNativeWidgetRect == nullptr) return false;
	__try {
		D2RL::Widgets::Rect reported{};
		if (GetNativeWidgetRect(widget, &reported) != &reported
			|| reported.width <= 0 || reported.height <= 0) return false;
		const auto widgetAddress = reinterpret_cast<std::uintptr_t>(widget);
		if (NativeWidgetRectOffset > static_cast<std::size_t>(UINTPTR_MAX - widgetAddress)) return false;
		auto* storedAddress = reinterpret_cast<void*>(widgetAddress + NativeWidgetRectOffset);
		if (!IsWritableRange(storedAddress, sizeof(D2RL::Widgets::Rect))) return false;
		D2RL::Widgets::Rect stored{};
		std::memcpy(&stored, storedAddress, sizeof(stored));
		if (std::memcmp(&stored, &reported, sizeof(stored)) != 0) return false;
		if (std::memcmp(&stored, &rect, sizeof(stored)) == 0) return true;
		std::memcpy(storedAddress, &rect, sizeof(rect));
		D2RL::Widgets::Rect verify{};
		if (GetNativeWidgetRect(widget, &verify) == &verify
			&& std::memcmp(&verify, &rect, sizeof(rect)) == 0) return true;
		std::memcpy(storedAddress, &stored, sizeof(stored));
		return false;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

auto ApplyNativeText(void* widget, const char* text) noexcept -> bool {
	if (!NativeUiAvailable || widget == nullptr || text == nullptr) return false;
	const auto length = BoundedTextLength(text, NativeTextBufferBytes);
	if (length >= NativeTextBufferBytes) return false;
	__try {
		std::uintptr_t bufferAddress{};
		if (!ReadNativeField(widget, NativeTextPointerOffset, bufferAddress) || bufferAddress == 0) return false;
		auto* buffer = reinterpret_cast<char*>(bufferAddress);
		if (!IsWritableRange(buffer, NativeTextBufferBytes)) return false;
		char current[NativeTextBufferBytes]{};
		std::memcpy(current, buffer, sizeof(current));
		if (!IsExpectedText(current, text)) return false;
		std::array<char, NativeTextBufferBytes> next{};
		std::memcpy(next.data(), text, length);
		std::memcpy(buffer, next.data(), next.size());
		return std::memcmp(buffer, next.data(), next.size()) == 0;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

auto ResolvePosition(PluginConfig& config, std::string_view section) noexcept -> RectPair {
	if (section == "desktop.physical") return {&config.desktop.physicalLabel.rect, &config.desktop.physicalValue.rect, 362};
	if (section == "desktop.magic") return {&config.desktop.magicLabel.rect, &config.desktop.magicValue.rect, 363};
	if (section == "controller.physical") return {&config.controller.physicalLabel.rect, &config.controller.physicalValue.rect, 492};
	if (section == "controller.magic") return {&config.controller.magicLabel.rect, &config.controller.magicValue.rect, 492};
	if (section == "mercenary.physical") return {&config.mercenary.physicalLabel.rect, &config.mercenary.physicalValue.rect, 330};
	if (section == "mercenary.magic") return {&config.mercenary.magicLabel.rect, &config.mercenary.magicValue.rect, 330};
	return {};
}

auto ResolveRect(PluginConfig& config, std::string_view section) noexcept -> D2RL::Widgets::Rect* {
	if (section == "desktop.physical_label") return &config.desktop.physicalLabel.rect;
	if (section == "desktop.physical_value") return &config.desktop.physicalValue.rect;
	if (section == "desktop.magic_label") return &config.desktop.magicLabel.rect;
	if (section == "desktop.magic_value") return &config.desktop.magicValue.rect;
	if (section == "controller.physical_label") return &config.controller.physicalLabel.rect;
	if (section == "controller.physical_value") return &config.controller.physicalValue.rect;
	if (section == "controller.magic_label") return &config.controller.magicLabel.rect;
	if (section == "controller.magic_value") return &config.controller.magicValue.rect;
	if (section == "mercenary.physical_label") return &config.mercenary.physicalLabel.rect;
	if (section == "mercenary.physical_value") return &config.mercenary.physicalValue.rect;
	if (section == "mercenary.magic_label") return &config.mercenary.magicLabel.rect;
	if (section == "mercenary.magic_value") return &config.mercenary.magicValue.rect;
	return nullptr;
}

auto ResolveEnabled(DisplayToggles& enabled, std::string_view key) noexcept -> bool* {
	if (key == "character_physical") return &enabled.characterPhysical;
	if (key == "character_magic") return &enabled.characterMagic;
	if (key == "controller_character_physical") return &enabled.controllerCharacterPhysical;
	if (key == "controller_character_magic") return &enabled.controllerCharacterMagic;
	if (key == "mercenary_physical") return &enabled.mercenaryPhysical;
	if (key == "mercenary_magic") return &enabled.mercenaryMagic;
	return nullptr;
}

auto ParseConfig(const char* text, PluginConfig& config) noexcept -> bool {
	if (text == nullptr) return false;
	std::string_view input(text);
	std::string_view section;
	bool valid = true;
	while (!input.empty()) {
		const auto newline = input.find('\n');
		auto line = input.substr(0, newline);
		input = newline == std::string_view::npos ? std::string_view{} : input.substr(newline + 1);
		if (const auto comment = line.find('#'); comment != std::string_view::npos) line = line.substr(0, comment);
		line = Trim(line);
		if (line.empty()) continue;
		if (line.front() == '[' && line.back() == ']') {
			section = Trim(line.substr(1, line.size() - 2));
			continue;
		}

		const auto equals = line.find('=');
		if (equals == std::string_view::npos) {
			valid = false;
			continue;
		}
		const auto key = Trim(line.substr(0, equals));
		auto value = Trim(line.substr(equals + 1));
		if (section == "enabled") {
			auto* toggle = ResolveEnabled(config.enabled, key);
			if (toggle == nullptr) continue;
			if (value == "true") *toggle = true;
			else if (value == "false") *toggle = false;
			else valid = false;
			continue;
		}
		if (section == "controller" && key == "enabled") {
			if (value == "true") {
				config.enabled.controllerCharacterPhysical = true;
				config.enabled.controllerCharacterMagic = true;
			} else if (value == "false") {
				config.enabled.controllerCharacterPhysical = false;
				config.enabled.controllerCharacterMagic = false;
			}
			else valid = false;
			continue;
		}

		const auto position = ResolvePosition(config, section);
		if (position.label != nullptr && position.value != nullptr) {
			std::int32_t parsed{};
			const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed, 10);
			if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
				valid = false;
				continue;
			}
			if (key == "x") {
				if (parsed < -8192 || parsed > 8192 - position.valueXOffset) {
					valid = false;
					continue;
				}
				position.label->x = parsed;
				position.value->x = parsed + position.valueXOffset;
			} else if (key == "y") {
				position.label->y = parsed;
				position.value->y = parsed;
			}
			continue;
		}

		auto* rect = ResolveRect(config, section);
		if (rect == nullptr) continue;
		std::int32_t parsed{};
		const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed, 10);
		if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
			valid = false;
			continue;
		}

		if (key == "x") rect->x = parsed;
		else if (key == "y") rect->y = parsed;
		else if (key == "width") rect->width = parsed;
		else if (key == "height") rect->height = parsed;
	}

	const auto validRect = [](const D2RL::Widgets::Rect& rect) noexcept {
		return rect.x >= -8192 && rect.x <= 8192 && rect.y >= -8192 && rect.y <= 8192
			&& rect.width > 0 && rect.width <= 8192 && rect.height > 0 && rect.height <= 8192;
	};
	const auto validLayout = [&](const LayoutConfig& layout) noexcept {
		return validRect(layout.physicalLabel.rect) && validRect(layout.physicalValue.rect)
			&& validRect(layout.magicLabel.rect) && validRect(layout.magicValue.rect);
	};
	return valid && validLayout(config.desktop) && validLayout(config.controller) && validLayout(config.mercenary);
}

void SetDefaultConfig(PluginConfig& config) noexcept {
	config.enabled = {true, true, false, false, true, true};
	config.desktop = {
		{ {93, 1321, 359, 57} }, { {455, 1321, 113, 57} },
		{ {598, 1321, 359, 57} }, { {961, 1321, 113, 57} },
	};
	config.controller = {
		{ {1989, 842, 488, 79} }, { {2481, 842, 197, 79} },
		{ {1989, 950, 488, 79} }, { {2481, 950, 197, 79} },
	};
	config.mercenary = {
		{ {608, 1435, 325, 59} }, { {938, 1435, 113, 59} },
		{ {608, 1517, 325, 59} }, { {938, 1517, 113, 59} },
	};
}

void LoadConfig(const D2RL::PluginContext* context) noexcept {
	// Player defaults mirror the current active TOML; mercenary defaults use the
	// two extra HD hireling resistance rows in the active sprite.
	PluginConfig loaded{};
	SetDefaultConfig(loaded);

	std::array<char, 32 * 1024> text{};
	std::uint32_t requiredSize{};
	if (!context->ReadConfig(text.data(), static_cast<std::uint32_t>(text.size()), &requiredSize)) {
		std::scoped_lock lock(ConfigMutex);
		Config = loaded;
		context->LogWarn("Character Resistance Display: couldn't read its TOML config; using the built-in layout positions.");
		return;
	}
	text.back() = '\0';
	if (!ParseConfig(text.data(), loaded)) {
		SetDefaultConfig(loaded);
		std::scoped_lock lock(ConfigMutex);
		Config = loaded;
		context->LogWarn("Character Resistance Display: invalid TOML settings; using the built-in positions and display switches.");
		return;
	}
	{
		std::scoped_lock lock(ConfigMutex);
		Config = loaded;
	}
	context->LogInfo("Character Resistance Display: loaded widget positions from TOML.");
}

struct ConfigValueEdit {
	std::string_view section;
	std::string_view key;
	std::string_view value;
};

auto RewriteConfigValues(std::string_view document, const ConfigValueEdit* edits,
	std::size_t editCount, std::string& updated) noexcept -> bool {
	if (edits == nullptr || editCount == 0 || editCount > 2) return false;
	try {
		std::array<unsigned, 2> matches{};
		std::string_view section;
		updated.clear();
		updated.reserve(document.size() + 64);
		for (std::size_t lineStart = 0; lineStart < document.size();) {
			const auto newline = document.find('\n', lineStart);
			const auto nextLine = newline == std::string_view::npos ? document.size() : newline + 1;
			auto lineEnd = newline == std::string_view::npos ? document.size() : newline;
			if (lineEnd > lineStart && document[lineEnd - 1] == '\r') --lineEnd;
			const auto line = document.substr(lineStart, lineEnd - lineStart);
			const auto ending = document.substr(lineEnd, nextLine - lineEnd);
			const auto comment = line.find('#');
			const auto code = line.substr(0, comment);
			const auto trimmed = Trim(code);
			if (trimmed.size() >= 2 && trimmed.front() == '[' && trimmed.back() == ']') {
				section = Trim(trimmed.substr(1, trimmed.size() - 2));
				updated.append(line);
			} else {
				const auto equals = code.find('=');
				std::size_t matchedEdit = editCount;
				if (equals != std::string_view::npos) {
					const auto key = Trim(code.substr(0, equals));
					for (std::size_t i = 0; i < editCount; ++i) {
						if (section == edits[i].section && key == edits[i].key) {
							if (++matches[i] != 1) return false;
							matchedEdit = i;
							break;
						}
					}
				}
				if (matchedEdit == editCount) {
					updated.append(line);
				} else {
					std::size_t valueStart = equals + 1;
					while (valueStart < code.size() && (code[valueStart] == ' ' || code[valueStart] == '\t')) ++valueStart;
					std::size_t valueEnd = code.size();
					while (valueEnd > valueStart && (code[valueEnd - 1] == ' ' || code[valueEnd - 1] == '\t' || code[valueEnd - 1] == '\r')) --valueEnd;
					if (valueStart == valueEnd) return false;
					updated.append(line.substr(0, valueStart));
					updated.append(edits[matchedEdit].value);
					updated.append(line.substr(valueEnd));
				}
			}
			updated.append(ending);
			lineStart = nextLine;
		}
		for (std::size_t i = 0; i < editCount; ++i) {
			if (matches[i] != 1) return false;
		}
		return true;
	} catch (...) {
		return false;
	}
}

auto ParseMenuPosition(const char* tomlValue, std::int32_t& x, std::int32_t& y) noexcept -> bool {
	if (tomlValue == nullptr) return false;
	std::string_view value = Trim(tomlValue);
	if (value.size() < 5 || value.front() != '[' || value.back() != ']') return false;
	value = Trim(value.substr(1, value.size() - 2));
	const auto comma = value.find(',');
	if (comma == std::string_view::npos || value.find(',', comma + 1) != std::string_view::npos) return false;
	const auto first = Trim(value.substr(0, comma));
	const auto second = Trim(value.substr(comma + 1));
	if (first.empty() || second.empty()) return false;
	const auto xResult = std::from_chars(first.data(), first.data() + first.size(), x, 10);
	const auto yResult = std::from_chars(second.data(), second.data() + second.size(), y, 10);
	return xResult.ec == std::errc{} && xResult.ptr == first.data() + first.size()
		&& yResult.ec == std::errc{} && yResult.ptr == second.data() + second.size();
}

auto ResolveMenuToggle(PluginConfig& config, std::string_view key,
	std::string_view& configKey) noexcept -> bool* {
	constexpr std::string_view prefix = "enabled.";
	if (!key.starts_with(prefix)) return nullptr;
	configKey = key.substr(prefix.size());
	return ResolveEnabled(config.enabled, configKey);
}

auto WriteMenuError(char* error, std::uint32_t errorSize, const char* message) noexcept -> bool {
	if (error != nullptr && errorSize != 0) (void)std::snprintf(error, errorSize, "%s", message);
	return false;
}

bool __cdecl GetMenuSetting(const char* key, char* output, std::uint32_t outputSize, void*) noexcept {
	if (key == nullptr || output == nullptr || outputSize == 0) return false;
	try {
		std::scoped_lock lock(ConfigMutex);
		const std::string_view settingKey(key);
		std::string_view configKey;
		if (auto* toggle = ResolveMenuToggle(Config, settingKey, configKey); toggle != nullptr) {
			const int written = std::snprintf(output, outputSize, "%s", *toggle ? "true" : "false");
			return written >= 0 && static_cast<std::uint32_t>(written) < outputSize;
		}
		const auto position = ResolvePosition(Config, settingKey);
		if (position.label == nullptr || position.value == nullptr) return false;
		const int written = std::snprintf(output, outputSize, "[%d, %d]", position.label->x, position.label->y);
		return written >= 0 && static_cast<std::uint32_t>(written) < outputSize;
	} catch (...) {
		return false;
	}
}

bool __cdecl SetMenuSetting(const char* key, const char* tomlValue, char* error,
	std::uint32_t errorSize, void*) noexcept {
	if (key == nullptr || tomlValue == nullptr) return WriteMenuError(error, errorSize, "Missing setting or value.");
	const std::string_view settingKey(key);
	const bool isTrue = std::strcmp(tomlValue, "true") == 0;
	const bool isFalse = std::strcmp(tomlValue, "false") == 0;
	std::int32_t x{};
	std::int32_t y{};
	std::string_view configKey;
	const bool toggleKey = settingKey.starts_with("enabled.");
	if (toggleKey) {
		if (!isTrue && !isFalse) return WriteMenuError(error, errorSize, "Enter true or false.");
	} else if (!ParseMenuPosition(tomlValue, x, y)) {
		return WriteMenuError(error, errorSize, "Enter a two-number position such as [93, 1321].");
	}

	const auto* context = Context;
	if (context == nullptr) return WriteMenuError(error, errorSize, "The plugin is unloading.");
	try {
		std::scoped_lock lock(ConfigMutex);
		std::array<char, 32 * 1024> text{};
		std::uint32_t requiredSize{};
		if (!context->ReadConfig(text.data(), static_cast<std::uint32_t>(text.size()), &requiredSize)
			|| requiredSize >= text.size()) {
			return WriteMenuError(error, errorSize, "Cannot read the complete plugin TOML.");
		}
		text.back() = '\0';
		const std::string document(text.data(), BoundedTextLength(text.data(), text.size()));
		std::array<std::string, 2> encodedValues{};
		std::array<ConfigValueEdit, 2> edits{};
		std::size_t editCount{};
		if (toggleKey) {
			auto* toggle = ResolveMenuToggle(Config, settingKey, configKey);
			if (toggle == nullptr) return WriteMenuError(error, errorSize, "Unknown setting.");
			(void)toggle;
			edits[0] = {"enabled", configKey, isTrue ? "true" : "false"};
			editCount = 1;
		} else {
			const auto position = ResolvePosition(Config, settingKey);
			if (position.label == nullptr || position.value == nullptr) return WriteMenuError(error, errorSize, "Unknown setting.");
			if (x < -8192 || x > 8192 - position.valueXOffset || y < -8192 || y > 8192) {
				return WriteMenuError(error, errorSize, "The position must stay within the supported layout range.");
			}
			encodedValues[0] = std::to_string(x);
			encodedValues[1] = std::to_string(y);
			edits[0] = {settingKey, "x", encodedValues[0]};
			edits[1] = {settingKey, "y", encodedValues[1]};
			editCount = 2;
		}
		std::string updated;
		if (!RewriteConfigValues(document, edits.data(), editCount, updated)) {
			return WriteMenuError(error, errorSize, "The setting is missing or duplicated in the plugin TOML.");
		}
		PluginConfig next{};
		SetDefaultConfig(next);
		if (!ParseConfig(updated.c_str(), next)) {
			return WriteMenuError(error, errorSize, "The updated TOML would contain invalid plugin settings.");
		}
		if (!context->WriteConfig(updated.c_str())) {
			return WriteMenuError(error, errorSize, "The plugin TOML could not be saved; runtime values were kept.");
		}
		Config = next;
	} catch (...) {
		return WriteMenuError(error, errorSize, "The setting could not be applied.");
	}
	SettingsMenu.ValueChanged(key);
	QueueUiRefresh(context);
	return true;
}

enum class PlayerReadFailure : std::uint8_t {
	None,
	ReadersUnavailable,
	InvalidLocalContext,
	LocalPlayerUnavailable,
	ReadFault,
};

auto ReadPlayerResistances(std::int32_t& physical, std::int32_t& magic, std::int32_t& localContext,
	PlayerReadFailure& failure) noexcept -> bool {
	failure = PlayerReadFailure::None;
	__try {
		if (GetLocalDataContext == nullptr || GetLocalPlayer == nullptr || ReadWideUnitStat == nullptr) {
			failure = PlayerReadFailure::ReadersUnavailable;
			return false;
		}
		localContext = GetLocalDataContext();
		if (localContext < 0 || localContext >= 8) {
			failure = PlayerReadFailure::InvalidLocalContext;
			return false;
		}
		void* const player = GetLocalPlayer(localContext);
		if (player == nullptr) {
			failure = PlayerReadFailure::LocalPlayerUnavailable;
			return false;
		}
		physical = ReadWideUnitStat(player, PhysicalResistanceStatId, BaseStatLayer);
		magic = ReadWideUnitStat(player, MagicResistanceStatId, BaseStatLayer);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		failure = PlayerReadFailure::ReadFault;
		return false;
	}
}

enum class MercenaryReadFailure : std::uint8_t {
	None,
	ReadersUnavailable,
	InvalidLocalContext,
	LocalPlayerUnavailable,
	MercenaryUnitUnavailable,
	ReadFault,
};

auto ReadMercenaryResistances(std::int32_t& physical, std::int32_t& magic, bool& hasMercenary,
	std::int32_t& localContext, MercenaryReadFailure& failure) noexcept -> bool {
	failure = MercenaryReadFailure::None;
	hasMercenary = false;
	__try {
		if (GetLocalDataContext == nullptr || GetLocalPlayer == nullptr || GetMercenaryId == nullptr
			|| GetUnit == nullptr || ReadWideUnitStat == nullptr) {
			failure = MercenaryReadFailure::ReadersUnavailable;
			return false;
		}
		localContext = GetLocalDataContext();
		if (localContext < 0 || localContext >= 8) {
			failure = MercenaryReadFailure::InvalidLocalContext;
			return false;
		}
		void* const player = GetLocalPlayer(localContext);
		if (player == nullptr) {
			failure = MercenaryReadFailure::LocalPlayerUnavailable;
			return false;
		}
		const std::int32_t mercenaryId = GetMercenaryId(player, 7, 0);
		if (mercenaryId < 0) return true;
		void* const mercenary = GetUnit(static_cast<std::uint32_t>(mercenaryId), 1);
		if (mercenary == nullptr) {
			failure = MercenaryReadFailure::MercenaryUnitUnavailable;
			return false;
		}
		physical = ReadWideUnitStat(mercenary, PhysicalResistanceStatId, BaseStatLayer);
		magic = ReadWideUnitStat(mercenary, MagicResistanceStatId, BaseStatLayer);
		hasMercenary = true;
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		failure = MercenaryReadFailure::ReadFault;
		return false;
	}
}

void ResetCharacterPanelCache() noexcept {
	CharacterPanel = D2RL::Widgets::InvalidHandle;
	DesktopWidgets = {};
	ControllerWidgets = {};
}

void ResetMercenaryPanelCache() noexcept {
	MercenaryPanel = D2RL::Widgets::InvalidHandle;
	MercenaryWidgets = {};
}

void __cdecl OnGameplayEvent(const D2RL::PluginContext* context, const D2RL::Lifecycle::GameplayEvent* event, void*) noexcept {
	if (!D2RL::Lifecycle::HasGameplayEventField(event, D2RL::Lifecycle::GameplayEventRequiredSize)) return;
	if (event->kind != D2RL::Lifecycle::GameplayEventKind::GameLeft
		&& event->kind != D2RL::Lifecycle::GameplayEventKind::GameJoined
		&& event->kind != D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady) return;

	PlayerStateEpoch.fetch_add(1, std::memory_order_acq_rel);
	LocalPlayerReady.store(event->kind == D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady, std::memory_order_release);
	// A queued game-thread callback can be discarded while returning to character select.
	// Clear the marker so the timer can request a fresh read in the next game session.
	GameRefreshQueued.store(false, std::memory_order_release);
	StatsAvailable.store(false, std::memory_order_release);
	PhysicalResistance.store(0, std::memory_order_relaxed);
	MagicResistance.store(0, std::memory_order_relaxed);
	MercenaryStatsAvailable.store(false, std::memory_order_release);
	MercenaryPhysicalResistance.store(0, std::memory_order_relaxed);
	MercenaryMagicResistance.store(0, std::memory_order_relaxed);
	ReadFailureWarningLogged.store(false, std::memory_order_release);
	MercenaryReadFailureWarningLogged.store(false, std::memory_order_release);
	NativeWidgetWarningLogged = false;
	NativeTextWarningLogged = false;
	NativeRectWarningLogged = false;
	MercenaryLayoutWarningLogged = false;
	MercenaryNativeWidgetWarningLogged = false;
	MercenaryNativeTextWarningLogged = false;
	MercenaryNativeRectWarningLogged = false;
	ResetCharacterPanelCache();
	ResetMercenaryPanelCache();
	QueueUiRefresh(context);
}

auto RegisterGameplayEventListeners(const D2RL::PluginContext* context) noexcept -> bool {
	if (context->QueryService(&LifecycleApi) != D2RL::ServiceQueryResult::Success
		|| !D2RL::HasLifecycleServiceField(
			LifecycleApi,
			static_cast<std::uint32_t>(offsetof(D2RL::LifecycleService, registerGameplayEventListener)
				+ sizeof(D2RL::Lifecycle::RegisterGameplayEventListenerFn)))
		|| LifecycleApi->registerGameplayEventListener == nullptr) {
		LifecycleApi = nullptr;
		context->LogWarn("Character Resistance Display: lifecycle events are unavailable; panel handles will be refreshed from widget visibility.");
		return false;
	}

	constexpr std::array eventKinds{
		D2RL::Lifecycle::GameplayEventKind::GameLeft,
		D2RL::Lifecycle::GameplayEventKind::GameJoined,
		D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady,
	};
	bool allListenersRegistered = true;
	for (const auto kind : eventKinds) {
		const D2RL::Lifecycle::GameplayEventListener listener{
			.structSize = D2RL::Lifecycle::GameplayEventListenerSize,
			.kind = kind,
			.callback = OnGameplayEvent,
		};
		D2RL::Lifecycle::ListenerHandle handle{};
		if (LifecycleApi->registerGameplayEventListener(context, &listener, &handle) != D2RL::Lifecycle::Result::Success) {
			allListenersRegistered = false;
			context->LogWarn("Character Resistance Display: couldn't register a gameplay lifecycle listener; polling recovery remains enabled.");
		}
	}
	return allListenersRegistered;
}

void __cdecl RefreshGameStats(const D2RL::PluginContext* context, void*) noexcept {
	const std::uint64_t playerStateEpoch = PlayerStateEpoch.load(std::memory_order_acquire);
	if (LifecycleTrackingReady.load(std::memory_order_acquire)
		&& !LocalPlayerReady.load(std::memory_order_acquire)) {
		StatsAvailable.store(false, std::memory_order_release);
		MercenaryStatsAvailable.store(false, std::memory_order_release);
		GameRefreshQueued.store(false, std::memory_order_release);
		QueueUiRefresh(context);
		return;
	}
	std::int32_t physical{};
	std::int32_t magic{};
	std::int32_t localContext = -1;
	PlayerReadFailure failure{};
	const bool readSucceeded = NativeReadersAvailable.load(std::memory_order_acquire)
		&& ReadPlayerResistances(physical, magic, localContext, failure);
	std::int32_t mercenaryPhysical{};
	std::int32_t mercenaryMagic{};
	std::int32_t mercenaryContext = -1;
	bool hasMercenary{};
	MercenaryReadFailure mercenaryFailure{};
	const bool mercenaryReadSucceeded = MercenaryReadersAvailable.load(std::memory_order_acquire)
		&& ReadMercenaryResistances(mercenaryPhysical, mercenaryMagic, hasMercenary, mercenaryContext, mercenaryFailure);
	const bool sessionStillCurrent = playerStateEpoch == PlayerStateEpoch.load(std::memory_order_acquire)
		&& (!LifecycleTrackingReady.load(std::memory_order_acquire) || LocalPlayerReady.load(std::memory_order_acquire));
	if (readSucceeded && sessionStillCurrent) {
		PhysicalResistance.store(physical, std::memory_order_relaxed);
		MagicResistance.store(magic, std::memory_order_relaxed);
		StatsAvailable.store(true, std::memory_order_release);
		ReadFailureWarningLogged.store(false, std::memory_order_release);
	} else {
		StatsAvailable.store(false, std::memory_order_release);
		if (!readSucceeded && failure != PlayerReadFailure::None
			&& !ReadFailureWarningLogged.exchange(true, std::memory_order_acq_rel)) {
			char message[192]{};
			(void)std::snprintf(message, sizeof(message),
				"Character Resistance Display: stat read failed after player readiness (reason %u, local context %d).",
				static_cast<unsigned>(failure), localContext);
			context->LogWarn(message);
		}
	}
	if (mercenaryReadSucceeded && sessionStillCurrent) {
		MercenaryPhysicalResistance.store(mercenaryPhysical, std::memory_order_relaxed);
		MercenaryMagicResistance.store(mercenaryMagic, std::memory_order_relaxed);
		MercenaryStatsAvailable.store(hasMercenary, std::memory_order_release);
		MercenaryReadFailureWarningLogged.store(false, std::memory_order_release);
	} else {
		MercenaryStatsAvailable.store(false, std::memory_order_release);
		if (!mercenaryReadSucceeded && mercenaryFailure != MercenaryReadFailure::None
			&& !MercenaryReadFailureWarningLogged.exchange(true, std::memory_order_acq_rel)) {
			char message[192]{};
			(void)std::snprintf(message, sizeof(message),
				"Character Resistance Display: mercenary stat read failed (reason %u, local context %d).",
				static_cast<unsigned>(mercenaryFailure), mercenaryContext);
			context->LogWarn(message);
		}
	}
	GameRefreshQueued.store(false, std::memory_order_release);
	QueueUiRefresh(context);
}

auto FindWidget(D2RL::Widgets::WidgetHandle parent, const char* name, D2RL::Widgets::WidgetHandle& handle) noexcept -> bool {
	if (handle != D2RL::Widgets::InvalidHandle) return true;
	return WidgetApi->findWidget(Context, parent, name, &handle) == D2RL::Widgets::Result::Success;
}

auto FindDesktopWidgets() noexcept -> bool {
	if (CharacterPanel == D2RL::Widgets::InvalidHandle) return false;
	return FindWidget(CharacterPanel, "PhysicalResistanceTitle", DesktopWidgets.physicalLabel)
		&& FindWidget(CharacterPanel, "PhysicalText", DesktopWidgets.physicalValue)
		&& FindWidget(CharacterPanel, "PhysicalCapText", DesktopWidgets.physicalCapValue)
		&& FindWidget(CharacterPanel, "MagicResistanceTitle", DesktopWidgets.magicLabel)
		&& FindWidget(CharacterPanel, "MagicText", DesktopWidgets.magicValue)
		&& FindWidget(CharacterPanel, "MagicCapText", DesktopWidgets.magicCapValue);
}

auto FindControllerWidgets() noexcept -> bool {
	if (CharacterPanel == D2RL::Widgets::InvalidHandle) return false;
	return FindWidget(CharacterPanel, "ControllerPhysicalResistanceTitle", ControllerWidgets.physicalLabel)
		&& FindWidget(CharacterPanel, "ControllerPhysicalText", ControllerWidgets.physicalValue)
		&& FindWidget(CharacterPanel, "ControllerPhysicalCapText", ControllerWidgets.physicalCapValue)
		&& FindWidget(CharacterPanel, "ControllerMagicResistanceTitle", ControllerWidgets.magicLabel)
		&& FindWidget(CharacterPanel, "ControllerMagicText", ControllerWidgets.magicValue)
		&& FindWidget(CharacterPanel, "ControllerMagicCapText", ControllerWidgets.magicCapValue);
}

auto FindMercenaryWidgets() noexcept -> bool {
	if (MercenaryPanel == D2RL::Widgets::InvalidHandle) return false;
	return FindWidget(MercenaryPanel, "PhysicalResistanceTitle", MercenaryWidgets.physicalLabel)
		&& FindWidget(MercenaryPanel, "PhysicalText", MercenaryWidgets.physicalValue)
		&& FindWidget(MercenaryPanel, "PhysicalCapText", MercenaryWidgets.physicalCapValue)
		&& FindWidget(MercenaryPanel, "MagicResistanceTitle", MercenaryWidgets.magicLabel)
		&& FindWidget(MercenaryPanel, "MagicText", MercenaryWidgets.magicValue)
		&& FindWidget(MercenaryPanel, "MagicCapText", MercenaryWidgets.magicCapValue);
}

auto ApplyRect(D2RL::Widgets::WidgetHandle handle, void* nativeWidget, const D2RL::Widgets::Rect& rect) noexcept -> bool {
	if (CanSetWidgetRect
		&& WidgetApi->setWidgetRect(Context, handle, &rect) == D2RL::Widgets::Result::Success) return true;
	return ApplyNativeRect(nativeWidget, rect);
}

auto ApplyText(D2RL::Widgets::WidgetHandle handle, void* nativeWidget, const char* text) noexcept -> bool {
	if (CanSetText && WidgetApi->setText(Context, handle, text) == D2RL::Widgets::Result::Success) return true;
	return ApplyNativeText(nativeWidget, text);
}

void SetResistanceWidgetsVisible(const WidgetSet& widgets, bool physicalEnabled, bool magicEnabled,
	bool physicalAtCap, bool magicAtCap) noexcept {
	if (!CanSetWidgetVisible) return;
	(void)WidgetApi->setWidgetVisible(Context, widgets.physicalLabel, physicalEnabled);
	(void)WidgetApi->setWidgetVisible(Context, widgets.physicalValue, physicalEnabled && !physicalAtCap);
	(void)WidgetApi->setWidgetVisible(Context, widgets.physicalCapValue, physicalEnabled && physicalAtCap);
	(void)WidgetApi->setWidgetVisible(Context, widgets.magicLabel, magicEnabled);
	(void)WidgetApi->setWidgetVisible(Context, widgets.magicValue, magicEnabled && !magicAtCap);
	(void)WidgetApi->setWidgetVisible(Context, widgets.magicCapValue, magicEnabled && magicAtCap);
}

void __cdecl RefreshCharacterPanel(const D2RL::PluginContext* context) noexcept {
	if (context == nullptr || WidgetApi == nullptr) return;
	if (CharacterPanel == D2RL::Widgets::InvalidHandle
		&& WidgetApi->findPanel(Context, "CharacterStatsPanel", &CharacterPanel) != D2RL::Widgets::Result::Success) return;

	if (CanGetWidgetState) {
		D2RL::Widgets::State panelState{};
		const auto result = WidgetApi->getWidgetState(Context, CharacterPanel, &panelState);
		if (result != D2RL::Widgets::Result::Success || !panelState.visible) {
			ResetCharacterPanelCache();
			return;
		}
	}

	WidgetSet* widgets{};
	const LayoutConfig* layout{};
	bool controller = false;
	if (FindControllerWidgets()) {
		widgets = &ControllerWidgets;
		layout = &Config.controller;
		controller = true;
	} else if (FindDesktopWidgets()) {
		widgets = &DesktopWidgets;
		layout = &Config.desktop;
	} else {
		if (!LayoutWarningLogged) {
			LayoutWarningLogged = true;
			context->LogWarn("Character Resistance Display: added text widgets weren't found. Load the HD character-sheet layout changes for this mod.");
		}
		ResetCharacterPanelCache();
		return;
	}

	NativeWidgetSet nativeWidgets{};
	if (NativeUiAvailable && !ResolveNativeWidgetSet("CharacterStatsPanel", controller, nativeWidgets) && !NativeWidgetWarningLogged) {
		NativeWidgetWarningLogged = true;
		context->LogWarn("Character Resistance Display: native character widget lookup was incomplete; check the HD character-sheet widget names.");
	}

	const bool available = StatsAvailable.load(std::memory_order_acquire);
	const std::int32_t rawPhysical = PhysicalResistance.load(std::memory_order_relaxed);
	const std::int32_t rawMagic = MagicResistance.load(std::memory_order_relaxed);
	const bool physicalAtCap = available && rawPhysical >= PhysicalResistanceCap;
	const bool magicAtCap = available && rawMagic >= MagicResistanceCap;
	char physicalText[32]{};
	char magicText[32]{};
	if (available) {
		(void)std::snprintf(physicalText, sizeof(physicalText), "%d%%", std::min(rawPhysical, PhysicalResistanceCap));
		(void)std::snprintf(magicText, sizeof(magicText), "%d%%", std::min(rawMagic, MagicResistanceCap));
	} else {
		(void)std::snprintf(physicalText, sizeof(physicalText), "--");
		(void)std::snprintf(magicText, sizeof(magicText), "--");
	}
	bool textOk = true;
	if (controller) {
		textOk = ApplyText(widgets->physicalLabel, nativeWidgets.physicalLabel, "Physical Resistance") && textOk;
		textOk = ApplyText(widgets->magicLabel, nativeWidgets.magicLabel, "Magic Resistance") && textOk;
	}
	textOk = ApplyText(widgets->physicalValue, nativeWidgets.physicalValue, physicalText) && textOk;
	textOk = ApplyText(widgets->physicalCapValue, nativeWidgets.physicalCapValue, physicalText) && textOk;
	textOk = ApplyText(widgets->magicValue, nativeWidgets.magicValue, magicText) && textOk;
	textOk = ApplyText(widgets->magicCapValue, nativeWidgets.magicCapValue, magicText) && textOk;
	bool rectsOk = ApplyRect(widgets->physicalLabel, nativeWidgets.physicalLabel, layout->physicalLabel.rect);
	rectsOk = ApplyRect(widgets->physicalValue, nativeWidgets.physicalValue, layout->physicalValue.rect) && rectsOk;
	rectsOk = ApplyRect(widgets->physicalCapValue, nativeWidgets.physicalCapValue, layout->physicalValue.rect) && rectsOk;
	rectsOk = ApplyRect(widgets->magicLabel, nativeWidgets.magicLabel, layout->magicLabel.rect) && rectsOk;
	rectsOk = ApplyRect(widgets->magicValue, nativeWidgets.magicValue, layout->magicValue.rect) && rectsOk;
	rectsOk = ApplyRect(widgets->magicCapValue, nativeWidgets.magicCapValue, layout->magicValue.rect) && rectsOk;
	if (!textOk && !NativeTextWarningLogged) {
		NativeTextWarningLogged = true;
		context->LogWarn("Character Resistance Display: text updates failed; check the reserved blank text in the HD character-sheet layout and its widget names.");
	}
	if (!rectsOk && !NativeRectWarningLogged) {
		NativeRectWarningLogged = true;
		context->LogWarn("Character Resistance Display: one or more widget positions could not be applied; check the TOML coordinates and native layout compatibility.");
	}
	if (controller) {
		const bool showControllerWidgets = textOk && rectsOk;
		SetResistanceWidgetsVisible(*widgets,
			Config.enabled.controllerCharacterPhysical && showControllerWidgets,
			Config.enabled.controllerCharacterMagic && showControllerWidgets,
			physicalAtCap, magicAtCap);
	} else {
		const bool physicalEnabled = layout == &Config.desktop
			? Config.enabled.characterPhysical : Config.enabled.mercenaryPhysical;
		const bool magicEnabled = layout == &Config.desktop
			? Config.enabled.characterMagic : Config.enabled.mercenaryMagic;
		SetResistanceWidgetsVisible(*widgets, physicalEnabled, magicEnabled,
			textOk && rectsOk && physicalAtCap, textOk && rectsOk && magicAtCap);
	}
}

void __cdecl RefreshMercenaryPanel(const D2RL::PluginContext* context) noexcept {
	if (context == nullptr || WidgetApi == nullptr) return;
	if (MercenaryPanel == D2RL::Widgets::InvalidHandle
		&& WidgetApi->findPanel(Context, "HirelingInventoryPanel", &MercenaryPanel) != D2RL::Widgets::Result::Success) return;

	if (CanGetWidgetState) {
		D2RL::Widgets::State panelState{};
		const auto result = WidgetApi->getWidgetState(Context, MercenaryPanel, &panelState);
		if (result != D2RL::Widgets::Result::Success || !panelState.visible) {
			ResetMercenaryPanelCache();
			return;
		}
	}

	if (!FindMercenaryWidgets()) {
		if (!MercenaryLayoutWarningLogged) {
			MercenaryLayoutWarningLogged = true;
			context->LogWarn("Character Resistance Display: mercenary resistance widgets weren't found. Add the Physical/Magic resistance text boxes to the HD hireling panel layout.");
		}
		ResetMercenaryPanelCache();
		return;
	}

	NativeWidgetSet nativeWidgets{};
	if (NativeUiAvailable && !ResolveNativeWidgetSet("HirelingInventoryPanel", false, nativeWidgets)
		&& !MercenaryNativeWidgetWarningLogged) {
		MercenaryNativeWidgetWarningLogged = true;
		context->LogWarn("Character Resistance Display: native mercenary widget lookup was incomplete; check the HD hireling panel widget names.");
	}

	const bool available = MercenaryStatsAvailable.load(std::memory_order_acquire);
	const std::int32_t rawPhysical = MercenaryPhysicalResistance.load(std::memory_order_relaxed);
	const std::int32_t rawMagic = MercenaryMagicResistance.load(std::memory_order_relaxed);
	const bool physicalAtCap = available && rawPhysical >= PhysicalResistanceCap;
	const bool magicAtCap = available && rawMagic >= MagicResistanceCap;
	char physicalText[32]{};
	char magicText[32]{};
	if (available) {
		(void)std::snprintf(physicalText, sizeof(physicalText), "%d%%", std::min(rawPhysical, PhysicalResistanceCap));
		(void)std::snprintf(magicText, sizeof(magicText), "%d%%", std::min(rawMagic, MagicResistanceCap));
	} else {
		(void)std::snprintf(physicalText, sizeof(physicalText), "--");
		(void)std::snprintf(magicText, sizeof(magicText), "--");
	}
	bool textOk = true;
	textOk = ApplyText(MercenaryWidgets.physicalValue, nativeWidgets.physicalValue, physicalText) && textOk;
	textOk = ApplyText(MercenaryWidgets.physicalCapValue, nativeWidgets.physicalCapValue, physicalText) && textOk;
	textOk = ApplyText(MercenaryWidgets.magicValue, nativeWidgets.magicValue, magicText) && textOk;
	textOk = ApplyText(MercenaryWidgets.magicCapValue, nativeWidgets.magicCapValue, magicText) && textOk;
	const auto& layout = Config.mercenary;
	bool rectsOk = ApplyRect(MercenaryWidgets.physicalLabel, nativeWidgets.physicalLabel, layout.physicalLabel.rect);
	rectsOk = ApplyRect(MercenaryWidgets.physicalValue, nativeWidgets.physicalValue, layout.physicalValue.rect) && rectsOk;
	rectsOk = ApplyRect(MercenaryWidgets.physicalCapValue, nativeWidgets.physicalCapValue, layout.physicalValue.rect) && rectsOk;
	rectsOk = ApplyRect(MercenaryWidgets.magicLabel, nativeWidgets.magicLabel, layout.magicLabel.rect) && rectsOk;
	rectsOk = ApplyRect(MercenaryWidgets.magicValue, nativeWidgets.magicValue, layout.magicValue.rect) && rectsOk;
	rectsOk = ApplyRect(MercenaryWidgets.magicCapValue, nativeWidgets.magicCapValue, layout.magicValue.rect) && rectsOk;
	if (!textOk && !MercenaryNativeTextWarningLogged) {
		MercenaryNativeTextWarningLogged = true;
		context->LogWarn("Character Resistance Display: mercenary value text updates failed; check the blank text buffers and hireling layout widget names.");
	}
	if (!rectsOk && !MercenaryNativeRectWarningLogged) {
		MercenaryNativeRectWarningLogged = true;
		context->LogWarn("Character Resistance Display: mercenary text positions could not be applied; check the TOML coordinates and hireling layout compatibility.");
	}
	SetResistanceWidgetsVisible(MercenaryWidgets,
		Config.enabled.mercenaryPhysical, Config.enabled.mercenaryMagic,
		textOk && rectsOk && physicalAtCap, textOk && rectsOk && magicAtCap);
}

void __cdecl RefreshCharacterSheet(const D2RL::PluginContext* context, void*) noexcept {
	UiRefreshQueued.store(false, std::memory_order_release);
	if (context == nullptr || WidgetApi == nullptr) return;
	std::scoped_lock lock(ConfigMutex);
	RefreshCharacterPanel(context);
	RefreshMercenaryPanel(context);
}

void QueueUiRefresh(const D2RL::PluginContext* context) noexcept {
	if (ThreadApi == nullptr || context == nullptr) return;
	bool expected = false;
	if (!UiRefreshQueued.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return;
	if (ThreadApi->runOnUiThread(context, RefreshCharacterSheet, nullptr) != D2RL::Threads::Result::Success) {
		UiRefreshQueued.store(false, std::memory_order_release);
	}
}

void CALLBACK RefreshWorkerTick(PVOID, BOOLEAN) noexcept {
	if (!WorkerRunning.load(std::memory_order_acquire)) return;
	if (LifecycleTrackingReady.load(std::memory_order_acquire)
		&& !LocalPlayerReady.load(std::memory_order_acquire)) return;
	bool expected = false;
	if (!GameRefreshQueued.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return;
	const auto result = ThreadApi->runOnGameThread(Context, RefreshGameStats, nullptr);
	if (result != D2RL::Threads::Result::Success) {
		GameRefreshQueued.store(false, std::memory_order_release);
		StatsAvailable.store(false, std::memory_order_release);
		MercenaryStatsAvailable.store(false, std::memory_order_release);
		QueueUiRefresh(Context);
	}
}

auto ValidateNativeUi(const D2RL::PluginContext* context) noexcept -> bool {
	if (context->exeBase == 0) {
		context->LogWarn("Character Resistance Display: executable base unavailable; native widget compatibility path is disabled.");
		return false;
	}
	const bool panelMatches = context->CheckExpectedBytes(
		FindTopLevelPanelRva, FindTopLevelPanelExpected.data(), static_cast<std::uint32_t>(FindTopLevelPanelExpected.size()));
	const bool childMatches = context->CheckExpectedBytes(
		FindChildWidgetByNameRva, FindChildWidgetByNameExpected.data(), static_cast<std::uint32_t>(FindChildWidgetByNameExpected.size()));
	const bool rectMatches = context->CheckExpectedBytes(
		GetWidgetRectRva, GetWidgetRectExpected.data(), static_cast<std::uint32_t>(GetWidgetRectExpected.size()));
	if (!panelMatches || !childMatches || !rectMatches) {
		if (!panelMatches) context->LogWarn("Character Resistance Display: top-level panel resolver signature mismatch; native widget path is disabled.");
		if (!childMatches) context->LogWarn("Character Resistance Display: child widget resolver signature mismatch; native widget path is disabled.");
		if (!rectMatches) context->LogWarn("Character Resistance Display: widget rectangle signature mismatch; native widget path is disabled.");
		return false;
	}
	FindTopLevelPanel = reinterpret_cast<FindTopLevelPanelFn>(context->exeBase + FindTopLevelPanelRva);
	FindChildWidgetByName = reinterpret_cast<FindChildWidgetByNameFn>(context->exeBase + FindChildWidgetByNameRva);
	GetNativeWidgetRect = reinterpret_cast<GetNativeWidgetRectFn>(context->exeBase + GetWidgetRectRva);
	return true;
}

auto ValidateNativeReaders(const D2RL::PluginContext* context) noexcept -> bool {
	if (context->exeBase == 0) {
		context->LogWarn("Character Resistance Display: the loader did not provide an executable base; stat reads are disabled.");
		return false;
	}
	const bool localDataContextMatches = context->CheckExpectedBytes(
		GetLocalDataContextRva, GetLocalDataContextExpected.data(), static_cast<std::uint32_t>(GetLocalDataContextExpected.size()));
	const bool localPlayerMatches = context->CheckExpectedBytes(
		GetLocalPlayerRva, GetLocalPlayerExpected.data(), static_cast<std::uint32_t>(GetLocalPlayerExpected.size()));
	if (!localDataContextMatches || !localPlayerMatches) {
		if (!localDataContextMatches) context->LogWarn("Character Resistance Display: GetLocalDataContext signature mismatch; stat reads are disabled.");
		if (!localPlayerMatches) context->LogWarn("Character Resistance Display: GetLocalPlayer signature mismatch; stat reads are disabled.");
		return false;
	}

	GetLocalDataContext = reinterpret_cast<GetLocalDataContextFn>(context->exeBase + GetLocalDataContextRva);
	GetLocalPlayer = reinterpret_cast<GetLocalPlayerFn>(context->exeBase + GetLocalPlayerRva);
	const HMODULE core = GetModuleHandleW(L"D2RCore.dll");
	ReadWideUnitStat = core == nullptr ? nullptr : reinterpret_cast<ReadWideUnitStatFn>(GetProcAddress(core, "ReadWideUnitStat"));
	if (ReadWideUnitStat == nullptr) {
		context->LogWarn("Character Resistance Display: D2RCore ReadWideUnitStat export is unavailable; stat reads are disabled.");
		return false;
	}
	return true;
}

auto ValidateMercenaryReaders(const D2RL::PluginContext* context) noexcept -> bool {
	if (context->exeBase == 0) return false;
	const bool unitMatches = context->CheckExpectedBytes(
		GetUnitRva, GetUnitExpected.data(), static_cast<std::uint32_t>(GetUnitExpected.size()));
	const bool mercenaryIdMatches = context->CheckExpectedBytes(
		GetMercenaryIdRva, GetMercenaryIdExpected.data(), static_cast<std::uint32_t>(GetMercenaryIdExpected.size()));
	if (!unitMatches || !mercenaryIdMatches) {
		if (!unitMatches) context->LogWarn("Character Resistance Display: GetUnit signature mismatch; mercenary stat reads are disabled.");
		if (!mercenaryIdMatches) context->LogWarn("Character Resistance Display: GetPet signature mismatch; mercenary stat reads are disabled.");
		return false;
	}
	GetUnit = reinterpret_cast<GetUnitFn>(context->exeBase + GetUnitRva);
	GetMercenaryId = reinterpret_cast<GetMercenaryIdFn>(context->exeBase + GetMercenaryIdRva);
	return true;
}

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &PluginInfo;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (!D2RL::HasContext(context)) return false;
	Context = context;
	WidgetApi = nullptr;
	ThreadApi = nullptr;
	LifecycleApi = nullptr;
	WorkerRunning.store(false, std::memory_order_release);
	StatsAvailable.store(false, std::memory_order_release);
	MercenaryStatsAvailable.store(false, std::memory_order_release);
	NativeReadersAvailable.store(false, std::memory_order_release);
	MercenaryReadersAvailable.store(false, std::memory_order_release);
	LifecycleTrackingReady.store(false, std::memory_order_release);
	LocalPlayerReady.store(false, std::memory_order_release);
	ReadFailureWarningLogged.store(false, std::memory_order_release);
	MercenaryReadFailureWarningLogged.store(false, std::memory_order_release);
	PlayerStateEpoch.store(0, std::memory_order_release);
	GameRefreshQueued.store(false, std::memory_order_release);
	UiRefreshQueued.store(false, std::memory_order_release);
	ResetCharacterPanelCache();
	ResetMercenaryPanelCache();
	NativeWidgetWarningLogged = false;
	NativeTextWarningLogged = false;
	NativeRectWarningLogged = false;
	MercenaryLayoutWarningLogged = false;
	MercenaryNativeWidgetWarningLogged = false;
	MercenaryNativeTextWarningLogged = false;
	MercenaryNativeRectWarningLogged = false;
	NativeUiAvailable = false;
	LayoutWarningLogged = false;
	FindTopLevelPanel = nullptr;
	FindChildWidgetByName = nullptr;
	GetNativeWidgetRect = nullptr;
	GetUnit = nullptr;
	GetMercenaryId = nullptr;
	CanSetText = false;
	CanSetWidgetRect = false;
	CanSetWidgetVisible = false;
	CanGetWidgetState = false;
	LoadConfig(context);
	const auto& enabled = Config.enabled;
	InstallLooseUiFiles(context, enabled.characterPhysical || enabled.characterMagic,
		enabled.mercenaryPhysical || enabled.mercenaryMagic,
		enabled.controllerCharacterPhysical || enabled.controllerCharacterMagic);
	(void)SettingsMenu.Start(context, SettingsMenuSchema, GetMenuSetting, SetMenuSetting, nullptr);
	if (context->QueryService(&WidgetApi) != D2RL::ServiceQueryResult::Success
		|| !D2RL::HasWidgetServiceField(WidgetApi, D2RL::WidgetServiceRequiredSize)
		|| WidgetApi->findPanel == nullptr || WidgetApi->findWidget == nullptr) {
		WidgetApi = nullptr;
		context->LogWarn("Character Resistance Display: WidgetService panel lookup is unavailable; character-sheet updates are disabled.");
		return true;
	}
	CanSetText = D2RL::HasWidgetServiceField(WidgetApi, D2RL::WidgetServiceSetTextFieldEnd) && WidgetApi->setText != nullptr;
	CanSetWidgetRect = D2RL::HasWidgetServiceField(WidgetApi, D2RL::WidgetServiceSetWidgetRectFieldEnd) && WidgetApi->setWidgetRect != nullptr;
	// WidgetServiceRequiredSize covers this field on both legacy and current tables.
	CanSetWidgetVisible = WidgetApi->setWidgetVisible != nullptr;
	CanGetWidgetState = D2RL::HasWidgetServiceField(WidgetApi, D2RL::WidgetServiceGetWidgetStateFieldEnd) && WidgetApi->getWidgetState != nullptr;
	NativeUiAvailable = (!CanSetText || !CanSetWidgetRect) && ValidateNativeUi(context);
	if (!CanSetText) context->LogInfo(NativeUiAvailable
		? "Character Resistance Display: WidgetService.setText is unavailable; using the validated native text fallback."
		: "Character Resistance Display: WidgetService.setText and its native fallback are unavailable.");
	if (!CanSetWidgetRect) context->LogInfo(NativeUiAvailable
		? "Character Resistance Display: WidgetService.setWidgetRect is unavailable; using the validated native position fallback."
		: "Character Resistance Display: WidgetService.setWidgetRect and its native fallback are unavailable.");
	if ((!CanSetText || !CanSetWidgetRect) && !NativeUiAvailable)
		context->LogWarn("Character Resistance Display: the loader lacks widget setters and the native fallback did not validate; affected updates are disabled.");
	if (context->QueryService(&ThreadApi) != D2RL::ServiceQueryResult::Success
		|| !D2RL::HasThreadServiceField(ThreadApi, D2RL::ThreadServiceRequiredSize)
		|| ThreadApi->runOnUiThread == nullptr || ThreadApi->runOnGameThread == nullptr) {
		ThreadApi = nullptr;
		context->LogWarn("Character Resistance Display: ThreadService is unavailable; character-sheet updates are disabled.");
		return true;
	}
	NativeReadersAvailable.store(ValidateNativeReaders(context), std::memory_order_release);
	if (!NativeReadersAvailable.load(std::memory_order_acquire)) {
		context->LogWarn("Character Resistance Display: continuing with the character-sheet UI active; unavailable values will show as --.");
	}
	const bool mercenaryReadersAvailable = NativeReadersAvailable.load(std::memory_order_acquire)
		&& ValidateMercenaryReaders(context);
	MercenaryReadersAvailable.store(mercenaryReadersAvailable, std::memory_order_release);
	if (NativeReadersAvailable.load(std::memory_order_acquire) && !mercenaryReadersAvailable)
		context->LogWarn("Character Resistance Display: mercenary resistance reads are disabled; player values remain available.");
	LifecycleTrackingReady.store(RegisterGameplayEventListeners(context), std::memory_order_release);
	Context = context;
	WorkerRunning.store(true, std::memory_order_release);
	if (!CreateTimerQueueTimer(&RefreshTimer, nullptr, RefreshWorkerTick, nullptr, 0, RefreshIntervalMs, WT_EXECUTEDEFAULT)) {
		WorkerRunning.store(false, std::memory_order_release);
		const DWORD error = GetLastError();
		char message[128]{};
		(void)std::snprintf(message, sizeof(message), "Character Resistance Display: couldn't start the stat refresh timer (Windows error %lu).", static_cast<unsigned long>(error));
		context->LogWarn(message);
		return false;
	}
	if (!CanSetWidgetVisible) context->LogWarn("Character Resistance Display: WidgetService.setWidgetVisible is unavailable; TOML display switches and cap-value coloring may not be applied.");
	context->LogInfo("Character Resistance Display loaded; native reads are signature-guarded and text positions come from TOML.");
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	WorkerRunning.store(false, std::memory_order_release);
	SettingsMenu.Stop();
	if (RefreshTimer != nullptr) {
		(void)DeleteTimerQueueTimer(nullptr, RefreshTimer, INVALID_HANDLE_VALUE);
		RefreshTimer = nullptr;
	}
	GameRefreshQueued.store(false, std::memory_order_release);
	UiRefreshQueued.store(false, std::memory_order_release);
	LifecycleTrackingReady.store(false, std::memory_order_release);
	LocalPlayerReady.store(false, std::memory_order_release);
	NativeReadersAvailable.store(false, std::memory_order_release);
	MercenaryReadersAvailable.store(false, std::memory_order_release);
	StatsAvailable.store(false, std::memory_order_release);
	MercenaryStatsAvailable.store(false, std::memory_order_release);
	PlayerStateEpoch.fetch_add(1, std::memory_order_acq_rel);
	Context = nullptr;
	WidgetApi = nullptr;
	ThreadApi = nullptr;
	LifecycleApi = nullptr;
	FindTopLevelPanel = nullptr;
	FindChildWidgetByName = nullptr;
	GetNativeWidgetRect = nullptr;
	GetUnit = nullptr;
	GetMercenaryId = nullptr;
	NativeUiAvailable = false;
	CanSetText = false;
	CanSetWidgetRect = false;
	CanSetWidgetVisible = false;
	CanGetWidgetState = false;
}
