#include <stdafx.h>
#include <OsirisProxy.h>
#include <PropertyMaps.h>
#include <GameDefinitions/Ai.h>
#include <GameDefinitions/RootTemplates.h>
#include <Lua/LuaSerializers.h>
#include <Lua/LuaBinding.h>
#include "resource.h"
#include <fstream>

// Callback from the Lua runtime when a handled (i.e. pcall/xpcall'd) error was thrown.
// This is needed to capture errors for the Lua debugger, as there is no
// built-in mechanism to capture handled errors.
//
// Lua ldebug.c (line ~639) must be patched like this:
//
// void nse_lua_report_handled_error(lua_State* L);
// 
// l_noret luaG_errormsg(lua_State* L) {
// 	if (L->errfunc != 0) {  /* is there an error handling function? */
// 	  nse_lua_report_handled_error(L);
//    StkId errfunc = restorestack(L, L->errfunc);
//
void nse_lua_report_handled_error(lua_State* L)
{
#if !defined(OSI_NO_DEBUGGER)
	char const* err = "(Unknown)";
	if (lua_type(L, -1) == LUA_TSTRING) {
		err = lua_tostring(L, -1);
	}

	auto debugger = dse::gOsirisProxy->GetLuaDebugger();
	if (debugger) {
		debugger->OnLuaError(L, err);
	}
#endif
}

namespace dse::lua
{
	RegistryEntry::RegistryEntry()
		: L_(nullptr), ref_(-1)
	{}

	RegistryEntry::RegistryEntry(lua_State * L, int index)
		: L_(L)
	{
		lua_pushvalue(L, index);
		ref_ = luaL_ref(L, LUA_REGISTRYINDEX);
	}

	RegistryEntry::~RegistryEntry()
	{
		if (ref_ != -1) {
			luaL_unref(L_, LUA_REGISTRYINDEX, ref_);
		}
	}

	RegistryEntry::RegistryEntry(RegistryEntry && other)
		: L_(other.L_), ref_(other.ref_)
	{
		other.ref_ = -1;
	}

	RegistryEntry & RegistryEntry::operator = (RegistryEntry && other)
	{
		L_ = other.L_;
		ref_ = other.ref_;
		other.ref_ = -1;
		return *this;
	}

	void RegistryEntry::Push() const
	{
		assert(ref_ != -1);
		lua_rawgeti(L_, LUA_REGISTRYINDEX, ref_);
	}


	int TracebackHandler(lua_State * L)
	{
		const char *msg = lua_tostring(L, 1);
		if (msg == NULL) {  /* is error object not a string? */
			if (luaL_callmeta(L, 1, "__tostring") &&  /* does it have a metamethod */
				lua_type(L, -1) == LUA_TSTRING)  /* that produces a string? */
				return 1;  /* that is the message */
			else
				msg = lua_pushfstring(L, "(error object is a %s value)",
					luaL_typename(L, 1));
		}
		luaL_traceback(L, L, msg, 1);  /* append a standard traceback */
		return 1;  /* return the traceback */
	}

	int CallWithTraceback(lua_State * L, int narg, int nres)
	{
		int base = lua_gettop(L) - narg;  /* function index */
		lua_pushcfunction(L, &TracebackHandler);  /* push message handler */
		lua_insert(L, base);  /* put it under function and args */
		int status = lua_pcall(L, narg, nres, base);
		lua_remove(L, base);  /* remove message handler from the stack */
		return status;
	}


	void PushExtFunction(lua_State * L, char const * func)
	{
		lua_getglobal(L, "Ext"); // stack: Ext
		lua_getfield(L, -1, func); // stack: Ext, fn
		lua_remove(L, -2); // stack: fn
	}


	void PushModFunction(lua_State* L, char const* mod, char const* func)
	{
		lua_getglobal(L, "Mods"); // stack: Mods
		lua_getfield(L, -1, mod); // stack: Mods, mod
		lua_remove(L, -2); // stack: mod
		lua_getfield(L, -1, func); // stack: mod, fn
		lua_remove(L, -2); // stack: fn
	}



	char const* const ObjectProxy<CombatComponentTemplate>::MetatableName = "eoc::CombatComponentTemplate";

	CombatComponentTemplate* ObjectProxy<CombatComponentTemplate>::Get(lua_State* L)
	{
		if (obj_) return obj_;
		luaL_error(L, "CharacterTemplate not bound!");
		return nullptr;
	}

	int ObjectProxy<CombatComponentTemplate>::Index(lua_State* L)
	{
		return GenericGetter(L, gCombatComponentTemplatePropertyMap);
	}

	int ObjectProxy<CombatComponentTemplate>::NewIndex(lua_State* L)
	{
		return GenericSetter(L, gCombatComponentTemplatePropertyMap);
	}



	char const* const ObjectProxy<CharacterTemplate>::MetatableName = "eoc::CharacterTemplate";

	CharacterTemplate* ObjectProxy<CharacterTemplate>::Get(lua_State* L)
	{
		if (obj_) return obj_;
		luaL_error(L, "CharacterTemplate not bound!");
		return nullptr;
	}

	int ObjectProxy<CharacterTemplate>::Index(lua_State* L)
	{
		auto obj = Get(L);
		if (!obj) return 0;

		auto prop = luaL_checkstring(L, 2);

		if (strcmp(prop, "CombatTemplate") == 0) {
			ObjectProxy<CombatComponentTemplate>::New(L, &obj->CombatComponent);
			return 1;
		}

		auto fetched = LuaPropertyMapGet(L, gCharacterTemplatePropertyMap, obj, prop, true);
		return fetched ? 1 : 0;
	}

	int ObjectProxy<CharacterTemplate>::NewIndex(lua_State* L)
	{
		return GenericSetter(L, gCharacterTemplatePropertyMap);
	}



	char const* const ObjectProxy<ItemTemplate>::MetatableName = "eoc::ItemTemplate";

	ItemTemplate* ObjectProxy<ItemTemplate>::Get(lua_State* L)
	{
		if (obj_) return obj_;
		luaL_error(L, "ItemTemplate not bound!");
		return nullptr;
	}

	int ObjectProxy<ItemTemplate>::Index(lua_State* L)
	{
		auto obj = Get(L);
		if (!obj) return 0;

		auto prop = luaL_checkstring(L, 2);

		if (strcmp(prop, "CombatTemplate") == 0) {
			ObjectProxy<CombatComponentTemplate>::New(L, &obj->CombatComponent);
			return 1;
		}

		auto fetched = LuaPropertyMapGet(L, gItemTemplatePropertyMap, obj, prop, true);
		return fetched ? 1 : 0;
	}

	int ObjectProxy<ItemTemplate>::NewIndex(lua_State* L)
	{
		return GenericSetter(L, gItemTemplatePropertyMap);
	}



	char const* const ObjectProxy<ProjectileTemplate>::MetatableName = "eoc::ProjectileTemplate";

	ProjectileTemplate* ObjectProxy<ProjectileTemplate>::Get(lua_State* L)
	{
		if (obj_) return obj_;
		luaL_error(L, "ProjectileTemplate not bound!");
		return nullptr;
	}

	int ObjectProxy<ProjectileTemplate>::Index(lua_State* L)
	{
		return GenericGetter(L, gProjectileTemplatePropertyMap);
	}

	int ObjectProxy<ProjectileTemplate>::NewIndex(lua_State* L)
	{
		return GenericSetter(L, gProjectileTemplatePropertyMap);
	}


	char const* const ObjectProxy<SurfaceTemplate>::MetatableName = "eoc::SurfaceTemplate";

	SurfaceTemplate* ObjectProxy<SurfaceTemplate>::Get(lua_State* L)
	{
		if (obj_) return obj_;
		luaL_error(L, "SurfaceTemplate not bound!");
		return nullptr;
	}

	int ObjectProxy<SurfaceTemplate>::Index(lua_State* L)
	{
		auto obj = Get(L);
		auto prop = checked_get<char const*>(L, 2);

		if (strcmp(prop, "Statuses") == 0) {
			return LuaWrite(L, obj->Statuses.Value);
		}

		return GenericGetter(L, gSurfaceTemplatePropertyMap);
	}

	int ObjectProxy<SurfaceTemplate>::NewIndex(lua_State* L)
	{
		auto obj = Get(L);
		auto prop = checked_get<char const*>(L, 2);

		if (strcmp(prop, "Statuses") == 0) {
			lua_pushvalue(L, 3);
			LuaRead(L, obj->Statuses.Value);
			lua_pop(L, 1);
			return 0;
		}

		return GenericSetter(L, gSurfaceTemplatePropertyMap);
	}


	char const* const ObjectProxy<eoc::AiGrid>::MetatableName = "eoc::AiGrid";

	eoc::AiGrid* ObjectProxy<eoc::AiGrid>::Get(lua_State* L)
	{
		return obj_;
	}

	int AiGridSearchForCell(lua_State* L)
	{
		auto grid = ObjectProxy<eoc::AiGrid>::CheckedGet(L, 1);
		auto x = checked_get<float>(L, 2);
		auto z = checked_get<float>(L, 3);
		auto radius = checked_get<float>(L, 4);
		auto flags = checked_get_flags<ESurfaceFlag>(L, 5);
		auto bias = checked_get<float>(L, 6);

		auto search = GetStaticSymbols().eoc__AiGrid__SearchForCell;
		if (!search) {
			OsiError("AiGrid::SearchForCell not mapped!");
			return 0;
		}

		auto result = search(grid, x, z, radius, flags, nullptr, bias);
		push(L, !result);
		return 1;
	}

	int AiGridGetCellInfo(lua_State* L)
	{
		auto grid = ObjectProxy<eoc::AiGrid>::CheckedGet(L, 1);
		auto x = checked_get<float>(L, 2);
		auto z = checked_get<float>(L, 3);

		auto cell = grid->GetCell(glm::vec2(x, z));
		if (!cell) {
			OsiError("Could not find AiGrid cell at " << x << ";" << z);
			return 0;
		}

		auto groundIdx = grid->GetSurfaceIndex(cell, 0);
		auto cloudIdx = grid->GetSurfaceIndex(cell, 1);
		auto meta = grid->GetAiMetaData(cell);

		auto height = cell->Height * 0.25f + grid->DataGrid.OffsetY;

		lua_newtable(L);
		settable(L, "Flags", cell->AiFlags);
		settable(L, "Height", height);

		lua_newtable(L);
		if (meta != nullptr) {
			int32_t aiIdx = 1;
			for (auto ai : meta->Ai) {
				ObjectHandle handle;
				ai->GameObject->GetObjectHandle(handle);
				settable(L, aiIdx++, handle);
			}
		}

		lua_setfield(L, -2, "Objects");

		if (gOsirisProxy->IsInServerThread()) {
			auto level = GetStaticSymbols().GetCurrentServerLevel();
			if (!level || level->AiGrid != grid || !level->SurfaceManager) {
				OsiError("Current level not available yet!");
			} else {
				if (groundIdx != -1) {
					auto surface = level->SurfaceManager->Surfaces[groundIdx];
					settable(L, "GroundSurface", surface->MyHandle);
				}

				if (cloudIdx != -1) {
					auto surface = level->SurfaceManager->Surfaces[cloudIdx];
					settable(L, "CloudSurface", surface->MyHandle);
				}
			}
		}

		return 1;
	}

	int AiGridGetAiFlags(lua_State* L)
	{
		auto grid = ObjectProxy<eoc::AiGrid>::CheckedGet(L, 1);
		auto x = checked_get<float>(L, 2);
		auto z = checked_get<float>(L, 3);

		auto cell = grid->GetCell(glm::vec2(x, z));
		if (!cell) {
			OsiError("Could not find AiGrid cell at " << x << ";" << z);
			return 0;
		}

		push(L, cell->AiFlags);
		return 1;
	}

	int AiGridSetAiFlags(lua_State* L)
	{
		// Only allow updating Walkable/Reachable flags for now
		constexpr uint64_t UpdateFlags = 5;

		auto grid = ObjectProxy<eoc::AiGrid>::CheckedGet(L, 1);
		auto x = checked_get<float>(L, 2);
		auto z = checked_get<float>(L, 3);
		auto flags = checked_get<uint64_t>(L, 4);

		auto cell = grid->GetCell(glm::vec2(x, z));
		if (!cell) {
			OsiError("Could not find AiGrid cell at " << x << ";" << z);
			return 0;
		}

		cell->AiFlags = (flags & UpdateFlags) | (cell->AiFlags & ~UpdateFlags);
		return 0;
	}

	int ObjectProxy<eoc::AiGrid>::Index(lua_State* L)
	{
		auto grid = Get(L);
		if (!grid) return 0;

		auto prop = checked_get<FixedString>(L, 2);

		if (prop == GFS.strSearchForCell) {
			lua_pushcfunction(L, &AiGridSearchForCell);
			return 1;
		}

		if (prop == GFS.strGetCellInfo) {
			lua_pushcfunction(L, &AiGridGetCellInfo);
			return 1;
		}

		if (prop == GFS.strGetAiFlags) {
			lua_pushcfunction(L, &AiGridGetAiFlags);
			return 1;
		}

		if (prop == GFS.strSetAiFlags) {
			lua_pushcfunction(L, &AiGridSetAiFlags);
			return 1;
		}

		if (prop == GFS.strOffsetX) {
			push(L, grid->DataGrid.OffsetX);
			return 1;
		}

		if (prop == GFS.strOffsetY) {
			push(L, grid->DataGrid.OffsetY);
			return 1;
		}

		if (prop == GFS.strOffsetZ) {
			push(L, grid->DataGrid.OffsetZ);
			return 1;
		}

		if (prop == GFS.strGridScale) {
			push(L, grid->DataGrid.GridScale);
			return 1;
		}

		if (prop == GFS.strWidth) {
			push(L, (double)grid->DataGrid.Width * grid->DataGrid.GridScale);
			return 1;
		}

		if (prop == GFS.strHeight) {
			push(L, (double)grid->DataGrid.Height * grid->DataGrid.GridScale);
			return 1;
		}

		OsiError("Unknown eoc::AiGrid property: " << prop);
		return 0;
	}

	int ObjectProxy<eoc::AiGrid>::NewIndex(lua_State* L)
	{
		return luaL_error(L, "__newindex not supported for eoc::AiGrid");
	}


	char const * const DamageList::MetatableName = "CDamageList";

	void DamageList::PopulateMetatable(lua_State * L)
	{
		lua_newtable(L);

		lua_pushcfunction(L, &GetByType);
		lua_setfield(L, -2, "GetByType");

		lua_pushcfunction(L, &Add);
		lua_setfield(L, -2, "Add");

		lua_pushcfunction(L, &Clear);
		lua_setfield(L, -2, "Clear");

		lua_pushcfunction(L, &Multiply);
		lua_setfield(L, -2, "Multiply");

		lua_pushcfunction(L, &Merge);
		lua_setfield(L, -2, "Merge");

		lua_pushcfunction(L, &ConvertDamageType);
		lua_setfield(L, -2, "ConvertDamageType");

		lua_pushcfunction(L, &AggregateSameTypeDamages);
		lua_setfield(L, -2, "AggregateSameTypeDamages");

		lua_pushcfunction(L, &ToTable);
		lua_setfield(L, -2, "ToTable");

		lua_setfield(L, -2, "__index");
	}

	int DamageList::GetByType(lua_State * L)
	{
		auto self = DamageList::CheckUserData(L, 1);
		auto damageType = checked_get<DamageType>(L, 2);

		int32_t amount = 0;
		for (auto const& dmg : self->damages_) {
			if (dmg.DamageType == damageType) {
				amount += dmg.Amount;
			}
		}

		push(L, amount);
		return 1;
	}

	int DamageList::Add(lua_State * L)
	{
		auto self = DamageList::CheckUserData(L, 1);
		auto damageType = checked_get<DamageType>(L, 2);
		auto amount = (int32_t)luaL_checkinteger(L, 3);

		self->damages_.AddDamage(damageType, amount);

		return 0;
	}

	int DamageList::Clear(lua_State * L)
	{
		auto self = DamageList::CheckUserData(L, 1);
		if (lua_gettop(L) >= 2) {
			auto damageType = checked_get<DamageType>(L, 2);
			self->damages_.ClearDamage(damageType);
		} else {
			self->damages_.Clear();
		}

		return 0;
	}

	int DamageList::Multiply(lua_State * L)
	{
		auto self = DamageList::CheckUserData(L, 1);
		auto multiplier = luaL_checknumber(L, 2);

		for (auto& dmg : self->damages_) {
			dmg.Amount = (int32_t)round(dmg.Amount * multiplier);
		}

		return 0;
	}

	int DamageList::Merge(lua_State * L)
	{
		auto self = DamageList::CheckUserData(L, 1);
		auto other = DamageList::CheckUserData(L, 2);

		for (auto const& dmg : other->damages_) {
			self->damages_.AddDamage(dmg.DamageType, dmg.Amount);
		}

		return 0;
	}

	int DamageList::ConvertDamageType(lua_State * L)
	{
		auto self = DamageList::CheckUserData(L, 1);
		auto damageType = checked_get<DamageType>(L, 2);

		int32_t totalDamage = 0;
		for (auto const& dmg : self->damages_) {
			totalDamage += dmg.Amount;
		}

		self->damages_.Clear();
		self->damages_.AddDamage(damageType, totalDamage);

		return 0;
	}

	int DamageList::AggregateSameTypeDamages(lua_State * L)
	{
		auto self = DamageList::CheckUserData(L, 1);

		for (uint32_t i = self->damages_.Size; i > 0; i--) {
			auto & src = self->damages_[i - 1];
			for (uint32_t j = i - 1; j > 0; j--) {
				auto & dest = self->damages_[j - 1];
				if (src.DamageType == dest.DamageType) {
					dest.Amount += src.Amount;
					self->damages_.Remove(i - 1);
					break;
				}
			}
		}

		return 0;
	}

	int DamageList::ToTable(lua_State * L)
	{
		auto self = DamageList::CheckUserData(L, 1);

		lua_newtable(L); // Stack: tab

		for (uint32_t i = 0; i < self->damages_.Size; i++) {
			auto const & item = self->damages_[i];

			push(L, i + 1); // Stack: tab, index
			lua_newtable(L); // Stack: tab, index, dmgTab
			setfield(L, "DamageType", item.DamageType);
			setfield(L, "Amount", item.Amount);

			lua_settable(L, -3); // Stack: tab
		}

		return 1;
	}


	void ExtensionLibrary::Register(lua_State * L)
	{
		RegisterLib(L);
		RegisterStatsObjects(L);
		ObjectProxy<CharacterTemplate>::RegisterMetatable(L);
		ObjectProxy<ItemTemplate>::RegisterMetatable(L);
		ObjectProxy<ProjectileTemplate>::RegisterMetatable(L);
		ObjectProxy<CombatComponentTemplate>::RegisterMetatable(L);
		ObjectProxy<SurfaceTemplate>::RegisterMetatable(L);
		ObjectProxy<eoc::AiGrid>::RegisterMetatable(L);
		DamageList::RegisterMetatable(L);
	}

	int ExtensionLibrary::Include(lua_State * L)
	{
		auto modGuid = luaL_checkstring(L, 1);
		auto fileName = luaL_checkstring(L, 2);

		bool replaceGlobals = !lua_isnil(L, 3);
		auto globalsIdx = lua_gettop(L) + 1;

		if (replaceGlobals) {
			luaL_checktype(L, 3, LUA_TTABLE);
#if LUA_VERSION_NUM > 501
			lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
			lua_pushvalue(L, 3);
			lua_rawseti(L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
#endif
		}

		auto nret = gOsirisProxy->GetCurrentExtensionState()
			->LuaLoadModScript(modGuid, fileName, true, replaceGlobals ? 3 : 0);

		if (replaceGlobals) {
#if LUA_VERSION_NUM > 501
			lua_pushvalue(L, globalsIdx);
			lua_rawseti(L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
			lua_remove(L, globalsIdx);
#endif
		}

		if (nret) {
			return *nret;
		} else {
			return 0;
		}
	}


#if LUA_VERSION_NUM > 501
	// Unsafe libs (IO, OS, package, debug) removed
	static const luaL_Reg loadedlibs[] = {
	  {"_G", luaopen_base},
	  {LUA_TABLIBNAME, luaopen_table},
	  {LUA_STRLIBNAME, luaopen_string},
	  {LUA_MATHLIBNAME, luaopen_math},
	  // debug table is stripped in the sandbox startup script
	  {LUA_DBLIBNAME, luaopen_debug},
	  {NULL, NULL}
	};

#else
	// Unsafe libs (IO, OS, package, debug) removed
	static const luaL_Reg loadedlibs[] = {
	  {"", luaopen_base},
	  {LUA_TABLIBNAME, luaopen_table},
	  {LUA_STRLIBNAME, luaopen_string},
	  {LUA_MATHLIBNAME, luaopen_math},
	  {LUA_BITLIBNAME, luaopen_bit},
	  // debug table is stripped in the sandbox startup script
	  {LUA_DBLIBNAME, luaopen_debug},
	  {NULL, NULL}
	};

	static const luaL_Reg lj_lib_preload[] = {
	#if LJ_HASFFI
	  { LUA_FFILIBNAME,	luaopen_ffi },
	#endif
	  { NULL,		NULL }
	};

#endif

	int LuaPanic(lua_State * L)
	{
		char const* err = "(Unknown)";
		if (lua_type(L, -1) == LUA_TSTRING) {
			err = lua_tostring(L, -1);
		}

#if !defined(OSI_NO_DEBUGGER)
		auto debugger = gOsirisProxy->GetLuaDebugger();
		if (debugger) {
			debugger->OnLuaError(L, err);
		}
#endif

		throw Exception(err);
	}

	State::State()
	{
		L = luaL_newstate();
#if LUA_VERSION_NUM <= 501
		luaJIT_setmode(L, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_ON);
#endif
		lua_atpanic(L, &LuaPanic);
		OpenLibs();
	}

	void RestoreLevelMaps(std::unordered_set<int32_t> const &);

	State::~State()
	{
		RestoreLevelMaps(OverriddenLevelMaps);
		lua_close(L);
	}

	void State::LoadBootstrap(STDString const& path, STDString const& modTable)
	{
		CallExt("_LoadBootstrap", RestrictAll, ReturnType<>{}, path, modTable);
	}

	void State::FinishStartup()
	{
		assert(!startupDone_);
		startupDone_ = true;

#if !defined(OSI_NO_DEBUGGER)
		auto debugger = gOsirisProxy->GetLuaDebugMessageHandler();
		if (debugger && debugger->IsDebuggerReady()) {
			debugger->SendModInfo();
		}
#endif
	}
		
	void State::OpenLibs()
	{
#if LUA_VERSION_NUM > 501
		const luaL_Reg *lib;
		/* "require" functions from 'loadedlibs' and set results to global table */
		for (lib = loadedlibs; lib->func; lib++) {
			luaL_requiref(L, lib->name, lib->func, 1);
			lua_pop(L, 1);  /* remove lib */
		}
#else
		const luaL_Reg* lib;
		for (lib = loadedlibs; lib->func; lib++) {
			lua_pushcfunction(L, lib->func);
			lua_pushstring(L, lib->name);
			lua_call(L, 1, 0);
		}
		luaL_findtable(L, LUA_REGISTRYINDEX, "_PRELOAD",
			sizeof(lj_lib_preload) / sizeof(lj_lib_preload[0]) - 1);
		for (lib = lj_lib_preload; lib->func; lib++) {
			lua_pushcfunction(L, lib->func);
			lua_setfield(L, -2, lib->name);
		}
		lua_pop(L, 1);
#endif
	}

	std::optional<int> State::LoadScript(STDString const & script, STDString const & name, int globalsIdx)
	{
		int top = lua_gettop(L);

		/* Load the file containing the script we are going to run */
		int status = luaL_loadbufferx(L, script.c_str(), script.size(), name.c_str(), "text");
		if (status != LUA_OK) {
			LuaError("Failed to parse script: " << lua_tostring(L, -1));
			lua_pop(L, 1);  /* pop error message from the stack */
			return {};
		}

#if LUA_VERSION_NUM <= 501
		if (globalsIdx != 0) {
			lua_pushvalue(L, globalsIdx);
			lua_setfenv(L, -2);
		}
#endif

		/* Ask Lua to run our little script */
		status = CallWithTraceback(L, 0, LUA_MULTRET);
		if (status != LUA_OK) {
			LuaError("Failed to execute script: " << lua_tostring(L, -1));
			lua_pop(L, 1); // pop error message from the stack
			return {};
		}

		return lua_gettop(L) - top;
	}

	std::optional<int32_t> State::GetHitChance(CDivinityStats_Character * attacker, CDivinityStats_Character * target)
	{
		Restriction restriction(*this, RestrictAll);

		PushExtFunction(L, "_GetHitChance"); // stack: fn
		auto _{ PushArguments(L,
			std::tuple{Push<ObjectProxy<CDivinityStats_Character>>(attacker),
			Push<ObjectProxy<CDivinityStats_Character>>(target)}) };

		auto result = CheckedCall<std::optional<int32_t>>(L, 2, "Ext.GetHitChance");
		if (result) {
			return std::get<0>(*result);
		} else {
			return {};
		}
	}

	bool State::GetSkillDamage(SkillPrototype * skill, DamagePairList * damageList,
		CRPGStats_ObjectInstance *attacker, bool isFromItem, bool stealthed, float * attackerPosition,
		float * targetPosition, DeathType * pDeathType, int level, bool noRandomization)
	{
		Restriction restriction(*this, RestrictAll);

		PushExtFunction(L, "_GetSkillDamage"); // stack: fn

		auto luaSkill = SkillPrototypeProxy::New(L, skill, -1); // stack: fn, skill
		UnbindablePin _(luaSkill);
		ItemOrCharacterPushPin _a(L, attacker);

		push(L, isFromItem);
		push(L, stealthed);
		
		// Push attacker position
		lua_newtable(L);
		settable(L, 1, attackerPosition[0]);
		settable(L, 2, attackerPosition[1]);
		settable(L, 3, attackerPosition[2]);

		// Push target position
		lua_newtable(L);
		settable(L, 1, targetPosition[0]);
		settable(L, 2, targetPosition[1]);
		settable(L, 3, targetPosition[2]);

		push(L, level);
		push(L, noRandomization);

		auto result = CheckedCall<std::optional<DeathType>, std::optional<DamageList *>>(L, 8, "Ext.GetSkillDamage");
		if (result) {
			auto deathType = std::get<0>(*result);
			auto damages = std::get<1>(*result);

			if (deathType && damages) {
				if (pDeathType) {
					*pDeathType = *deathType;
				}

				for (auto const& dmg : (*damages)->Get()) {
					damageList->AddDamage(dmg.DamageType, dmg.Amount);
				}

				return true;
			}
		}

		return false;
	}

	std::optional<std::pair<int, bool>> State::GetSkillAPCost(SkillPrototype* skill, CDivinityStats_Character* character, eoc::AiGrid* aiGrid,
		glm::vec3* position, float* radius)
	{
		Restriction restriction(*this, RestrictAll);

		PushExtFunction(L, "_GetSkillAPCost");

		auto luaSkill = SkillPrototypeProxy::New(L, skill, -1);
		UnbindablePin _(luaSkill);
		ItemOrCharacterPushPin _ch(L, character);

		if (aiGrid != nullptr) {
			ObjectProxy<eoc::AiGrid>::New(L, aiGrid);
		} else {
			push(L, nullptr);
		}

		if (position != nullptr) {
			push(L, *position);
		} else {
			push(L, nullptr);
		}

		if (radius != nullptr) {
			push(L, *radius);
		} else {
			push(L, nullptr);
		}

		auto result = CheckedCall<std::optional<bool>, std::optional<int>>(L, 5, "Ext.GetSkillAPCost");
		if (result) {
			auto ap = std::get<1>(*result);
			auto elementalAffinity = std::get<0>(*result);

			if (ap && elementalAffinity) {
				return std::make_pair(*ap, *elementalAffinity);
			}
		}

		return {};
	}

	void State::OnNetMessageReceived(STDString const & channel, STDString const & payload, UserId userId)
	{
		CallExt("_NetMessageReceived", 0, ReturnType<>{}, channel, payload, userId.Id);
	}

	void State::OnGameSessionLoading()
	{
		CallExt("_OnGameSessionLoading", RestrictAll | ScopeSessionLoad, ReturnType<>{});
	}

	void State::OnGameSessionLoaded()
	{
		CallExt("_OnGameSessionLoaded", RestrictAll, ReturnType<>{});
	}

	void State::OnModuleLoadStarted()
	{
		CallExt("_OnModuleLoadStarted", RestrictAll | ScopeModulePreLoad, ReturnType<>{});
	}

	void State::OnModuleLoading()
	{
		CallExt("_OnModuleLoading", RestrictAll | ScopeModuleLoad, ReturnType<>{});
	}

	void State::OnStatsLoaded()
	{
		CallExt("_OnStatsLoaded", RestrictAll | ScopeModuleLoad, ReturnType<>{});
	}

	void State::OnModuleResume()
	{
		CallExt("_OnModuleResume", RestrictAll | ScopeModuleResume, ReturnType<>{});
	}

	STDString State::GetBuiltinLibrary(int resourceId)
	{
		auto hResource = FindResource(gThisModule, MAKEINTRESOURCE(resourceId),
			L"LUA_SCRIPT");

		if (hResource) {
			auto hGlobal = LoadResource(gThisModule, hResource);
			if (hGlobal) {
				auto resourceData = LockResource(hGlobal);
				if (resourceData) {
					DWORD resourceSize = SizeofResource(gThisModule, hResource);
					STDString script;
					script.resize(resourceSize);
					memcpy(script.data(), resourceData, resourceSize);
					return script;
				}
			}
		}

		OsiErrorS("Could not find bootstrap resource!");
		return STDString();
	}

}
