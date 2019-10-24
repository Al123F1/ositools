#include "stdafx.h"
#include "DataLibraries.h"
#include <string>
#include <functional>
#include <psapi.h>

#if defined(OSI_EOCAPP)
namespace osidbg
{
	bool LibraryManager::FindEoCApp(uint8_t const * & start, size_t & size)
	{
		HMODULE hEoCPlugin = GetModuleHandleW(L"EoCPlugin.dll");
		if (hEoCPlugin != NULL) {
			MessageBoxW(NULL, L"This version of the Osiris Extender can only be used ingame.", L"Osiris Extender Error", MB_OK | MB_ICONERROR);
			TerminateProcess(GetCurrentProcess(), 1);
		}

		HMODULE hEoCApp = GetModuleHandleW(L"EoCApp.exe");
		if (hEoCApp == NULL) {
			return false;
		}

		MODULEINFO moduleInfo;
		if (!GetModuleInformation(GetCurrentProcess(), hEoCApp, &moduleInfo, sizeof(moduleInfo))) {
			Fail(L"Could not get module info of EoCApp.exe");
		}

		start = (uint8_t const *)moduleInfo.lpBaseOfDll;
		size = moduleInfo.SizeOfImage;
		return true;
	}

	void LibraryManager::FindLibrariesEoCApp()
	{
		uint8_t const prologue1[] = {
			0x8B, 0xC8, // mov ecx, eax
			0xFF, 0xC0, // inc eax
			0x89, 0x05 // mov cs:xxx, eax
		};

		uint8_t const prologue2[] = {
			0x85, 0xC9, // test ecx, ecx
			0x75, 0x17, // jnz short loc_xxx
			0x44, 0x8D, 0x41 // lea r8d, [rdx+XXh]
		};

		uint8_t const prologue3[] = {
			0x48, 0x8D, 0x0D // lea rcx, xxx
		};

		uint8_t const * p = (uint8_t const *)moduleStart_;
		uint8_t const * moduleEnd = p + moduleSize_;

		for (; p < moduleEnd - 100; p++) {
			if (*p == 0x8B
				// && memcmp(p, prologue0, sizeof(prologue0)) == 0
				&& memcmp(p + 0x06, prologue1, sizeof(prologue1)) == 0
				&& memcmp(p + 0x10, prologue2, sizeof(prologue2)) == 0
				&& memcmp(p + 0x18, prologue3, sizeof(prologue3)) == 0) {
				int32_t rel1 = *(int32_t *)(p + 0x1B);
				int32_t rel2 = *(int32_t *)(p + 0x22);

				uint8_t const * initFunc = p + rel1 + 0x18 + 7;
				uint8_t const * freeFunc = p + rel2 + 0x1F + 7;

				auto it = libraries_.find(initFunc);
				if (it != libraries_.end()) {
					it->second.refs++;
				}
				else {
					libraries_.insert(std::pair<uint8_t const *, EoCLibraryInfo>(initFunc, { initFunc, freeFunc, 1 }));
				}
			}
		}

#if 0
		Debug("LibraryManager::FindLibrariesEoCApp(): Found libraries:");
		for (auto const & v : libraries_) {
			Debug("\t(Init %p; Dtor %p, Refs %d)!", v.second.initFunc, v.second.freeFunc, v.second.refs);
		}
#endif
	}

	void LibraryManager::FindServerGlobalsEoCApp()
	{
		EoCLibraryInfo const * serverLib{ nullptr };
		for (auto const & v : libraries_) {
			if (v.second.refs > 100) {
				serverLib = &v.second;
				break;
			}
		}

		if (serverLib == nullptr) {
			Fail("LibraryManager::FindServerGlobalsEoCApp(): Could not find esv library!");
			return;
		}

		for (auto p = serverLib->initFunc; p < serverLib->initFunc + 0x1000; p++) {
			if (p[0] == 0x48 && p[1] == 0x89 && p[2] == 0x05
				&& p[7] == 0xEB && p[8] == 0x07
				&& p[9] == 0x48 && p[10] == 0x89
				&& p[16] == 0xE8
				&& p[21] == 0xE8
				&& p[26] == 0xE8
				&& p[31] == 0xE8
				&& p[36] == 0xE8) {

				uint8_t const * funcPtr = p + 16;
				for (auto i = 0; i < 36; i++) {
					int32_t rel = *(int32_t *)(funcPtr + 1);
					uint8_t const * registrantFunc = funcPtr + rel + 5;
					serverRegisterFuncs_[i] = registrantFunc;
					funcPtr += 5;
				}

				funcPtr = p + 16 + (36 * 5) + 0x2C;
				for (auto i = 0; i < 14; i++) {
					int32_t rel = *(int32_t *)(funcPtr + 1);
					uint8_t const * registrantFunc = funcPtr + rel + 5;
					serverRegisterFuncs_[i + 36] = registrantFunc;
					funcPtr += 5;
				}

				for (auto i = 0; i < std::size(serverRegisterFuncs_); i++) {
					uint8_t const * r = serverRegisterFuncs_[i];
					if (r[0] == 0x48 && r[1] == 0x83 && r[2] == 0xEC /* sub rsp, XXh */
						&& r[4] == 0x48 && r[5] == 0x8B && r[6] == 0x05 /* mov rax, cs:xxx */) {

						int32_t rel = *(int32_t *)(r + 7);
						uint8_t const * registrantObj = r + rel + 4 + 7;
						serverGlobals_[i] = (uint8_t const **)registrantObj;

					}
					else {
						serverGlobals_[i] = nullptr;
						Debug("LibraryManager::FindServerGlobalsEoCApp(): Could not extract global from func @ %p", r);
					}
				}
			}
		}

		EsvCharacterFactory = (CharacterFactory **)serverGlobals_[(unsigned)EsvGlobalEoCApp::EsvCharacterFactory];
		EsvItemFactory = (ItemFactory **)serverGlobals_[(unsigned)EsvGlobalEoCApp::EsvItemFactory];
	}

	void LibraryManager::FindEoCGlobalsEoCApp()
	{
		static uint8_t const libInitSig[] = {
			0x48, 0x89, 0x18,       // mov     [rax], rbx
			0x48, 0x89, 0x58, 0x08, // mov     [rax+8], rbx
			0x48, 0x89, 0x58, 0x10, // mov     [rax+10h], rbx
			0x48, 0x8D, 0x40, 0x40, // lea     rax, [rax+40h]
			0x48, 0x89, 0x58, 0xD8, // mov     [rax-28h], rbx
			0x48, 0x89, 0x58, 0xE0  // mov     [rax-20h], rbx
		};

		uint8_t const * globalsInitCode{ nullptr };
		for (auto const & lib : libraries_) {
			for (auto p = lib.second.initFunc; p < lib.second.initFunc + 0x300; p++) {
				if (p[0] == 0x48 && memcmp(p, libInitSig, sizeof(libInitSig)) == 0) {

					for (auto p2 = p + 0x20; p2 < p + 0x120; p2++) {
						if (p2[0] == 0xE8
							&& p2[5] == 0xE8
							&& p2[10] == 0xE8
							&& p2[15] == 0xE8
							&& p2[20] == 0xE8) {
							globalsInitCode = p2;
							break;
						}
					}
				}
			}
		}

		if (globalsInitCode == nullptr) {
			Fail("LibraryManager::FindEoCGlobalsEoCApp(): Could not find eoc library init function!");
			return;
		}

		for (auto i = 1; i < std::size(eocRegisterFuncs_); i++) {
			int32_t rel = *(int32_t *)(globalsInitCode + 1);
			uint8_t const * registrantFunc = globalsInitCode + rel + 5;
			eocRegisterFuncs_[i] = registrantFunc;
			globalsInitCode += 5;
		}

		for (auto i = 1; i < std::size(eocRegisterFuncs_); i++) {
			uint8_t const * r = eocRegisterFuncs_[i];
			if (r[0] == 0x48 && r[1] == 0x83 && r[2] == 0xEC /* sub rsp, XXh */
				&& r[4] == 0x48 && r[5] == 0x8B && r[6] == 0x05 /* mov rax, cs:xxx */) {

				int32_t rel = *(int32_t *)(r + 7);
				uint8_t const * registrantObj = r + rel + 4 + 7;
				eocGlobals_[i] = (uint8_t const **)registrantObj;

			}
			else {
				eocGlobals_[i] = nullptr;
				Debug("LibraryManager::FindEoCGlobalsEoCApp(): Could not extract global from func @ %p", r);
			}
		}
	}

	void LibraryManager::FindGlobalStringTableEoCApp()
	{
		static uint8_t const sig1[] = {
			0xB9, 0x88, 0xFA, 0x5F, 0x00, // mov ecx, 5FFA88h
			0xE8, // call xxx
		};

		static uint8_t const sig2[] = {
			0x48, 0x85, 0xC0, //  test rax, rax
			0x74, 0x14, // jz xxx
			0x48, 0x8B, 0xC8, // mov rcx, rax
			0xE8, // call xxx
		};

		static uint8_t const sig3[] = {
			0x48, 0x89, 0x05 // mov xxx, rax
		};

		uint8_t const * p = (uint8_t const *)moduleStart_;
		uint8_t const * moduleEnd = p + moduleSize_;

		for (; p < moduleEnd - 100; p++) {
			if (*p == 0xB9
				&& memcmp(p, sig1, sizeof(sig1)) == 0
				&& memcmp(p + 10, sig2, sizeof(sig2)) == 0
				&& memcmp(p + 23, sig3, sizeof(sig3)) == 0) {
				int32_t rel = *(int32_t *)(p + 26);

				GlobalStrings = (GlobalStringTable const **)(p + rel + 23 + 7);
				GlobalStringTable::UseMurmur = true;
				break;
			}
		}

		if (GlobalStrings == nullptr) {
			Debug("LibraryManager::FindGlobalStringTableEoCApp(): Could not find global string table");
		}
	}

	void LibraryManager::FindGameActionManagerEoCApp()
	{
		Pattern p;
		p.FromString(
			"48 8D 15 XX XX XX XX " // lea     rdx, fsx_GameAction
			"XX 8B XX " // mov     xx, xx
			"FF 90 80 00 00 00 " // call    qword ptr [rax+80h]
			"84 C0 " // test    al, al
			"0F 84 XX XX 00 00 " // jz      xxx
			"84 DB " // test    bl, bl
			"74 XX " // jz      short loc_xxx
			"4C 8B 06 " // mov     r8, [rsi]
			"49 8B XX " // mov     rcx, xx
			"8B 56 08 " // mov     edx, [rsi+8]
			"E8 XX XX XX XX " // call    esv__GameActionManager__CreateAction
		);

		p.Scan(moduleStart_, moduleSize_, [this](const uint8_t * match) {
			auto fsx = AsmLeaToAbsoluteAddress(match);
			if (IsFixedStringRef(fsx, "GameAction")) {
				auto actionAddr = AsmCallToAbsoluteAddress(match + 37);
				CreateGameAction = (esv::GameActionManager__CreateAction)actionAddr;
			}
		});

		if (CreateGameAction == nullptr) {
			Pattern alternate;
			alternate.FromString(
				"48 8D 15 XX XX XX XX " // lea     rdx, fsx_GameAction
				"XX 8B XX " // mov     xx, xx
				"FF 90 80 00 00 00 " // call    qword ptr [rax+80h]
				"84 C0 " // test    al, al
				"0F 84 XX XX 00 00 " // jz      xxx
				"45 84 FF " // test    r15b, r15b
				"74 XX " // jz      short loc_xxx
				"4C 8B 06 " // mov     r8, [rsi]
				"49 8B XX " // mov     rcx, xx
				"8B 56 08 " // mov     edx, [rsi+8]
				"E8 XX XX XX XX " // call    esv__GameActionManager__CreateAction
			);

			alternate.Scan(moduleStart_, moduleSize_, [this](const uint8_t * match) {
				auto fsx = AsmLeaToAbsoluteAddress(match);
				if (IsFixedStringRef(fsx, "GameAction")) {
					auto actionAddr = AsmCallToAbsoluteAddress(match + 38);
					CreateGameAction = (esv::GameActionManager__CreateAction)actionAddr;
				}
			});

			if (CreateGameAction == nullptr) {
				Debug("LibraryManager::FindGameActionManagerEoCApp(): Could not find GameActionManager::CreateAction");
			}
		}

		Pattern p2;
		p2.FromString(
			"0F 84 XX 00 00 00 " // jz      xxx
			"4C 8B 00 " // mov     r8, [rax]
			"48 8D 15 XX XX XX XX " // lea     rdx, fs_ForceMove
			"48 8B C8 " // mov     rcx, rax
			"41 FF 50 28 " // call    qword ptr [r8+28h]
			"4D 8B 06 " // mov     r8, [r14]
			"49 8B CE " // mov     rcx, r14
		);

		Pattern p3;
		p3.FromString(
			"48 8B 05 XX XX XX XX " // mov     rax, cs:ls__gServerLevelAllocator
			"4C 8D 44 24 60 " // lea     r8, [rsp+58h+arg_0]
			"48 8D 54 24 70 " // lea     rdx, [rsp+58h+arg_10]
			"C7 44 24 60 FF FF FF FF " // mov     dword ptr [rsp+58h+arg_0], 0FFFFFFFFh
			"48 8B 88 80 00 00 00 " //  mov     rcx, [rax+80h]
			"48 8B 05 XX XX XX XX " // mov     rax, cs:ls__ObjectHandle__Unassigned
			"48 89 44 24 70 " // mov     [rsp+58h+arg_10], rax
			"48 8B B9 D0 00 00 00 " // mov     rdi, [rcx+0D0h]
			"48 8B CF " // mov     rcx, rdi
			"E8 XX XX XX XX " // call    xxx
			"4C 8D 46 70 " // lea     r8, [rsi+70h]
			"48 8B C8 " // mov     rcx, rax
			"48 8D 56 68 " // lea     rdx, [rsi+68h]
			"48 8B D8 " // mov     rbx, rax
			"E8 XX XX XX XX " // call    esv__GameObjectMoveAction__Setup
			"48 8B D3 " // mov     rdx, rbx
			"48 8B CF " // mov     rcx, rdi
			"E8 XX XX XX XX " // call    esv__GameActionManager__AddAction
		);

		p2.Scan(moduleStart_, moduleSize_, [this, &p3](const uint8_t * match) {
			auto fsx = AsmLeaToAbsoluteAddress(match + 9);
			if (IsFixedStringRef(fsx, "ForceMove")) {
				p3.Scan(match, 0x100, [this](const uint8_t * match2) {
					LevelManager = (void **)AsmLeaToAbsoluteAddress(match2);

					auto moveSetupAddr = AsmCallToAbsoluteAddress(match2 + 73);
					GameObjectMoveActionSetup = (esv::GameObjectMoveAction__Setup)moveSetupAddr;

					auto addActionAddr = AsmCallToAbsoluteAddress(match2 + 84);
					AddGameAction = (esv::GameActionManager__AddAction)addActionAddr;
				});

				if (LevelManager == nullptr || AddGameAction == nullptr) {
					Pattern alternate;
					alternate.FromString(
						"48 8B 05 XX XX XX XX " // mov     rax, cs:ls__gServerLevelAllocator
						"4C 8D 44 24 60 " // lea     r8, [rsp+58h+arg_0]
						"48 8D 54 24 70 " // lea     rdx, [rsp+58h+arg_10]
						"C7 44 24 60 FF FF FF FF " // mov     dword ptr [rsp+58h+arg_0], 0FFFFFFFFh
						"48 8B 88 80 00 00 00 " //  mov     rcx, [rax+80h]
						"48 8B 05 XX XX XX XX " // mov     rax, cs:ls__ObjectHandle__Unassigned
						"48 89 44 24 70 " // mov     [rsp+58h+arg_10], rax
						"48 8B B9 D0 00 00 00 " // mov     rdi, [rcx+0D0h]
						"48 8B CF " // mov     rcx, rdi
						"E8 XX XX XX XX " // call    xxx
						"4C 8D 46 70 " // lea     r8, [rsi+70h]
						"48 8B C8 " // mov     rcx, rax
						"48 8D 56 68 " // lea     rdx, [rsi+68h]
						"48 8B D8 " // mov     rbx, rax
						"E8 XX XX XX XX " // call    esv__GameObjectMoveAction__Setup
						"48 8D 8F 30 01 00 00 " // lea     rcx, [rdi+130h]
						"48 89 5C 24 60 " // mov     [rsp+58h+arg_0], rbx
						"48 8D 54 24 60 " // lea     rdx, [rsp+58h+arg_0]
						"E8 XX XX XX XX " // call    esv__GameActionManager__AddAction
					);

					alternate.Scan(match, 0x100, [this](const uint8_t * match2) {
						LevelManager = (void **)AsmLeaToAbsoluteAddress(match2);

						auto moveSetupAddr = AsmCallToAbsoluteAddress(match2 + 73);
						GameObjectMoveActionSetup = (esv::GameObjectMoveAction__Setup)moveSetupAddr;

						auto addActionAddr = AsmCallToAbsoluteAddress(match2 + 95);
						AddGameAction = (esv::GameActionManager__AddAction)addActionAddr;
					});
				}
			}
		});

		if (LevelManager == nullptr || AddGameAction == nullptr) {
			Debug("LibraryManager::FindGameActionManagerEoCApp(): Could not find esv::LevelManager");
		}
	}

	void LibraryManager::FindGameActionsEoCApp()
	{
		Pattern p;
		p.FromString(
			"EB XX " // jmp     short xxx
			"48 8B 00 " // mov     rax, [rax]
			"48 8D 15 XX XX XX XX " // lea     rdx, fsx_RandomPoints
			"48 8B XX " // mov     rcx, rbx
			"FF 50 28 " // call    qword ptr [rax+28h]
			"4C 8B XX " // mov     r8, [rbx]
		);

		Pattern p2;
		p2.FromString(
			"48 8B C4 " // mov     rax, rsp
			"53 " // push    rbx
		);

		p.Scan(moduleStart_, moduleSize_, [this, &p2](const uint8_t * match) {
			auto fsx = AsmLeaToAbsoluteAddress(match + 5);
			if (IsFixedStringRef(fsx, "RandomPoints")) {
				p2.Scan(match - 0x200, 0x200, [this](const uint8_t * match2) {
					TornadoActionSetup = (esv::TornadoAction__Setup)match2;
				});
			}
		});

		if (TornadoActionSetup == nullptr) {
			Debug("LibraryManager::FindGameActionsEoCApp(): Could not find TornadoAction");
		}

		// FIXME: WallAction, SummonHelpers::Summon
	}

	void LibraryManager::FindStatusMachineEoCApp()
	{
		Pattern p;
		p.FromString(
			"83 7A 1C 00 " // cmp     dword ptr [rdx+1Ch], 0
			"48 8B F2 " // mov     rsi, rdx
			"4C 8B F1 " // mov     r14, rcx
			"0F 8E 8C 00 00 00 " // jle     short xxx
			"4C 8B 05 XX XX XX XX " // mov     r8, cs:?Unassigned@ObjectHandle@ls@@2V12@B
			"48 8D 15 XX XX XX XX " // lea     rdx, fs_LIFESTEAL
			"48 89 5C 24 30 " //  mov     [rsp+28h+arg_0], rbx
			"48 89 7C 24 40 " //  mov     [rsp+28h+arg_10], rdi
			"48 8B B9 A0 01 00 00 " //  mov     rdi, [rcx+1A0h]
			"48 8B CF " //  mov     rcx, rdi 
			"E8 XX XX XX XX " //  call    esv__StatusMachine__CreateStatus
		);

		uint8_t const * lastMatch;
		p.Scan(moduleStart_, moduleSize_, [this, &lastMatch](const uint8_t * match) {
			auto fsx = AsmLeaToAbsoluteAddress(match + 23);
			if (IsFixedStringRef(fsx, "LIFESTEAL")) {
				auto actionAddr = AsmCallToAbsoluteAddress(match + 50);
				lastMatch = match + 55;
				StatusMachineCreateStatus = (esv::StatusMachine__CreateStatus)actionAddr;
			}
		});

		if (StatusMachineCreateStatus == nullptr) {
			Debug("LibraryManager::FindStatusMachineEoCApp(): Could not find StatusMachine::CreateStatus");
		}

		Pattern p2;
		p2.FromString(
			"C7 43 2C 00 00 00 00 " // mov     dword ptr [rbx+2Ch], 0
			"48 8B CF " // mov     rcx, rdi
			"E8 XX XX XX XX " // call    esv__StatusMachine__ApplyStatus
			"48 8B 7C 24 40 " // mov     rdi, [rsp+28h+arg_10]
		);

		p2.Scan(lastMatch, 0x100, [this](const uint8_t * match) {
			auto actionAddr = AsmCallToAbsoluteAddress(match + 10);
			StatusMachineApplyStatus = (esv::StatusMachine__ApplyStatus)actionAddr;
		});

		if (StatusMachineApplyStatus == nullptr) {
			Debug("LibraryManager::FindStatusMachineEoCApp(): Could not find StatusMachine::ApplyStatus");
		}
	}

	void LibraryManager::FindStatusTypesEoCApp()
	{
		Pattern p;
		p.FromString(
			"45 33 C9 " // xor     r9d, r9d
			"48 8D 15 XX XX XX XX " // lea     rdx, fsx_TargetDependentHeal
			"48 8B CB " // mov     rcx, rbx
			"FF 90 B0 01 00 00 " // call    qword ptr [rax+1B0h]
		);

		Pattern p2;
		p2.FromString(
			"48 89 5C 24 10 " // mov     [rsp-8+arg_8], rbx
			"48 89 74 24 18 " // mov     [rsp-8+arg_10], rsi
		);

		p.Scan(moduleStart_, moduleSize_, [this, &p2](const uint8_t * match) {
			auto fsx = AsmLeaToAbsoluteAddress(match + 3);
			if (IsFixedStringRef(fsx, "TargetDependentHeal")) {
				p2.Scan(match - 0x200, 0x200, [this](const uint8_t * match) {
					// Look for this function ptr
					auto ptr = (uint64_t)match;
					for (auto p = moduleStart_; p < moduleStart_ + moduleSize_; p += 8) {
						if (*reinterpret_cast<uint64_t const *>(p) == ptr) {
							StatusHealVMT = reinterpret_cast<esv::StatusVMT const *>(p - 25 * 8);
						}
					}
				});
			}
		});

		if (StatusHealVMT == nullptr) {
			Debug("LibraryManager::FindStatusTypesEoCApp(): Could not find esv::StatusHeal");
		}

		Pattern p3;
		p3.FromString(
			"4C 8D 0D XX XX XX XX " // lea     r9, fsx_Dummy_BodyFX
			"48 8D 15 XX XX XX XX " // lea     rdx, fsx_RS3_FX_GP_Status_Retaliation_Beam_01
			"E8 XX XX XX XX " // call    esv__EffectFactory__CreateEffectWrapper
			"48 8B D8 " // mov     rbx, rax
		);

		Pattern p4;
		p4.FromString(
			"40 55 " // push    rbp
			"41 54 " // push    r12
			"41 55 " // push    r13
			"41 57 " // push    r15
		);

		p3.Scan(moduleStart_, moduleSize_, [this, &p4](const uint8_t * match) {
			auto fsx = AsmLeaToAbsoluteAddress(match + 7);
			if (IsFixedStringRef(fsx, "RS3_FX_GP_Status_Retaliation_Beam_01")) {
				p4.Scan(match - 0xa00, 0xa00, [this](const uint8_t * match) {
					// Look for this function ptr
					auto ptr = (uint64_t)match;
					for (auto p = moduleStart_; p < moduleStart_ + moduleSize_; p += 8) {
						if (*reinterpret_cast<uint64_t const *>(p) == ptr) {
							StatusHitVMT = reinterpret_cast<esv::StatusVMT const *>(p - 12 * 8);
						}
					}
				});
			}
		});

		if (StatusHitVMT == nullptr) {
			Debug("LibraryManager::FindStatusTypesEoCApp(): Could not find esv::StatusHit");
		}
	}

	void LibraryManager::FindHitFuncsEoCApp()
	{
		Pattern p;
		p.FromString(
			"74 74 " // jz      short loc_141A3EED4
			"48 8B 00 " // mov     rax, [rax]
			"48 8D 15 XX XX XX XX " // lea     rdx, fs_DamageItems
			"48 8B CE " // mov     rcx, rsi
			"FF 50 28 " // call    qword ptr [rax+28h]
		);

		Pattern p2;
		p2.FromString(
			"C7 44 24 20 05 00 00 00 " // mov     dword ptr [rsp+140h+var_120], 5
			"44 89 65 50 " // mov     [rbp+40h+arg_0], r12d
			"E8 XX XX XX XX " // call    esv__Character__Hit
			"4C 8B AE A0 01 00 00 " // mov     r13, [rsi+1A0h]
			"EB 5F " // jmp short xxx
		);

		p.Scan(moduleStart_, moduleSize_, [this, &p2](const uint8_t * match) {
			auto fsx = AsmLeaToAbsoluteAddress(match + 5);
			if (IsFixedStringRef(fsx, "DamageItems")) {
				p2.Scan(match, 0x400, [this](const uint8_t * match) {
					auto actionAddr = AsmCallToAbsoluteAddress(match + 12);
					CharacterHit = (esv::Character__Hit)actionAddr;
				});
			}
		});

		if (CharacterHit == nullptr) {
			Debug("LibraryManager::FindHitFuncsEoCApp(): Could not find Character::Hit");
		}
	}

	void LibraryManager::FindItemFuncsEoCApp()
	{
		Pattern p;
		p.FromString(
			"45 0F B6 C1 " // movzx   r8d, r9b
			"48 8B CF " // mov     rcx, rdi
			"E8 XX XX XX XX " // call    esv__ParseItem
			"33 D2 " // xor     edx, edx
			"48 8D 4C 24 38 " // lea     rcx, [rsp+78h+var_40]
			"E8 XX XX XX XX " // call    esv__CreateItemFromParsed
		);

		p.Scan(moduleStart_, moduleSize_, [this](const uint8_t * match) {
			auto parseAddr = AsmCallToAbsoluteAddress(match + 7);
			ParseItem = (esv::ParseItem)parseAddr;
			auto createAddr = AsmCallToAbsoluteAddress(match + 19);
			CreateItemFromParsed = (esv::CreateItemFromParsed)createAddr;
		});

		if (ParseItem == nullptr || CreateItemFromParsed == nullptr) {
			Debug("LibraryManager::FindItemFuncsEoCApp(): Could not find esv::CreateItemFromParsed");
		}
	}
}
#endif
