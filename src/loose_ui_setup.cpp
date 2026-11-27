#include "install_token.hpp"
#include "loose_ui_setup.h"
#include "setup_resources.h"

#include <Windows.h>
#include <nlohmann/json.hpp>

#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {
namespace fs = std::filesystem;
using Json = nlohmann::ordered_json;

constexpr auto BaseFiles = L"d2rloader\\plugins\\basefiles-and-backups\\basefiles";
constexpr auto BackupFiles = L"d2rloader\\plugins\\basefiles-and-backups\\backups\\character-resistance-display";
constexpr auto InstallMutex = L"Local\\D2RLoader.ModFirstRunInstall.v1";
constexpr DWORD Missing = INVALID_FILE_ATTRIBUTES;

[[noreturn]] void ThrowWindowsError(DWORD code, const char* operation) {
	throw std::system_error(static_cast<int>(code), std::system_category(), operation);
}

auto ExtendedPath(fs::path path) -> fs::path {
	const auto& name = path.make_preferred().native();
	if (name.starts_with(L"\\\\?\\")) return path;
	if (name.starts_with(L"\\\\")) return L"\\\\?\\UNC\\" + name.substr(2);
	return L"\\\\?\\" + name;
}

auto Attributes(const fs::path& path) -> DWORD {
	const DWORD attributes = GetFileAttributesW(ExtendedPath(path).c_str());
	if (attributes != Missing) return attributes;
	const DWORD error = GetLastError();
	if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return Missing;
	ThrowWindowsError(error, "checking file attributes");
}

// Refuse junctions and symlinks under the active mod so its loose tree cannot
// redirect any installation or backup writes.
auto CheckedAttributes(const fs::path& modRoot, const fs::path& relative) -> DWORD {
	auto path = modRoot;
	for (auto it = relative.begin(); it != relative.end(); ++it) {
		if (*it == L".." || it->has_root_path()) throw std::runtime_error("invalid setup path");
		path /= *it;
		const DWORD attributes = Attributes(path);
		if (attributes == Missing) return Missing;
		if (attributes & FILE_ATTRIBUTE_REPARSE_POINT)
			throw std::runtime_error("setup path uses a link or junction");
		if (std::next(it) == relative.end()) return attributes;
		if (!(attributes & FILE_ATTRIBUTE_DIRECTORY))
			throw std::runtime_error("a setup directory is occupied by a file");
	}
	throw std::runtime_error("empty setup path");
}

bool SameName(const fs::path& left, const fs::path& right) noexcept {
	return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
}

struct LooseLocation {
	fs::path modRoot;
	fs::path data;
};

auto FindLooseLocation(const D2RL::PluginContext& context) -> LooseLocation {
	if (context.loadScope != D2RL::LoadScope::Mod || context.activeMod == nullptr) return {};
	const std::string_view name(context.activeMod);
	if (name.empty()) return {};
	const fs::path mod(std::u8string(name.begin(), name.end()));
	if (mod.has_root_path() || mod.has_parent_path() || mod == L"." || mod == L".."
		|| mod.native().find_first_of(L"<>:\"/\\|?*") != std::wstring::npos
		|| mod.native().back() == L'.' || mod.native().back() == L' ') return {};

	const fs::path package = mod.native() + L".mpq";
	for (const auto* location : {context.modDirectory, context.scopeRootDirectory}) {
		if (location == nullptr || !*location) continue;
		auto root = fs::path(location).lexically_normal().make_preferred();
		if (!root.is_absolute()) continue;
		if (root.filename().empty()) root = root.parent_path();
		if (SameName(root.filename(), package)) root = root.parent_path();

		for (const auto& candidate : {root, root / mod, root / L"mods" / mod}) {
			if (!SameName(candidate.filename(), mod)) continue;
			const DWORD attributes = Attributes(candidate);
			if (attributes == Missing || !(attributes & FILE_ATTRIBUTE_DIRECTORY)
				|| (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;

			const auto modRoot = fs::canonical(candidate);
			const DWORD packageAttributes = CheckedAttributes(modRoot, package);
			if (packageAttributes != Missing) {
				if (packageAttributes & FILE_ATTRIBUTE_DIRECTORY) return {modRoot, package / L"data"};
				return {}; // Packed archive: don't create a shadow loose tree.
			}
			const DWORD dataAttributes = CheckedAttributes(modRoot, L"data");
			if (dataAttributes != Missing && (dataAttributes & FILE_ATTRIBUTE_DIRECTORY))
				return {modRoot, L"data"};
		}
	}
	return {};
}

class ModInstallLock {
public:
	ModInstallLock() {
		handle_ = CreateMutexW(nullptr, FALSE, InstallMutex);
		if (handle_ == nullptr) ThrowWindowsError(GetLastError(), "opening the shared mod-install lock");
		const DWORD result = WaitForSingleObject(handle_, 60'000);
		if (result == WAIT_OBJECT_0 || result == WAIT_ABANDONED) return;
		const DWORD error = result == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
		CloseHandle(handle_);
		ThrowWindowsError(error, "waiting for the shared mod-install lock");
	}

	ModInstallLock(const ModInstallLock&) = delete;
	ModInstallLock& operator=(const ModInstallLock&) = delete;

	~ModInstallLock() {
		ReleaseMutex(handle_);
		CloseHandle(handle_);
	}

private:
	HANDLE handle_{};
};

class StagedFile {
public:
	explicit StagedFile(const fs::path& parent)
		: directory_(parent / (L".character-resistance-display-" + std::to_wstring(GetCurrentProcessId())
			+ L"-" + std::to_wstring(GetTickCount64()))), file_(directory_ / L"copy.tmp") {
		if (!CreateDirectoryW(directory_.c_str(), nullptr))
			ThrowWindowsError(GetLastError(), "creating a UI file staging directory");
	}

	StagedFile(const StagedFile&) = delete;
	StagedFile& operator=(const StagedFile&) = delete;

	~StagedFile() {
		DeleteFileW(file_.c_str());
		RemoveDirectoryW(directory_.c_str());
	}

	void Publish(const fs::path& target, std::string_view contents, bool replace) const {
		std::ofstream out(file_, std::ios::binary);
		out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
		out.close();
		if (!out) throw std::runtime_error("could not stage a UI file");
		const DWORD flags = MOVEFILE_WRITE_THROUGH | (replace ? MOVEFILE_REPLACE_EXISTING : 0);
		if (!MoveFileExW(file_.c_str(), target.c_str(), flags))
			ThrowWindowsError(GetLastError(), "publishing a UI file");
	}

private:
	fs::path directory_;
	fs::path file_;
};

auto ReadOptional(const fs::path& root, const fs::path& relative) -> std::optional<std::string> {
	const DWORD attributes = CheckedAttributes(root, relative);
	if (attributes == Missing) return std::nullopt;
	if (attributes & FILE_ATTRIBUTE_DIRECTORY)
		throw std::runtime_error("a UI file path is occupied by a directory");
	std::ifstream in(ExtendedPath(root / relative), std::ios::binary | std::ios::ate);
	const auto size = in.tellg();
	if (!in || size < 0 || size > 64 * 1024 * 1024)
		throw std::runtime_error("could not read UI file or it exceeds the setup size limit");
	std::string contents(static_cast<std::size_t>(size), '\0');
	if (!in.seekg(0) || !in.read(contents.data(), static_cast<std::streamsize>(contents.size())))
		throw std::runtime_error("could not read the complete UI file");
	return contents;
}

void WriteAtomic(const fs::path& root, const fs::path& relative, std::string_view contents, bool replace) {
	const DWORD attributes = CheckedAttributes(root, relative);
	if (attributes != Missing && (!replace || (attributes & FILE_ATTRIBUTE_DIRECTORY)))
		throw std::runtime_error("setup destination already exists");
	const auto path = ExtendedPath(root / relative);
	fs::create_directories(path.parent_path());
	StagedFile(path.parent_path()).Publish(path, contents, replace);
}

auto InstallerResource(int id) -> std::string_view {
	static const char moduleAnchor = 0;
	HMODULE module{};
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(&moduleAnchor), &module))
		throw std::runtime_error("could not locate the plugin's installer resources");
	const auto resource = FindResourceW(module, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10)); // RCDATA
	if (!resource) throw std::runtime_error("the plugin is missing a bundled UI resource");
	const auto* bytes = static_cast<const char*>(LockResource(LoadResource(module, resource)));
	const auto size = SizeofResource(module, resource);
	if (!bytes || size == 0) throw std::runtime_error("could not load a bundled UI resource");
	return {bytes, size};
}

// D2R's UI JSON accepts comments and trailing commas. Remove only that syntax
// before parsing; escaped quotes, comment markers, and commas in strings stay.
auto ParseLayout(std::string_view text) -> Json {
	std::string clean;
	clean.reserve(text.size());
	bool insideString = false;
	std::size_t lastToken = std::string::npos;
	for (std::size_t i = 0; i < text.size(); ++i) {
		const char ch = text[i];
		if (insideString) {
			clean += ch;
			if (ch == '\\' && i + 1 < text.size()) clean += text[++i];
			else if (ch == '"') {
				insideString = false;
				lastToken = clean.size() - 1;
			}
			continue;
		}
		if (ch == '/' && i + 1 < text.size()) {
			if (text[i + 1] == '/') {
				const auto end = text.find_first_of("\r\n", i + 2);
				if (end == text.npos) break;
				i = end - 1;
				continue;
			}
			if (text[i + 1] == '*') {
				const auto end = text.find("*/", i + 2);
				if (end == text.npos) throw std::runtime_error("unterminated UI layout comment");
				clean += ' ';
				i = end + 1;
				continue;
			}
		}
		if ((ch == '}' || ch == ']') && lastToken != clean.npos && clean[lastToken] == ',')
			clean[lastToken] = ' ';
		clean += ch;
		if (ch == '"') insideString = true;
		if (ch != ' ' && ch != '\r' && ch != '\n' && ch != '\t') lastToken = clean.size() - 1;
	}
	return Json::parse(clean);
}

struct Panel {
	bool enabled;
	const wchar_t* layout;
	const wchar_t* sprite;
	const char* type;
	int widgets;
};

auto MergeWidgets(std::string_view original, const Panel& panel) -> std::optional<std::string> {
	auto layout = ParseLayout(original);
	if (!layout.is_object() || layout.value("type", "") != panel.type
		|| !layout.contains("children") || !layout["children"].is_array())
		throw std::runtime_error("UI layout has the wrong panel type or no children array");
	auto& children = layout["children"];
	const auto widgets = Json::parse(InstallerResource(panel.widgets));
	if (!widgets.is_array() || widgets.size() != 6) throw std::runtime_error("invalid bundled widget list");
	bool changed = false;
	for (const auto& widget : widgets) {
		const auto name = widget.at("name").get<std::string>();
		auto found = false;
		for (const auto& child : children) {
			if (!child.is_object() || !child.contains("name") || child["name"] != name) continue;
			if (found || child.value("type", "") != "TextBoxWidget")
				throw std::runtime_error("conflicting resistance widget: " + name);
			found = true;
		}
		if (!found) {
			children.push_back(widget);
			changed = true;
		}
	}
	if (!changed) return std::nullopt;
	return layout.dump(4) + '\n';
}

struct Edit {
	fs::path target;
	std::optional<std::string> original;
	std::string contents;
};

auto BackupLocation(int panelId) -> fs::path {
	SYSTEMTIME now{};
	GetSystemTime(&now);
	wchar_t name[96]{};
	std::swprintf(name, std::size(name), L"%04u%02u%02uT%02u%02u%02u%03uZ-%lu-%llu-%d",
		static_cast<unsigned>(now.wYear), static_cast<unsigned>(now.wMonth), static_cast<unsigned>(now.wDay),
		static_cast<unsigned>(now.wHour), static_cast<unsigned>(now.wMinute), static_cast<unsigned>(now.wSecond),
		static_cast<unsigned>(now.wMilliseconds), GetCurrentProcessId(), GetTickCount64(), panelId);
	return fs::path(BackupFiles) / name;
}

void ApplyEdits(const D2RL::PluginContext& context, const fs::path& root, int panelId, const std::vector<Edit>& edits) {
	const auto backup = BackupLocation(panelId);
	const auto backupPath = ExtendedPath(root / backup);
	(void)CheckedAttributes(root, backup);
	fs::create_directories(backupPath.parent_path());
	if (!fs::create_directory(backupPath)) throw std::runtime_error("setup backup directory already exists");

	std::string created;
	for (const auto& edit : edits) {
		if (edit.original) WriteAtomic(root, backup / edit.target, *edit.original, false);
		else {
			const auto relative = edit.target.u8string();
			created.append(reinterpret_cast<const char*>(relative.data()), relative.size());
			created += '\n';
		}
	}
	WriteAtomic(root, backup / L"created-files.txt", created, false);

	std::size_t applied = 0;
	try {
		for (const auto& edit : edits) {
			if (ReadOptional(root, edit.target) != edit.original)
				throw std::runtime_error("a loose UI file changed during setup");
			WriteAtomic(root, edit.target, edit.contents, edit.original.has_value());
			++applied;
		}
	} catch (...) {
		bool restored = true;
		while (applied > 0) {
			const auto& edit = edits[--applied];
			try {
				if (ReadOptional(root, edit.target) != edit.contents) {
					restored = false;
					continue;
				}
				if (edit.original) WriteAtomic(root, edit.target, *edit.original, true);
				else if (!fs::remove(ExtendedPath(root / edit.target))) restored = false;
			} catch (...) {
				restored = false;
			}
		}
		if (!restored) context.LogWarn(
			"Character Resistance Display: could not fully roll back UI setup. See the plugin's backup folder.");
		throw;
	}
}

bool InstallPanel(const D2RL::PluginContext& context, const LooseLocation& location, const Panel& panel) {
	const auto& root = location.modRoot;
	const auto layoutTarget = location.data / panel.layout;
	auto layout = ReadOptional(root, layoutTarget);
	const auto baseLayout = layout ? std::nullopt : ReadOptional(root, fs::path(BaseFiles) / panel.layout);
	if (!layout && !baseLayout)
		throw std::runtime_error("layout is missing in both loose data and basefiles");

	const auto& input = layout ? *layout : *baseLayout;
	auto merged = MergeWidgets(input, panel);
	std::vector<Edit> edits;
	const auto spriteTarget = location.data / panel.sprite;
	const DWORD spriteAttributes = CheckedAttributes(root, spriteTarget);
	if (spriteAttributes != Missing && (spriteAttributes & FILE_ATTRIBUTE_DIRECTORY))
		throw std::runtime_error("the sprite path is occupied by a directory");
	if (panel.widgets == CRD_DESKTOP_WIDGETS) {
		if (spriteAttributes == Missing) {
			edits.push_back({spriteTarget, std::nullopt, std::string(InstallerResource(CRD_DESKTOP_SPRITE))});
		} else if (merged || !layout) {
			// A loose sprite can be custom. Replace it only if it matches
			// the original supplied alongside the layouts in basefiles.
			const auto baseSprite = ReadOptional(root, fs::path(BaseFiles) / panel.sprite);
			if (baseSprite) {
				const auto bundled = InstallerResource(CRD_DESKTOP_SPRITE);
				auto current = ReadOptional(root, spriteTarget);
				if (current == baseSprite && *current != bundled)
					edits.push_back({spriteTarget, std::move(current), std::string(bundled)});
			}
		}
	} else if (spriteAttributes == Missing) {
		auto sprite = ReadOptional(root, fs::path(BaseFiles) / panel.sprite);
		if (!sprite || sprite->empty())
			throw std::runtime_error("modified panel sprite is missing; supply it in loose data or basefiles");
		edits.push_back({spriteTarget, std::nullopt, std::move(*sprite)});
	}

	if (merged) edits.push_back({layoutTarget, std::move(layout), std::move(*merged)});
	else if (!layout) edits.push_back({layoutTarget, std::nullopt, *baseLayout});
	if (edits.empty()) return false;
	ApplyEdits(context, root, panel.widgets, edits);
	return true;
}

} // namespace

void InstallLooseUiFiles(const D2RL::PluginContext* context, bool desktop, bool mercenary, bool controller) noexcept {
	if (!D2RL::HasContext(context) || (!desktop && !mercenary && !controller)) return;
	try {
		const auto location = FindLooseLocation(*context);
		if (location.modRoot.empty()) return;
		const Panel panels[]{
			{desktop, L"global\\ui\\layouts\\characterstatspanelhd.json",
				L"hd\\global\\ui\\panel\\character_sheet\\character_background.sprite", "CharacterStatsPanel", CRD_DESKTOP_WIDGETS},
			{mercenary, L"global\\ui\\layouts\\hirelinginventorypanelhd.json",
				L"hd\\global\\ui\\panel\\hireling\\hirelingpanel.sprite", "HirelingInventoryPanel", CRD_MERCENARY_WIDGETS},
			{controller, L"global\\ui\\layouts\\controller\\characterstatspanelhd.json",
				L"hd\\global\\ui\\controller\\panel\\charactersheet\\v2\\CharacterSheet_BG.sprite", "CharacterStatsPanel", CRD_CONTROLLER_WIDGETS},
		};
		const ModInstallLock lock;
		bool changed = false;
		for (const auto& panel : panels) {
			if (!panel.enabled) continue;
			try {
				const auto section=panel.widgets==CRD_DESKTOP_WIDGETS ? "installation.desktop"
                    : panel.widgets==CRD_MERCENARY_WIDGETS ? "installation.mercenary" : "installation.controller";
                if (InstallToken::Complete(*context,section)) continue;
                if (InstallPanel(*context, location, panel)) changed = true;
                InstallToken::Commit(*context,{},section);
			} catch (const std::exception& error) {
				char message[640]{};
				std::snprintf(message, sizeof(message), "Character Resistance Display: could not set up '%ls': %s",
					panel.layout, error.what());
				context->LogWarn(message);
			}
		}
		if (changed) context->LogInfo(
			"Character Resistance Display: installed resistance UI files. Restart D2RLoader with the same mod to load them.");
	} catch (const std::exception& error) {
		char message[512]{};
		std::snprintf(message, sizeof(message), "Character Resistance Display: loose UI setup skipped: %s", error.what());
		context->LogWarn(message);
	}
}
