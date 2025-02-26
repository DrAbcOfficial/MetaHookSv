#include "stdafx.h"

#ifdef _WIN64
#define HOST_MACHINE IMAGE_FILE_MACHINE_AMD64
#else
#define HOST_MACHINE IMAGE_FILE_MACHINE_I386
#endif

#define GET_HEADER_DICTIONARY(headers, idx)  &headers->OptionalHeader.DataDirectory[idx]

#define AlignValueUp(value, alignment) ((size_t(value) + size_t(alignment) + 1) & ~(size_t(alignment) - 1))

#define OffsetPointer(data, offset) LPVOID(LPBYTE(data) + ptrdiff_t(offset))

// Protection flags for memory pages (Executable, Readable, Writeable)
static const int ProtectionFlags[2][2][2] = {
	{
		// not executable
		{PAGE_NOACCESS, PAGE_WRITECOPY},
		{PAGE_READONLY, PAGE_READWRITE},
	}, {
		// executable
		{PAGE_EXECUTE, PAGE_EXECUTE_WRITECOPY},
		{PAGE_EXECUTE_READ, PAGE_EXECUTE_READWRITE},
	},
};

SIZE_T MmpSizeOfImageHeadersUnsafe(PVOID BaseAddress) {
	PIMAGE_DOS_HEADER dh = (PIMAGE_DOS_HEADER)BaseAddress;
	PIMAGE_NT_HEADERS nh = (PIMAGE_NT_HEADERS)((LPBYTE)BaseAddress + dh->e_lfanew);

	//https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-image_optional_header32
	SIZE_T cbSizeOfHeaders = dh->e_lfanew +										// e_lfanew member of IMAGE_DOS_HEADER
		4 +																	// 4 byte signature
		sizeof(IMAGE_FILE_HEADER) +											// size of IMAGE_FILE_HEADER
		sizeof(IMAGE_OPTIONAL_HEADER) +										// size of optional header
		sizeof(IMAGE_SECTION_HEADER) * nh->FileHeader.NumberOfSections;		// size of all section headers
	return cbSizeOfHeaders;
}

PMEMORYMODULE WINAPI MapMemoryModuleHandle(HMEMORYMODULE hModule)
{
	if (!hModule)
		return nullptr;

	PIMAGE_NT_HEADERS nh = RtlImageNtHeader(hModule);

	if (!nh)
		return nullptr;

	SIZE_T cbSizeOfHeaders = MmpSizeOfImageHeadersUnsafe(hModule);

	PMEMORYMODULE pModule = (PMEMORYMODULE)((LPBYTE)hModule + cbSizeOfHeaders);

	if (pModule->Signature != MEMORY_MODULE_SIGNATURE || pModule->codeBase != (LPBYTE)hModule)
		return nullptr;

	return pModule;
}

BOOL WINAPI IsValidMemoryModuleHandle(HMEMORYMODULE hModule) {
	return MapMemoryModuleHandle(hModule) != nullptr;
}

NTSTATUS MmpInitializeStructure(DWORD ImageMemorySize, DWORD ImageFileSize, LPCVOID ImageFileBuffer, PIMAGE_NT_HEADERS ImageHeaders) {

	if (!ImageHeaders)return STATUS_ACCESS_VIOLATION;

	//
	// Make sure there have enough free space to embed our structure.
	//
	SIZE_T cbSizeOfHeaders = MmpSizeOfImageHeadersUnsafe((PVOID)ImageHeaders->OptionalHeader.ImageBase);
	PIMAGE_SECTION_HEADER pSections = IMAGE_FIRST_SECTION(ImageHeaders);
	for (int i = 0; i < ImageHeaders->FileHeader.NumberOfSections; ++i) {
		if (pSections[i].VirtualAddress < cbSizeOfHeaders + sizeof(MEMORYMODULE)) {
			return STATUS_NOT_SUPPORTED;
		}
	}

	//
	// Setup MemoryModule structure.
	//
	PMEMORYMODULE hMemoryModule = (PMEMORYMODULE)(ImageHeaders->OptionalHeader.ImageBase + cbSizeOfHeaders);
	RtlZeroMemory(hMemoryModule, sizeof(MEMORYMODULE));
	hMemoryModule->codeBase = (PBYTE)ImageHeaders->OptionalHeader.ImageBase;
	hMemoryModule->dwImageFileSize = ImageFileSize;
	hMemoryModule->dwImageMemorySize = ImageMemorySize;
	hMemoryModule->Signature = MEMORY_MODULE_SIGNATURE;
	hMemoryModule->SizeofHeaders = ImageHeaders->OptionalHeader.SizeOfHeaders;
	hMemoryModule->lpReserved = (LPVOID)ImageFileBuffer;
	hMemoryModule->dwReferenceCount = 1;

	return STATUS_SUCCESS;
}


NTSTATUS MemoryResolveImportTable(
	_In_ LPBYTE base,
	_In_ PIMAGE_NT_HEADERS lpNtHeaders,
	_In_ PMEMORYMODULE hMemoryModule) {
	NTSTATUS status = STATUS_SUCCESS;
	PIMAGE_IMPORT_DESCRIPTOR importDesc = nullptr;
	DWORD count = 0;

	do {
		__try {
			PIMAGE_DATA_DIRECTORY dir = GET_HEADER_DICTIONARY(lpNtHeaders, IMAGE_DIRECTORY_ENTRY_IMPORT);
			PIMAGE_IMPORT_DESCRIPTOR iat = nullptr;

			if (dir && dir->Size) {
				iat = importDesc = PIMAGE_IMPORT_DESCRIPTOR(lpNtHeaders->OptionalHeader.ImageBase + dir->VirtualAddress);
			}

			if (iat) {
				while (iat->Name) {
					++count;
					++iat;
				}
			}

			if (importDesc && count) {
				hMemoryModule->hModulesList = (HMODULE*)RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, HEAP_ZERO_MEMORY, sizeof(HMODULE) * count);
				if (!hMemoryModule->hModulesList) {
					status = STATUS_NO_MEMORY;
					break;
				}

				for (DWORD i = 0; i < count; ++i, ++importDesc) {
					uintptr_t* thunkRef;
					FARPROC* funcRef;
					HMODULE handle = LoadLibraryA((LPCSTR)(base + importDesc->Name));

					if (!handle) {
						status = STATUS_DLL_NOT_FOUND;
						break;
					}

					hMemoryModule->hModulesList[hMemoryModule->dwModulesCount++] = handle;
					thunkRef = (uintptr_t*)(base + (importDesc->OriginalFirstThunk ? importDesc->OriginalFirstThunk : importDesc->FirstThunk));
					funcRef = (FARPROC*)(base + importDesc->FirstThunk);
					while (*thunkRef) {
						*funcRef = GetProcAddress(
							handle,
							IMAGE_SNAP_BY_ORDINAL(*thunkRef) ? (LPCSTR)IMAGE_ORDINAL(*thunkRef) : (LPCSTR)PIMAGE_IMPORT_BY_NAME(base + (*thunkRef))->Name
						);
						if (!*funcRef) {
							status = STATUS_ENTRYPOINT_NOT_FOUND;
							break;
						}
						++thunkRef;
						++funcRef;
					}

					if (!NT_SUCCESS(status))break;
				}

			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			status = GetExceptionCode();
		}
	} while (false);

	if (!NT_SUCCESS(status)) {
		for (DWORD i = 0; i < hMemoryModule->dwModulesCount; ++i)
			FreeLibrary(hMemoryModule->hModulesList[i]);

		RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, hMemoryModule->hModulesList);
		hMemoryModule->hModulesList = nullptr;
		hMemoryModule->dwModulesCount = 0;
	}

	return status;
}

NTSTATUS MemorySetSectionProtection(
	_In_ LPBYTE base,
	_In_ PIMAGE_NT_HEADERS lpNtHeaders,
	_In_ DWORD dwFlags) {
	NTSTATUS status = STATUS_SUCCESS;
	PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(lpNtHeaders);

	//
	// Determine whether it is a .NET assembly
	//
	auto& com = lpNtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR];
	bool CorImage = (com.Size && com.VirtualAddress);

	for (DWORD i = 0; i < lpNtHeaders->FileHeader.NumberOfSections; ++i, ++section) {
		LPVOID address = LPBYTE(base) + section->VirtualAddress;
		SIZE_T size = AlignValueUp(section->Misc.VirtualSize, lpNtHeaders->OptionalHeader.SectionAlignment);

		if ((section->Characteristics & IMAGE_SCN_MEM_DISCARDABLE) && !CorImage) {
			//
			// If it is a .NET assembly, we cannot release this memory block
			//
			if (dwFlags & LOAD_FLAGS_NO_DISCARD_SECTION)
			{

			}
			else
			{
#pragma warning(disable:6250)
				VirtualFree(address, size, MEM_DECOMMIT);
#pragma warning(default:6250)
			}
		}
		else {
			BOOL executable = (section->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0,
				readable = (section->Characteristics & IMAGE_SCN_MEM_READ) != 0,
				writeable = (section->Characteristics & IMAGE_SCN_MEM_WRITE) != 0;

			if (dwFlags & LOAD_FLAGS_NO_EXECUTE)
			{
				executable = FALSE;
			}

			if (dwFlags & LOAD_FLAGS_READ_ONLY)
			{
				writeable = FALSE;
			}

			if (dwFlags & LOAD_FLAGS_NO_DISCARD_SECTION)
			{
				readable = TRUE;
			}

			DWORD protect = ProtectionFlags[executable][readable][writeable];
			DWORD oldProtect = 0;

			if (section->Characteristics & IMAGE_SCN_MEM_NOT_CACHED)
				protect |= PAGE_NOCACHE;

			status = NtProtectVirtualMemory(NtCurrentProcess(), &address, &size, protect, &oldProtect);
			if (!NT_SUCCESS(status))break;
		}
	}

	return status;
}

NTSTATUS MemoryLoadLibrary(
	_Out_ HMEMORYMODULE* MemoryModuleHandle,
	_Out_opt_ DWORD* OutImageSize,
	_In_ LPCVOID data,
	_In_ DWORD size) {

	PIMAGE_DOS_HEADER dos_header = nullptr;
	PIMAGE_NT_HEADERS old_header = nullptr;
	BOOLEAN CorImage = FALSE;
	NTSTATUS status = STATUS_SUCCESS;

	//
	// Check parameters
	//
	__try
	{

		(*MemoryModuleHandle) = nullptr;

		//
		// Check dos magic
		//
		dos_header = (PIMAGE_DOS_HEADER)data;
		if (dos_header->e_magic != IMAGE_DOS_SIGNATURE) {
			status = STATUS_INVALID_IMAGE_FORMAT;
			__leave;
		}

		//
		// Check nt headers
		//
		old_header = (PIMAGE_NT_HEADERS)((size_t)data + dos_header->e_lfanew);
		if (old_header->Signature != IMAGE_NT_SIGNATURE ||
			(old_header->OptionalHeader.SectionAlignment & 1) ) {
			status = STATUS_INVALID_IMAGE_FORMAT;
			__leave;
		}

		//
		// Match machine type
		//
		if (old_header->FileHeader.Machine != HOST_MACHINE) {
			status = STATUS_IMAGE_MACHINE_TYPE_MISMATCH;
			__leave;
		}

		//
		// Only dll image support
		//
		if (!(old_header->FileHeader.Characteristics & IMAGE_FILE_DLL) && 
			!(old_header->FileHeader.Characteristics & IMAGE_FILE_SYSTEM) && 
			!(old_header->FileHeader.Characteristics & IMAGE_FILE_EXECUTABLE_IMAGE)) {
			status = STATUS_NOT_SUPPORTED;
			__leave;
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		status = GetExceptionCode();
	}

	if (!NT_SUCCESS(status) || status == STATUS_IMAGE_MACHINE_TYPE_MISMATCH)
		return status;

	//
	// Reserve the address range of image
	//
	LPBYTE base = nullptr;
	if ((old_header->OptionalHeader.DllCharacteristics & IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE) == 0) {
		base = (LPBYTE)VirtualAlloc(
			LPVOID(old_header->OptionalHeader.ImageBase),
			old_header->OptionalHeader.SizeOfImage,
			MEM_RESERVE,
			PAGE_READWRITE
		);
	}
	if (!base) {
		base = (LPBYTE)VirtualAlloc(
			nullptr,
			old_header->OptionalHeader.SizeOfImage,
			MEM_RESERVE,
			PAGE_READWRITE
		);

		if (!base)
			status = STATUS_NO_MEMORY;
	}

	if (OutImageSize)
	{
		*OutImageSize = old_header->OptionalHeader.SizeOfImage;
	}

	if (!NT_SUCCESS(status)) {
		return status;
	}

	//
	// Allocate memory for image headers
	//
	size_t cbAlignedHeadersSize = (DWORD)AlignValueUp(old_header->OptionalHeader.SizeOfHeaders + sizeof(MEMORYMODULE), MmpGlobalDataPtr->SystemInfo.dwPageSize);
	if (!VirtualAlloc(base, cbAlignedHeadersSize, MEM_COMMIT, PAGE_READWRITE)) {
		VirtualFree(base, 0, MEM_RELEASE);
		status = STATUS_NO_MEMORY;
		return status;
	}

	//
	// Copy headers
	//
	PIMAGE_DOS_HEADER new_dos_header = (PIMAGE_DOS_HEADER)base;
	PIMAGE_NT_HEADERS new_header = (PIMAGE_NT_HEADERS)(base + dos_header->e_lfanew);
	RtlCopyMemory(
		new_dos_header,
		dos_header,
		old_header->OptionalHeader.SizeOfHeaders
	);
	new_header->OptionalHeader.ImageBase = (size_t)base;

	do {
		//
		// Setup MEMORYMODULE structure.
		//
		status = MmpInitializeStructure(old_header->OptionalHeader.SizeOfImage, size, data, new_header);
		if (!NT_SUCCESS(status)) break;

		//
		// Allocate and copy sections
		//
		PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(new_header);
		for (DWORD i = 0; i < new_header->FileHeader.NumberOfSections; ++i, ++section) {

			DWORD dwSectionSize = AlignValueUp(
				section->Misc.VirtualSize,
				new_header->OptionalHeader.SectionAlignment
			);

			if (dwSectionSize < section->SizeOfRawData) {
				status = STATUS_INVALID_IMAGE_FORMAT;
				break;
			}

			LPVOID SectionBase = VirtualAlloc(
				(LPSTR)new_header->OptionalHeader.ImageBase + section->VirtualAddress,
				dwSectionSize,
				MEM_COMMIT,
				PAGE_READWRITE
			);

			if (!SectionBase) {
				status = STATUS_NO_MEMORY;
				break;
			}

			if (section->Characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA)
			{
				memset(SectionBase, 0, dwSectionSize);
			}

			if (section->SizeOfRawData)
			{
				RtlCopyMemory(
					SectionBase,
					LPBYTE(data) + section->PointerToRawData,
					section->SizeOfRawData
				);
			}

		}

		if (!NT_SUCCESS(status))
			break;

		__try {

			//Can be rebased ?
			if ((old_header->OptionalHeader.DllCharacteristics & IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE) == IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE) {

				auto locationDelta = new_header->OptionalHeader.ImageBase - old_header->OptionalHeader.ImageBase;
				if (locationDelta) {
					typedef struct _REBASE_INFO {
						USHORT Offset : 12;
						USHORT Type : 4;
					}REBASE_INFO, * PREBASE_INFO;
					typedef struct _IMAGE_BASE_RELOCATION_HEADER {
						DWORD VirtualAddress;
						DWORD SizeOfBlock;
						REBASE_INFO TypeOffset[ANYSIZE_ARRAY];

						DWORD TypeOffsetCount()const {
							return (this->SizeOfBlock - 8) / sizeof(_REBASE_INFO);
						}
					}IMAGE_BASE_RELOCATION_HEADER, * PIMAGE_BASE_RELOCATION_HEADER;

					PIMAGE_DATA_DIRECTORY dir = GET_HEADER_DICTIONARY(new_header, IMAGE_DIRECTORY_ENTRY_BASERELOC);

					if (dir->VirtualAddress > old_header->OptionalHeader.SizeOfImage)
					{
						status = STATUS_INVALID_IMAGE_FORMAT;
						break;
					}

					if (dir->Size && dir->VirtualAddress) {

						PIMAGE_BASE_RELOCATION_HEADER relocation = (PIMAGE_BASE_RELOCATION_HEADER)(LPBYTE(base) + dir->VirtualAddress);
						DWORD maxSize = dir->Size;

						while (relocation->VirtualAddress && relocation->SizeOfBlock && maxSize) {

							if (relocation->VirtualAddress > old_header->OptionalHeader.SizeOfImage)
							{
								status = STATUS_INVALID_IMAGE_FORMAT;
								break;
							}

							auto relInfo = (_REBASE_INFO*)&relocation->TypeOffset;
							for (DWORD i = 0; i < relocation->TypeOffsetCount(); ++i, ++relInfo) {

								if (relocation->VirtualAddress + relInfo->Offset > old_header->OptionalHeader.SizeOfImage)
								{
									status = STATUS_INVALID_IMAGE_FORMAT;
									break;
								}

								switch (relInfo->Type) {
								case IMAGE_REL_BASED_HIGHLOW: *(DWORD*)(base + relocation->VirtualAddress + relInfo->Offset) += (DWORD)locationDelta; break;
	#ifdef _WIN64
								case IMAGE_REL_BASED_DIR64: *(ULONGLONG*)(base + relocation->VirtualAddress + relInfo->Offset) += (ULONGLONG)locationDelta; break;
	#endif
								case IMAGE_REL_BASED_ABSOLUTE:
								default: break;
								}
							}

							// advance to next relocation block
							//relocation->VirtualAddress += module->headers_align;

							if (maxSize > relocation->SizeOfBlock)
							{
								maxSize -= relocation->SizeOfBlock;
								relocation = decltype(relocation)(OffsetPointer(relocation, relocation->SizeOfBlock));
							}
							else
							{
								break;
							}
						}
					}
				}
			}

			if (!NT_SUCCESS(status))
				break;

			(*MemoryModuleHandle) = (HMEMORYMODULE)base;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			status = GetExceptionCode();
			break;
		}

		return status;

	} while (false);

	MemoryFreeLibrary((HMEMORYMODULE)base);
	return status;
}

BOOL MemoryFreeLibrary(HMEMORYMODULE mod)
{
	PMEMORYMODULE module = MapMemoryModuleHandle(mod);
	PIMAGE_NT_HEADERS headers = RtlImageNtHeader(mod);

	if (!module) return FALSE;
	if (module->loadFromLdrLoadDllMemory && !module->underUnload)return FALSE;
	if (module->hModulesList) {
		for (DWORD i = 0; i < module->dwModulesCount; ++i) {
			if (module->hModulesList[i]) {
				FreeLibrary(module->hModulesList[i]);
			}
		}
		
		RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, module->hModulesList);
	}

	if (module->codeBase) VirtualFree(mod, 0, MEM_RELEASE);
	return TRUE;
}
